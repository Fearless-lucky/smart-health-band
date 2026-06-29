#ifndef __GLOBALS_H
#define __GLOBALS_H

#include <stdint.h>

extern volatile float  g_temperature;
extern volatile int    g_temp_valid;

/* 原子读取 g_temp_valid + g_temperature (需 taskENTER_CRITICAL 内部调用)。
 * 所有跨任务读取体温数据的代码统一使用此函数, 避免 valid 与温度值
 * 不匹配或 volatile 被编译器优化。调用者需 #include "task.h"。 */
void Get_Temperature(int *valid, float *temp);

#endif
