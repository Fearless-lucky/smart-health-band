#ifndef __SYSTICK_H
#define __SYSTICK_H

#include <stdint.h>

void     Systick_Init(void);
void     Delay_ms(uint32_t ms);
void     Delay_us(uint32_t us);
uint32_t Systick_GetTick(void);

#endif
