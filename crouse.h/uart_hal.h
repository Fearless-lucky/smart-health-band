#ifndef __UART_HAL_H
#define __UART_HAL_H

#include <stdint.h>

void UART1_Init(uint32_t baud);
int  UART1_Send(const uint8_t *data, uint16_t len);

void UART2_Init(uint32_t baud);

#endif
