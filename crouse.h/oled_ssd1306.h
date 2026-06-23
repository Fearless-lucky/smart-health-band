#ifndef __OLED_SSD1306_H
#define __OLED_SSD1306_H

#include <stdint.h>
#include "algorithms.h"   /* activity_t, 用于 OLED_ShowActivity */

void OLED_Init(void);
void OLED_ShowMainPage(void);       /* 主页: 大号时间 + 6条语音指令指南 */
void OLED_ShowHeartRate(int hr);
void OLED_ShowSpO2(int spo2);
void OLED_ShowSteps(int steps);
void OLED_ShowTemperature(void);     /* 体温页 (含有效判断) */
void OLED_ShowTime(void);            /* 时间页 (已并入主页, 保留供 ASR 指令兼容) */
void OLED_ShowActivity(activity_t act);  /* 活动状态页 */

/* 底层接口 (Show* 内部使用, 外部一般不直接调用。
 * 直接组合 Clear+DrawString+Flush 时不会自动加 framebuf 互斥锁,
 * 跨任务访问 OLED 请优先用上面的 Show* 封装函数。) */
void OLED_Clear(void);
void OLED_Flush(void);
void OLED_DrawChar(uint8_t x, uint8_t page, char c);
void OLED_DrawString(uint8_t x, uint8_t page, const char *s);

#endif
