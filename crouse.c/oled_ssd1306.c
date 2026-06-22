#include "oled_ssd1306.h"
#include "i2c_hal.h"
#include "systick.h"
#include "algorithms.h"
#include "ds1302.h"
#include "globals.h"
#include "font5x7.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

#define SSD1306_ADDR (0x3C << 1)
#define CMD 0x00
#define DAT 0x40

/* ============================================================
 * framebuf 并发保护
 *
 * framebuf / dirty 被 vTaskDisplay (心率/血氧/步数/体温/时间页)
 * 和 vTaskVoice (ASR 指令触发的 Show* 调用) 同时访问, 两者优先级相同
 * (prio 2), 存在时间片抢占。若一个任务的 Clear+Draw+Flush 序列被
 * 另一任务打断, 会显示两页混杂的花屏。
 *
 * 用互斥锁把每个 Show* 函数的整段 (Clear→Draw→Flush) 包成原子操作。
 * 底层 Clear/DrawChar/DrawString/Flush 不加锁, 由上层 Show* 保证
 * 序列完整性 (避免递归加锁)。 */
static SemaphoreHandle_t g_oled_mutex = NULL;

static uint8_t framebuf[8][128];
static uint8_t dirty[8];

static void write_cmd(uint8_t cmd)
{
    I2C2_WriteReg(SSD1306_ADDR, CMD, &cmd, 1);
}

static void write_data(uint8_t *data, uint16_t len)
{
    I2C2_WriteReg(SSD1306_ADDR, DAT, data, len);
}

/* 取/还 framebuf 互斥锁。Show* 函数入口 take、出口 give。
 * 永远不阻塞超过 100ms (Display/Voice 任务 50/100ms 周期,
 * 一帧 I2C2 刷新 ~30ms, 取不到锁说明另一任务卡在 OLED, 放弃本次刷新)。 */
static int oled_lock(void)
{
    if (!g_oled_mutex) return -1;
    return (xSemaphoreTake(g_oled_mutex, pdMS_TO_TICKS(100)) == pdTRUE) ? 0 : -1;
}

static void oled_unlock(void)
{
    if (g_oled_mutex) xSemaphoreGive(g_oled_mutex);
}

void OLED_Init(void)
{
    Delay_ms(50);
    write_cmd(0xAE);
    write_cmd(0x20); write_cmd(0x00);
    write_cmd(0xB0);
    write_cmd(0xC8);
    write_cmd(0x00);
    write_cmd(0x10);
    write_cmd(0x40);
    write_cmd(0x81); write_cmd(0x7F);
    write_cmd(0xA1);
    write_cmd(0xA6);
    write_cmd(0xA8); write_cmd(0x3F);
    write_cmd(0xA4);
    write_cmd(0xD3); write_cmd(0x00);
    write_cmd(0xD5); write_cmd(0x80);
    write_cmd(0xD9); write_cmd(0xF1);
    write_cmd(0xDA); write_cmd(0x12);
    write_cmd(0xDB); write_cmd(0x40);
    write_cmd(0x8D); write_cmd(0x14);
    write_cmd(0xAF);
    OLED_Clear();

    /* 创建 framebuf 互斥锁 (调度器尚未启动, 创建安全)。
     * 堆耗尽会返回 NULL → 所有 Show* 静默不显示, 严重到必须用断言捕获。 */
    if (g_oled_mutex == NULL) {
        g_oled_mutex = xSemaphoreCreateMutex();
        configASSERT(g_oled_mutex);
    }
}

void OLED_Clear(void)
{
    memset(framebuf, 0x00, sizeof(framebuf));
    memset(dirty, 1, sizeof(dirty));
}

void OLED_DrawChar(uint8_t x, uint8_t page, char c)
{
    /* 越界保护: 字体 5px+1px 间距, x 需 <= 122; page 范围 0~7 */
    if (x > 122 || page > 7) return;

    uint8_t idx;
    if ((unsigned char)c < 32 || (unsigned char)c > 126) idx = 0;
    else idx = (unsigned char)c - 32;

    for (int i = 0; i < 5; i++) framebuf[page][x + i] = font5x7[idx][i];
    framebuf[page][x + 5] = 0x00;
    dirty[page] = 1;
}

void OLED_DrawString(uint8_t x, uint8_t page, const char *s)
{
    while (*s) {
        OLED_DrawChar(x, page, *s);
        x += 6;
        s++;
    }
}

void OLED_Flush(void)
{
    for (uint8_t page = 0; page < 8; page++) {
        if (!dirty[page]) continue;
        write_cmd(0xB0 + page);
        write_cmd(0x00);
        write_cmd(0x10);
        write_data(framebuf[page], 128);
        dirty[page] = 0;
    }
}

/* ============================================================
 * 像素级绘图原语 + 居中排版工具
 * ============================================================ */

/* 设置单个像素 (x: 0-127, y: 0-63) */
static void oled_setpixel(uint8_t x, uint8_t y)
{
    if (x > 127 || y > 63) return;
    framebuf[y / 8][x] |= (1 << (y % 8));
    dirty[y / 8] = 1;
}

