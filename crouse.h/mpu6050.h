#ifndef __MPU6050_H
#define __MPU6050_H

#include <stdint.h>

/* 通信: I2C1, PB8=SCL, PB9=SDA (Remap, 与 MAX30102 共享), 8-bit write 地址 0xD0 (7-bit=0x68) */

int  MPU6050_Init(void);
/* 返回: 0=成功, -1=I2C 通信失败 */

int  MPU6050_ReadData(int16_t *ax_addr, int16_t *ay_addr, int16_t *az_addr, float *temp_addr);
/* 一次 I2C 批量读取加速度(6字节) + 芯片温度(2字节)
 * ax/ay/az:   三轴加速度原始值 (传 NULL 跳过)
 * chip_temp:  芯片温度 °C         (传 NULL 跳过)
 * 返回: 0=成功, -1=I2C 错误 */

/* ---- I2C 地址 (8-bit write) ---- */
#define MPU6050_I2C_ADDR           (0x68 << 1)  /* 0xD0 */

/* ---- 加速度量程 ---- */
#define MPU6050_ACCEL_RANGE        0x00   /* ±2g */

/* ---- 芯片温度计算 ---- */
#define MPU6050_TEMP_OFFSET        36.53f /* °C 偏移 */
#define MPU6050_TEMP_SCALE         340.0f /* LSB/°C */
#define MPU6050_TEMP_FALLBACK      25.0f  /* 读取失败默认值 */

#endif
