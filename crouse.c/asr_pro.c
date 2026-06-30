#include "asr_pro.h"
#include "uart.h"
#include "stm32f10x_usart.h"
#include "algorithms.h"
#include "oled_ssd1306.h"

void ASR_Init(uint32_t baud)
{
    UART2_Init(baud);
}

void ASR_ProcessUART(void)
{
    /* 清除 Overrun Error，否则 UART 会停止接收 */
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(USART2);
    }

    while (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET) 
    {
        uint8_t c = (uint8_t)USART_ReceiveData(USART2);

        switch (c) {
        case ASR_CMD_HR:
            OLED_ShowHeartRate(hr);
            break;
        case ASR_CMD_STEPS:
            OLED_ShowSteps(steps);
            break;
        case ASR_CMD_STEPRST:
            Reset_StepCount();
            OLED_ShowSteps(steps);
            break;
        case ASR_CMD_SPO2:
            OLED_ShowSpO2(spo2);
            break;
        case ASR_CMD_TEMP:
            OLED_ShowTemperature();
            break;
        case ASR_CMD_ACTIVITY:
            OLED_ShowActivity(activity_state);
            break;
        default:
            break;
        }
    }
}