/* 画一条贯穿整屏宽度的水平线 */
static void oled_hline(uint8_t y)
{
    if (y > 63) return;
    uint8_t page = y / 8;
    uint8_t bit  = 1 << (y % 8);
    for (uint8_t x = 0; x < 128; x++) framebuf[page][x] |= bit;
    dirty[page] = 1;
}

/* 2 倍放大绘制单字符 (10×14 px, 占 2 个 page) */
static void oled_drawchar_big(uint8_t x, uint8_t page, char c)
{
    uint8_t idx;
    if ((unsigned char)c < 32 || (unsigned char)c > 126) idx = 0;
    else idx = (unsigned char)c - 32;

    uint8_t base_y = page * 8;
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t col_data = font5x7[idx][col];
        for (uint8_t bit = 0; bit < 7; bit++) {
            if (col_data & (1 << bit)) {
                uint8_t px = x + col * 2;
                uint8_t py = base_y + bit * 2;
                oled_setpixel(px,     py);
                oled_setpixel(px + 1, py);
                oled_setpixel(px,     py + 1);
                oled_setpixel(px + 1, py + 1);
            }
        }
    }
}

/* 小字体居中绘制 (6px/字符) */
static void oled_drawstr_center(uint8_t page, const char *s)
{
    uint8_t len = 0;
    while (s[len]) len++;
    if (len == 0) return;
    uint8_t x = (128 - len * 6) / 2;
    OLED_DrawString(x, page, s);
}

/* 2 倍放大字体居中绘制 (12px/字符, 占 2 page) */
static void oled_drawstr_big_center(uint8_t page, const char *s)
{
    uint8_t len = 0;
    while (s[len]) len++;
    if (len == 0) return;
    uint8_t x = (128 - len * 12) / 2;
    for (uint8_t i = 0; i < len; i++)
        oled_drawchar_big(x + i * 12, page, s[i]);
}

/* 绘制大号温度数值 + °C 单位 (居中)。
 * ° 不在 5x7 字体表内 (仅 32~126), 此处用像素拼出 2x2 实心小圆替代度数符号。
 * 整体布局: [数值][°][C], ° 紧贴数值右上, C 在 ° 右侧。
 * 占 page 2~3, 垂直位置与大号数值一致。 */
static void oled_drawtemp_big_center(uint8_t page, const char *numstr)
{
    uint8_t numlen = 0;
    while (numstr[numlen]) numlen++;
    /* 总宽 = 数值(numlen*12) + °(6) + C(12) */
    uint8_t total = numlen * 12 + 6 + 12;
    uint8_t x = (128 - total) / 2;
    /* 数值 */
    for (uint8_t i = 0; i < numlen; i++)
        oled_drawchar_big(x + i * 12, page, numstr[i]);
    /* ° 符号: 在数值右上方画 4x4 空心小圆 (放大2倍, 相当于2x2逻辑像素) */
    uint8_t dx = x + numlen * 12;       /* ° 起始 x */
    uint8_t dy = page * 8;              /* ° 顶部 y (与数值顶部对齐) */
    /* 圆环: 4x4, 顶/底行画中间2像素, 左右列画中间2像素 */
    oled_setpixel(dx + 1, dy + 0); oled_setpixel(dx + 2, dy + 0);
    oled_setpixel(dx + 0, dy + 1); oled_setpixel(dx + 3, dy + 1);
    oled_setpixel(dx + 0, dy + 2); oled_setpixel(dx + 3, dy + 2);
    oled_setpixel(dx + 1, dy + 3); oled_setpixel(dx + 2, dy + 3);
    /* C 字符: 紧跟 ° 右侧, 间隔 2px */
    oled_drawchar_big(dx + 6, page, 'C');
}

/* ============================================================
 * 上层显示函数 — 统一布局: 标题(居中) → 分隔线 → 大号数值(居中)
 * → 单位(居中) → 副标题(居中) → 底部分隔线。
 * 每个整段 (Clear+Draw+Flush) 用互斥锁保护。
 * ============================================================ */

/* 主页 — 手表风格: 顶部大号时间 + 语音指令指南。
 * 8 条指令分 4 行双列展示, 配分隔线与底部装饰边框。 */
void OLED_ShowMainPage(void)
{
    if (oled_lock() != 0) return;
    char buf[16];

    OLED_Clear();

    /* --- 顶部: 大号时间 (page 0~1, 2x 字体) --- */
    rtc_time_t tm;
    DS1302_ReadTime(&tm);
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.hour, tm.min);
    oled_drawstr_big_center(0, buf);

    /* --- 分隔线 + 小标题 --- */
    oled_hline(18);
    oled_drawstr_center(2, "- Voice Guide -");

    /* --- 8 条语音指令, 双列布局 ---
     * 左列 x=4, 右列 x=68, 每行间隔 1 个 page
     * 名称缩短以适配双列宽度 (每列 ≤10 字符) */
    OLED_DrawString(4,  3, "1.HR");
    OLED_DrawString(68, 3, "5.Temp");
    OLED_DrawString(4,  4, "2.Steps");
    OLED_DrawString(68, 4, "6.Time");
    OLED_DrawString(4,  5, "3.Reset");
    OLED_DrawString(68, 5, "7.Main");
    OLED_DrawString(4,  6, "4.SpO2");
    OLED_DrawString(68, 6, "8.Next");

    /* --- 底部装饰: 双线边框 --- */
    oled_hline(57);
    oled_hline(59);

    OLED_Flush();
    oled_unlock();
}

