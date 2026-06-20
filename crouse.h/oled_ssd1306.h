#ifndef __OLED_SSD1306_H
#define __OLED_SSD1306_H

#include <stdint.h>

void OLED_Init(void);
void OLED_ShowMainPage(void);
void OLED_ShowHeartRate(int hr);
void OLED_ShowSpO2(int spo2);
void OLED_ShowSteps(int steps);
void OLED_Clear(void);
void OLED_Flush(void);
void OLED_DrawChar(uint8_t x, uint8_t page, char c);
void OLED_DrawString(uint8_t x, uint8_t page, const char *s);

#endif
