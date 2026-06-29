#ifndef __DS1302_H
#define __DS1302_H

#include <stdint.h>
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

typedef struct {
    uint8_t sec, min, hour, day, month, year;
} rtc_time_t;

int  DS1302_Init(void);   /* 返回 0=正常, -1=通信失败 (0xFF) */
void DS1302_ReadTime(rtc_time_t *t);

/* 通信: 三线 SPI-like (CE + DAT + CLK)
 * 引脚: PB1=CLK, PB14=DAT, PB13=CE
 *       模块已内置 32.768kHz 晶振和上拉电阻 */

/* ---- GPIO 引脚 ---- */
#define DS1302_CLK_PORT            GPIOB
#define DS1302_CLK_PIN             GPIO_Pin_1

#define DS1302_DAT_PORT            GPIOB
#define DS1302_DAT_PIN             GPIO_Pin_14

#define DS1302_CE_PORT             GPIOB
#define DS1302_CE_PIN              GPIO_Pin_13

#endif
