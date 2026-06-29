#ifndef __MAX30102_H
#define __MAX30102_H

#include <stdint.h>

/* 通信: I2C1, PB8=SCL, PB9=SDA (Remap), 8-bit write 地址 0xAE (7-bit=0x57) */

int  MAX30102_Init(void);
/* 返回: 0=成功, -1=I2C 通信失败 */

int  MAX30102_ReadData(void);
/* 读取 FIFO 全部可用样本，内部自动调用 PPG_ProcessSamples()
 * 返回: 0=成功, -1=无新数据, -2=I2C 错误 */

/* ---- I2C 地址 (8-bit write) ---- */
#define MAX30102_I2C_ADDR          (0x57 << 1)  /* 0xAE */

/* ---- 初始化寄存器配置 ---- */
#define MAX30102_SPO2_CONFIG       0x47   /* ADC=4096, SR=100Hz, PW=411us */
#define MAX30102_LED1_CURRENT      0x50   /* IR LED 电流 ~16mA */
#define MAX30102_LED2_CURRENT      0x50   /* Red LED 电流 ~16mA */
#define MAX30102_FIFO_CONFIG       0x10   /* 滚动覆盖, 不平均 */

#endif
