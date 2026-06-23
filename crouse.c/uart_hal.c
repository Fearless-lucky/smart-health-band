#include "uart_hal.h"
#include "stm32f10x.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "systick.h"

static void uart_init(USART_TypeDef *usart, uint32_t baud)
{
    USART_InitTypeDef cfg;
    USART_StructInit(&cfg);
    cfg.USART_BaudRate            = baud;
    cfg.USART_WordLength          = USART_WordLength_8b;
    cfg.USART_StopBits            = USART_StopBits_1;
    cfg.USART_Parity              = USART_Parity_No;
    cfg.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    cfg.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(usart, &cfg);
    USART_Cmd(usart, ENABLE);
}

static int uart_send(USART_TypeDef *usart, const uint8_t *data, uint16_t len)
{
    /* 每字节独立超时 (200ms/字节), 避免长帧发送过程中前几字节
     * 耗尽总预算导致后续字节截帧。 */
    for (uint16_t i = 0; i < len; i++) {
        uint32_t t0 = Systick_GetTick();
        USART_SendData(usart, data[i]);
        while (USART_GetFlagStatus(usart, USART_FLAG_TXE) == RESET) {
            if ((Systick_GetTick() - t0) > 200) return -1;
        }
    }
    {
        uint32_t t0 = Systick_GetTick();
        while (USART_GetFlagStatus(usart, USART_FLAG_TC) == RESET) {
            if ((Systick_GetTick() - t0) > 200) return -1;
        }
    }
    return 0;
}

void UART1_Init(uint32_t baud)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin   = GPIO_Pin_10;
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);
    uart_init(USART1, baud);
}

int UART1_Send(const uint8_t *data, uint16_t len)  { return uart_send(USART1, data, len); }

void UART2_Init(uint32_t baud)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = GPIO_Pin_2;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin   = GPIO_Pin_3;
    /* 上拉输入: ASR 模块未上电/未接线时 RX 不会悬空, 避免噪声误触发语音指令。
     * 与 UART1 RX (PA10) 保持一致。 */
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);
    uart_init(USART2, baud);
}

