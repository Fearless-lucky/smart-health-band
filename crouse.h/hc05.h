#ifndef __HC05_H
#define __HC05_H

#include <stdint.h>

void HC05_Init(uint32_t baud);
void HC05_Process(void); // 处理透传/上报逻辑

#endif
