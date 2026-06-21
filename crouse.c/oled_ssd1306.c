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

    /* 创建 framebuf 互斥锁 (调度器尚未启动, 创建安全) */
    if (g_oled_mutex == NULL)
        g_oled_mutex = xSemaphoreCreateMutex();
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
 * 上层显示函数 — 每个整段 (Clear+Draw+Flush) 用互斥锁保护,
 * 防止 Display 任务与 Voice 任务交叉刷新导致花屏。
 * 取不到锁直接返回 (放弃本次刷新), 调用方周期性重试即可。
 * ============================================================ */

void OLED_ShowMainPage(void)
{
    if (oled_lock() != 0) return;
    char buf[20];

    OLED_Clear();
    snprintf(buf, sizeof(buf), "HR:%d", Get_HeartRate());
    OLED_DrawString(0, 0, buf);

    snprintf(buf, sizeof(buf), "SpO2:%d%%", Get_SpO2());
    OLED_DrawString(0, 1, buf);

    snprintf(buf, sizeof(buf), "Steps:%d", Get_StepCount());
    OLED_DrawString(0, 2, buf);

    int   valid;
    float temp;
    Get_Temperature(&valid, &temp);
    if (valid) {
        snprintf(buf, sizeof(buf), "T:%.1fC", (double)temp);
    } else {
        snprintf(buf, sizeof(buf), "T:--.-C");
    }
    OLED_DrawString(0, 3, buf);

    rtc_time_t tm;
    DS1302_ReadTime(&tm);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.hour, tm.min, tm.sec);
    OLED_DrawString(0, 5, buf);

    OLED_Flush();
    oled_unlock();
}

void OLED_ShowHeartRate(int hr)
{
    if (oled_lock() != 0) return;
    char buf[12];
    OLED_Clear();
    OLED_DrawString(0, 1, "HeartRate:");
    snprintf(buf, sizeof(buf), "%d bpm", hr);
    OLED_DrawString(0, 3, buf);
    OLED_Flush();
    oled_unlock();
}

void OLED_ShowSpO2(int spo2)
{
    if (oled_lock() != 0) return;
    char buf[12];
    OLED_Clear();
    OLED_DrawString(0, 1, "SpO2:");
    snprintf(buf, sizeof(buf), "%d %%", spo2);
    OLED_DrawString(0, 3, buf);
    OLED_Flush();
    oled_unlock();
}

void OLED_ShowSteps(int steps)
{
    if (oled_lock() != 0) return;
    char buf[12];
    OLED_Clear();
    OLED_DrawString(0, 1, "Steps:");
    snprintf(buf, sizeof(buf), "%d", steps);
    OLED_DrawString(0, 3, buf);
    OLED_Flush();
    oled_unlock();
}

/* 体温页 — 统一封装, 消除 main.c / asr_pro.c / ShowMainPage 三处重复,
 * 且整段加锁避免与 Display 任务的体温页刷新交叉花屏。 */
void OLED_ShowTemperature(void)
{
    if (oled_lock() != 0) return;
    char buf[16];
    int   valid;
    float temp;
    OLED_Clear();
    OLED_DrawString(0, 1, "BodyTemp:");
    Get_Temperature(&valid, &temp);
    if (valid) {
        snprintf(buf, sizeof(buf), "%.1f C", (double)temp);
    } else {
        snprintf(buf, sizeof(buf), "--.- C");
    }
    OLED_DrawString(0, 3, buf);
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
    OLED_DrawString(0, 0, "-- Time --");
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.hour, tm.min, tm.sec);
    OLED_DrawString(0, 2, buf);
    snprintf(buf, sizeof(buf), "%02d/%02d/20%02d", tm.day, tm.month, tm.year);
    OLED_DrawString(0, 4, buf);
    OLED_Flush();
    oled_unlock();
}

/* 活动状态页 — ASR 指令触发, 统一带锁。 */
void OLED_ShowActivity(activity_t act)
{
    if (oled_lock() != 0) return;
    const char *label;
    switch (act) {
    case ACTIVITY_WALKING: label = "Walking"; break;
    case ACTIVITY_RUNNING: label = "Running"; break;
    case ACTIVITY_SHAKING: label = "Shaking"; break;
    default:              label = "Resting";  break;
    }
    OLED_Clear();
    OLED_DrawString(0, 2, label);
    OLED_Flush();
    oled_unlock();
}
