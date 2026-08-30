#include "oled_ssd1306.h"
#include "i2c.h"
#include "errors.h"
#include "systick.h"
#include "algorithms.h"
#include "ds1302.h"
#include "globals.h"
#include "font.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

#define SSD1306_ADDR (0x3C << 1)
#define CMD 0x00
#define DAT 0x40

/* framebuf 并发保护: vTaskDisplay 与 vTaskVoice 同优先级 (prio 2), 时间片
 * 抢占会打断 Clear+Draw+Flush 序列导致花屏。互斥锁把每个 Show* 的整段
 * (Clear→Draw→Flush) 包成原子操作; 底层 Clear/DrawChar/DrawString/Flush
 * 不加锁, 由上层 Show* 保证序列完整性 (避免递归加锁)。 */
static SemaphoreHandle_t g_oled_mutex = NULL;

static uint8_t framebuf[8][128];
static uint8_t dirty[8];

static void write_cmd(uint8_t cmd)
{
    I2C2_WriteReg(SSD1306_ADDR, CMD, &cmd, 1);
}

/* 取/还 framebuf 互斥锁。Show* 函数入口 take、出口 give。
 * 永远不阻塞超过 100ms (Display/Voice 任务 50/100ms 周期,
 * 一帧 I2C2 刷新 ~30ms, 取不到锁说明另一任务卡在 OLED, 放弃本次刷新)。 */
static int oled_lock(void)
{
    if (!g_oled_mutex) return ERR_IO;
    return (xSemaphoreTake(g_oled_mutex, pdMS_TO_TICKS(100)) == pdTRUE) ? 0 : ERR_TIMEOUT;
}

static void oled_unlock(void)
{
    if (g_oled_mutex) xSemaphoreGive(g_oled_mutex);
}

void OLED_Init(void)
{
    Delay_ms(50);                              
    write_cmd(0xAE);                            // 关显示 (配置完成再开)
    write_cmd(0x20); write_cmd(0x00);           // 寻址模式: 水平
    write_cmd(0xB0);                            // 页起始地址 = 0
    write_cmd(0xC8);                            // COM 从下到上 (画面不颠倒)
    write_cmd(0x00);                            // 低列起始 = 0
    write_cmd(0x10);                            // 高列起始 = 0
    write_cmd(0x40);                            // 显示起始行 = 0
    write_cmd(0x81); write_cmd(0x7F);           // 对比度 = 0x7F (中等亮度)
    write_cmd(0xA1);                            // 段重映射 (左右不镜像)
    write_cmd(0xA6);                            // 正常显示 (非反色)
    write_cmd(0xA8); write_cmd(0x3F);           // 复用比 = 64 (128×64)
    write_cmd(0xA4);                            // 全局显示 (非休眠)
    write_cmd(0xD3); write_cmd(0x00);           // 显示偏移 = 0
    write_cmd(0xD5); write_cmd(0x80);           // 时钟分频/振荡器频率
    write_cmd(0xD9); write_cmd(0xF1);           // 预充电周期
    write_cmd(0xDA); write_cmd(0x12);           // COM 引脚配置 (64行)
    write_cmd(0xDB); write_cmd(0x40);           // VCOMH 电压
    write_cmd(0x8D); write_cmd(0x14);           // 开电荷泵 (3.3V必须开)
    write_cmd(0xAF);                            // 开显示
    OLED_Clear();                               // 清屏

    /* 创建 framebuf 互斥锁 (调度器尚未启动, 创建安全)。
     * 堆耗尽会返回 NULL → 所有 Show* 静默不显示, 严重到必须用断言捕获。 */
    if (g_oled_mutex == NULL) {
        g_oled_mutex = xSemaphoreCreateMutex();
        configASSERT(g_oled_mutex);
    }
}

void OLED_Clear(void)
{
    memset(framebuf, 0x00, sizeof(framebuf));//像素置0
    memset(dirty, 1, sizeof(dirty));//标记全脏
}

