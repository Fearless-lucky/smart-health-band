#ifndef __MAX30102_H
#define __MAX30102_H

#include <stdint.h>

/* 通信: I2C1, PB8=SCL, PB9=SDA (Remap), 8-bit write 地址 0xAE (7-bit=0x57) */

int  MAX30102_Init(void);
/* 返回: 0=成功, -1=I2C 通信失败 */

int  MAX30102_ReadData(int32_t *ir, int32_t *red, uint16_t *count);
/* 读取 FIFO 全部可用样本，内部自动调用 PPG_ProcessSamples()
 * ir[]   输出: IR 原始数据缓冲区 (传 NULL 跳过)
 * red[]  输出: Red 原始数据缓冲区 (传 NULL 跳过)
 * count  输入: ir/red 缓冲区容量 (允许为 0; 传 NULL 时仅为算法层喂入)
 *        输出: 实际写入的样本数
 * 返回: 0=成功, -1=无新数据, -2=I2C 错误 */

#endif