void OLED_ShowHeartRate(int hr)
{
    if (oled_lock() != 0) return;
    char buf[12];
    OLED_Clear();
    oled_drawstr_center(0, "== Heart Rate ==");
    oled_hline(8);

    if (hr > 0) {
        snprintf(buf, sizeof(buf), "%d", hr);
        oled_drawstr_big_center(2, buf);
        oled_drawstr_center(4, "bpm");
        oled_drawstr_center(6, "Normal 60-100");
    } else {
        oled_drawstr_big_center(2, "--");
        oled_drawstr_center(4, "bpm");
        oled_drawstr_center(6, "No signal");
    }

    oled_hline(56);
    OLED_Flush();
    oled_unlock();
}

void OLED_ShowSpO2(int spo2)
{
    if (oled_lock() != 0) return;
    char buf[12];
    OLED_Clear();
    oled_drawstr_center(0, "==== SpO2 ====");
    oled_hline(8);

    if (spo2 > 0) {
        snprintf(buf, sizeof(buf), "%d", spo2);
        oled_drawstr_big_center(2, buf);
        oled_drawstr_center(4, "%");
        oled_drawstr_center(6, "Normal 95-100");
    } else {
        oled_drawstr_big_center(2, "--");
        oled_drawstr_center(4, "%");
        oled_drawstr_center(6, "No signal");
    }

    oled_hline(56);
    OLED_Flush();
    oled_unlock();
}

void OLED_ShowSteps(int steps)
{
    if (oled_lock() != 0) return;
    char buf[12];
    OLED_Clear();
    oled_drawstr_center(0, "=== Steps ===");
    oled_hline(8);

    snprintf(buf, sizeof(buf), "%d", steps);
    oled_drawstr_big_center(2, buf);
    oled_drawstr_center(4, "steps");
    oled_drawstr_center(6, "Keep moving!");

    oled_hline(56);
    OLED_Flush();
    oled_unlock();
}

/* 体温页 — 统一封装, 消除 main.c / asr_pro.c / ShowMainPage 三处重复,
 * 且整段加锁避免与 Display 任务的体温页刷新交叉花屏。
 * 温度数值后直接绘制 °C 符号 (°不在字体表内, 用像素拼出)。 */
void OLED_ShowTemperature(void)
{
    if (oled_lock() != 0) return;
    char buf[16];
    int   valid;
    float temp;
    OLED_Clear();
    oled_drawstr_center(0, "== Body Temp ==");
    oled_hline(8);

    Get_Temperature(&valid, &temp);
    if (valid) {
        snprintf(buf, sizeof(buf), "%.1f", (double)temp);
        oled_drawtemp_big_center(2, buf);
        oled_drawstr_center(6, "Normal 36.0-37.3");
    } else {
        oled_drawtemp_big_center(2, "--.-");
        oled_drawstr_center(6, "No signal");
    }

    oled_hline(56);
    OLED_Flush();
    oled_unlock();
}

/* 时间页 — 统一封装, 消除 main.c / asr_pro.c 两处重复。 */
void OLED_ShowTime(void)
{
    if (oled_lock() != 0) return;
    rtc_time_t tm;
    char buf[20];
    DS1302_ReadTime(&tm);
    OLED_Clear();
    oled_drawstr_center(0, "==== Time ====");
    oled_hline(8);

    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.hour, tm.min, tm.sec);
    oled_drawstr_big_center(2, buf);

    snprintf(buf, sizeof(buf), "20%02d/%02d/%02d", tm.year, tm.month, tm.day);
    oled_drawstr_center(4, buf);
    oled_drawstr_center(6, "Date");

    oled_hline(56);
    OLED_Flush();
    oled_unlock();
}

/* 活动状态页 — ASR 指令触发, 统一带锁。 */
void OLED_ShowActivity(activity_t act)
{
    if (oled_lock() != 0) return;
    const char *label;
    const char *hint;
    switch (act) {
    case ACTIVITY_WALKING: label = "Walking"; hint = "Nice pace!";   break;
    case ACTIVITY_RUNNING: label = "Running"; hint = "Great job!";   break;
    case ACTIVITY_SHAKING: label = "Shaking"; hint = "Steady...";    break;
    default:              label = "Resting"; hint = "Stay active";  break;
    }
    OLED_Clear();
    oled_drawstr_center(0, "== Activity ==");
    oled_hline(8);

    oled_drawstr_big_center(2, label);
    oled_drawstr_center(6, hint);

    oled_hline(56);
    OLED_Flush();
    oled_unlock();
}
