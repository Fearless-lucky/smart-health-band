#ifndef __MPU6050_H
#define __MPU6050_H

#include <stdint.h>

/* 通信: I2C1, PB8=SCL, PB9=SDA (Remap, 与 MAX30102 共享), 8-bit write 地址 0xD0 (7-bit=0x68) */

int  MPU6050_Init(void);
/* 返回: 0=成功, -1=I2C 通信失败 */

int  MPU6050_ReadData(int16_t *ax, int16_t *ay, int16_t *az, float *chip_temp);
/* 一次 I2C 批量读取加速度(6字节) + 芯片温度(2字节)
 * ax/ay/az:   三轴加速度原始值 (传 NULL 跳过)
 * chip_temp:  芯片温度 °C         (传 NULL 跳过)
 * 返回: 0=成功, -1=I2C 错误 */

#endif
