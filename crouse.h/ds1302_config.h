#ifndef __DS1302_CONFIG_H
#define __DS1302_CONFIG_H

#include "stm32f10x.h"

/* ============================================================
 * DS1302 硬件配置 — RTC 时钟模块
 * 通信: 三线 SPI-like (CE + DAT + CLK)
 * 引脚: PB1=CLK, PB14=DAT, PB13=CE
 *       模块已内置 32.768kHz 晶振和上拉电阻
 * ============================================================ */

/* ---- GPIO 引脚 ---- */
#define DS1302_CLK_PORT            GPIOB
#define DS1302_CLK_PIN             GPIO_Pin_1

#define DS1302_DAT_PORT            GPIOB
#define DS1302_DAT_PIN             GPIO_Pin_14

#define DS1302_CE_PORT             GPIOB
#define DS1302_CE_PIN              GPIO_Pin_13

#endif
