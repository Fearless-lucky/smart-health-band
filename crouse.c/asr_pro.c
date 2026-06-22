#include "asr_pro.h"
#include "uart_hal.h"
#include "stm32f10x_usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "algorithms.h"
#include "oled_ssd1306.h"
#include "globals.h"   /* g_page_advance */

void ASR_Init(uint32_t baud)
{
    UART2_Init(baud);
}

void ASR_ProcessUART(void)
{
    static uint32_t last_next = 0;

    /* 清除 Overrun Error，否则 UART 会停止接收 */
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(USART2);
    }

    while (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET) {
        uint8_t c = (uint8_t)USART_ReceiveData(USART2);

        switch (c) {
        case ASR_CMD_HR:
            OLED_ShowHeartRate(Get_HeartRate());
            break;
        case ASR_CMD_STEPS:
            OLED_ShowSteps(Get_StepCount());
            break;
        case ASR_CMD_STEPRST:
            Reset_StepCount();
            OLED_ShowSteps(Get_StepCount());
            break;
        case ASR_CMD_SPO2:
            OLED_ShowSpO2(Get_SpO2());
            break;
        case ASR_CMD_TEMP:
            OLED_ShowTemperature();
            break;
        case ASR_CMD_TIME:
            OLED_ShowTime();   /* 时间页: 大号时间显示 */
            break;
        case ASR_CMD_MAIN:
            OLED_ShowMainPage();
            break;
        case ASR_CMD_NEXT: {
            uint32_t now = xTaskGetTickCount();
            if (now - last_next > pdMS_TO_TICKS(2000)) {
                last_next = now;
                g_page_advance = 1;
            }
            break;
        }
        case ASR_CMD_ACTIVITY:
            OLED_ShowActivity(Get_ActivityState());
            break;
        default:
            break;
        }
    }
}