void OLED_Flush(void)
{
    for (uint8_t page = 0; page < 8; page++) {
        if (!dirty[page]) continue;
        write_cmd(0xB0 + page);
        write_cmd(0x00);
        write_cmd(0x10);
        if (I2C2_WriteReg(SSD1306_ADDR, DAT, framebuf[page], 128) == 0)
            dirty[page] = 0;   /* I2C 成功才清脏, 失败保留下次重传 */
    }
}









//具体显示函数
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

/* 设置单个像素 */
static void oled_setpixel(uint8_t x, uint8_t y)
{
    if (x > 127 || y > 63) return;
    framebuf[y / 8][x] |= (1 << (y % 8));
    dirty[y / 8] = 1;
}

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
    uint8_t len = (uint8_t)strlen(s);
    if (len == 0) return;
    uint8_t x = (128 - len * 6) / 2;
    OLED_DrawString(x, page, s);
}

/* --- 12x12 中文字体绘制 (用于主页紧凑布局) ---
 * 12px 高 = 1.5 个 page, 跨越 page 边界, 通过 oled_setpixel 逐像素写入。
 * 每字 24 字节: 12 行 × 2 字节, 每行前 8 位在 byte_hi, 后 4 位在 byte_lo 高位。 */
static void oled_draw_cn12(uint8_t x, uint8_t y, uint8_t idx)
{
    if (idx >= CN_COUNT) return;
    const uint8_t *p = font_cn_12[idx];
    for (uint8_t row = 0; row < 12; row++) {
        uint8_t hi = p[row * 2];
        uint8_t lo = p[row * 2 + 1];
        for (uint8_t col = 0; col < 8; col++)
            if (hi & (0x80 >> col)) oled_setpixel(x + col, y + row);
        for (uint8_t col = 0; col < 4; col++)
            if (lo & (0x80 >> col)) oled_setpixel(x + 8 + col, y + row);
    }
}

/* 绘制 12x12 中文指令串 (索引数组, 紧密排列, 每字 12px)。
 * 4 字 = 48px。起始坐标用绝对 y (非 page)。 */
static void oled_draw_cn12_str(uint8_t x, uint8_t y, const uint8_t *idxs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
        oled_draw_cn12(x + i * 12, y, idxs[i]);
}

/* 5x7 字符在任意 y 坐标绘制 (非 page 对齐)。
 * 用于主页序号与 12x12 中文同 y 对齐。 */
static void oled_drawchar_at(uint8_t x, uint8_t y, char c)
{
    uint8_t idx;
    if ((unsigned char)c < 32 || (unsigned char)c > 126) idx = 0;
    else idx = (unsigned char)c - 32;
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t col_data = font5x7[idx][col];
        for (uint8_t bit = 0; bit < 7; bit++)
            if (col_data & (1 << bit)) oled_setpixel(x + col, y + bit);
    }
}

/* 5x7 字符串在任意 y 坐标绘制 (非 page 对齐) */
static void oled_drawstr_at(uint8_t x, uint8_t y, const char *s)
{
    while (*s) {
        oled_drawchar_at(x, y, *s);
        x += 6;
        s++;
    }
}

/* 5x7 字符串在任意 y 坐标居中绘制 */
static void oled_drawstr_at_center(uint8_t y, const char *s)
{
    uint8_t len = (uint8_t)strlen(s);
    if (len == 0) return;
    uint8_t x = (128 - len * 6) / 2;
    oled_drawstr_at(x, y, s);
}

