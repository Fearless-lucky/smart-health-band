#include "oled_ssd1306.h"
#include "i2c_hal.h"
#include "systick.h"
#include "algorithms.h"
#include "ds1302.h"
#include "globals.h"
#include "font5x7.h"
#include <stdio.h>
#include <string.h>

#define SSD1306_ADDR (0x3C << 1)
#define CMD 0x00
#define DAT 0x40

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

void OLED_ShowMainPage(void)
{
    OLED_Clear();
    char buf[20];

    snprintf(buf, sizeof(buf), "HR:%d", Get_HeartRate());
    OLED_DrawString(0, 0, buf);

    snprintf(buf, sizeof(buf), "SpO2:%d%%", Get_SpO2());
    OLED_DrawString(0, 1, buf);

    snprintf(buf, sizeof(buf), "Steps:%d", Get_StepCount());
    OLED_DrawString(0, 2, buf);

    if (g_temp_valid) {
        snprintf(buf, sizeof(buf), "T:%.1fC", (double)g_temperature);
    } else {
        snprintf(buf, sizeof(buf), "T:--.-C");
    }
    OLED_DrawString(0, 3, buf);

    rtc_time_t tm;
    DS1302_ReadTime(&tm);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.hour, tm.min, tm.sec);
    OLED_DrawString(0, 5, buf);

    OLED_Flush();
}

void OLED_ShowHeartRate(int hr)
{
    char buf[12];
    OLED_Clear();
    OLED_DrawString(0, 1, "HeartRate:");
    snprintf(buf, sizeof(buf), "%d bpm", hr);
    OLED_DrawString(0, 3, buf);
    OLED_Flush();
}

void OLED_ShowSpO2(int spo2)
{
    char buf[12];
    OLED_Clear();
    OLED_DrawString(0, 1, "SpO2:");
    snprintf(buf, sizeof(buf), "%d %%", spo2);
    OLED_DrawString(0, 3, buf);
    OLED_Flush();
}

void OLED_ShowSteps(int steps)
{
    char buf[12];
    OLED_Clear();
    OLED_DrawString(0, 1, "Steps:");
    snprintf(buf, sizeof(buf), "%d", steps);
    OLED_DrawString(0, 3, buf);
    OLED_Flush();
}
