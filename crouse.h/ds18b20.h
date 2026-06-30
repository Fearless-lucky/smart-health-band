#ifndef __DS18B20_H
#define __DS18B20_H

#include <stdint.h>
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

/* 通信: 1-Wire (单总线), 需 4.7kΩ 上拉到 3.3V
 * 引脚: PA1 = DQ
 * 用法: 先 StartConversion(), 等待 ≥750ms, 再 ReadData() */

int  DS18B20_Init(void);
/* 返回: 0=成功, -1=总线无设备 (自检 presence 脉冲失败) */

void DS18B20_StartConversion(void);
/* 发送 Skip ROM + Convert T (0xCC 0x44), 不阻塞, 约 750ms 后转换完成 */

int  DS18B20_ReadData(float *temperature);
/* 发送 Skip ROM + Read Scratchpad, 读 9 字节并 CRC-8 校验后返回温度 °C
 * 返回: 0=成功, -1=CRC 校验失败, -2=总线无响应或入参 NULL */

/* ---- GPIO 引脚 ---- */
#define DS18B20_PORT               GPIOA
#define DS18B20_PIN                GPIO_Pin_1

#endif