/* 2 倍放大字体居中绘制 (12px/字符, 占 2 page) */
static void oled_drawstr_big_center(uint8_t page, const char *s)
{
    uint8_t len = (uint8_t)strlen(s);
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
    uint8_t numlen = (uint8_t)strlen(numstr);
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

/* 上层显示函数 — 统一布局: 标题(居中) → 分隔线 → 大号数值(居中)
 * → 单位(居中) → 副标题(居中) → 底部分隔线。
 * 每个整段 (Clear+Draw+Flush) 用互斥锁保护。 */

/* 主页 — 手表风格: 顶部大号时间 + 小号日期 + 6条语音指令(中文,对齐)。
 *
 * 布局 (128×64):
 *   y=0~13:  ◆  12:30  ◆      大号时间(2x)居中, 两侧菱形装饰
 *   y=16~22: 2024/01/15        小号日期(5x7, 居中)
 *   y=24:    ──────────────   分隔线
 *   y=26~37: 1.查看心率  2.查看步数   (序号+中文同y对齐)
 *   y=38~49: 3.归零步数  4.查看血氧
 *   y=50~61: 5.查看体温  6.查看状态
 *   y=63:    ──────────────   底部分隔线
 *
 * 序号用 oled_drawstr_at (任意y), 中文用 oled_draw_cn12_str (任意y),
 * 两者起始 y 完全一致, 解决对齐问题。
 */






 //具体页面显示
void OLED_ShowMainPage(void)
{
    if (oled_lock() != 0) return;
    char buf[20];

    OLED_Clear();

    /* --- 顶部: 大号时间 + 两侧菱形装饰 --- */
    rtc_time_t tm;
    DS1302_ReadTime(&tm);
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.hour, tm.min);
    oled_drawstr_big_center(0, buf);
    /* 左菱形◆ (x=18~24, y=3~10) */
    oled_setpixel(21, 3); oled_setpixel(20, 4); oled_setpixel(22, 4);
    oled_setpixel(19, 5); oled_setpixel(23, 5);
    oled_setpixel(20, 6); oled_setpixel(22, 6); oled_setpixel(21, 7);
    /* 右菱形◆ (x=104~110, y=3~10) */
    oled_setpixel(107, 3); oled_setpixel(106, 4); oled_setpixel(108, 4);
    oled_setpixel(105, 5); oled_setpixel(109, 5);
    oled_setpixel(106, 6); oled_setpixel(108, 6); oled_setpixel(107, 7);

    /* --- 小号日期 (5x7, 居中, y=16) --- */
    snprintf(buf, sizeof(buf), "20%02d/%02d/%02d", tm.year, tm.month, tm.day);
    oled_drawstr_at_center(16, buf);

    /* --- 分隔线 --- */
    oled_hline(24);

    /* --- 6 条语音指令 (12x12 中文, 双列, 序号同y对齐) --- */
    /* 行1 y=26: 查看心率 | 查看步数 */
    {static const uint8_t c1[] = {CN_CHA,CN_KAN,CN_XIN,CN_LV};
     static const uint8_t c2[] = {CN_CHA,CN_KAN,CN_BU,CN_SHU};
     oled_drawstr_at(2,  26, "1.");  oled_draw_cn12_str(14, 26, c1, 4);
     oled_drawstr_at(66, 26, "2.");  oled_draw_cn12_str(78, 26, c2, 4);}
    /* 行2 y=38: 归零步数 | 查看血氧 */
    {static const uint8_t c3[] = {CN_GUI,CN_LING,CN_BU,CN_SHU};
     static const uint8_t c4[] = {CN_CHA,CN_KAN,CN_XUE,CN_YANG};
     oled_drawstr_at(2,  38, "3.");  oled_draw_cn12_str(14, 38, c3, 4);
     oled_drawstr_at(66, 38, "4.");  oled_draw_cn12_str(78, 38, c4, 4);}
    /* 行3 y=50: 查看体温 | 查看状态 */
    {static const uint8_t c5[] = {CN_CHA,CN_KAN,CN_TI,CN_WEN};
     static const uint8_t c6[] = {CN_CHA,CN_KAN,CN_ZHUANG,CN_TAI};
     oled_drawstr_at(2,  50, "5.");  oled_draw_cn12_str(14, 50, c5, 4);
     oled_drawstr_at(66, 50, "6.");  oled_draw_cn12_str(78, 50, c6, 4);}

    /* --- 底部分隔线 --- */
    oled_hline(63);

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

/* 体温页 — 温度数值后直接绘制 °C 符号 (°不在字体表内, 用像素拼出)。 */
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

/* 活动状态页 — ASR 指令触发。 */
void OLED_ShowActivity(activity_t act)
{
    if (oled_lock() != 0) return;
    const char *label;
    const char *hint;
    switch (act) {
    case ACTIVITY_WALKING: label = "Walking"; hint = "Nice pace!";   break;
    case ACTIVITY_RUNNING: label = "Running"; hint = "Great job!";   break;
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
