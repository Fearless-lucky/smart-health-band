#ifndef __DS18B20_CONFIG_H
#define __DS18B20_CONFIG_H

#include "stm32f10x.h"

/* ============================================================
 * DS18B20 硬件配置 — 1-Wire 数字温度传感器
 * 通信: 1-Wire (单总线), 需 4.7kΩ 上拉到 3.3V
 * 引脚: PA1 = DQ
 * ============================================================ */

/* ---- GPIO 引脚 ---- */
#define DS18B20_PORT               GPIOA
#define DS18B20_PIN                GPIO_Pin_1

/* ============================================================
 * 温度补偿参数 — algorithms.c 使用
 * ============================================================ */
#define TEMP_BASE_OFFSET           2.5f   /* 基础温差 skin→core °C */
#define TEMP_AMBIENT_REF           25.0f  /* 参考环境温度 °C */
#define TEMP_AMBIENT_COEFF         0.05f  /* 环境温度修正系数 */
#define TEMP_OFFSET_MIN            1.0f   /* 最小补偿量 °C */
#define TEMP_OFFSET_MAX            3.5f   /* 最大补偿量 °C */
#define TEMP_CRC_INVALID           -999.0f /* CRC 校验失败返回值 */

#endif
