#include "asr_pro.h"
#include "uart_hal.h"
#include "stm32f10x_usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "algorithms.h"
#include "oled_ssd1306.h"
#include "ds1302.h"
#include "globals.h"
#include <stdio.h>

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
        case ASR_CMD_TEMP: {
            OLED_Clear();
            char buf[16];
            if (g_temp_valid) {
                snprintf(buf, sizeof(buf), "T:%.1fC", (double)g_temperature);
            } else {
                snprintf(buf, sizeof(buf), "T:--.-C");
            }
            OLED_DrawString(0, 2, buf);
            OLED_Flush();
            break;
        }
        case ASR_CMD_TIME: {
            OLED_Clear();
            rtc_time_t tm;
            DS1302_ReadTime(&tm);
            char buf[16];
            OLED_DrawString(0, 0, "-- Time --");
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.hour, tm.min, tm.sec);
            OLED_DrawString(0, 2, buf);
            snprintf(buf, sizeof(buf), "%02d/%02d/20%02d", tm.day, tm.month, tm.year);
            OLED_DrawString(0, 4, buf);
            OLED_Flush();
            break;
        }
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
        case ASR_CMD_ACTIVITY: {
            OLED_Clear();
            const char *label;
            switch (Get_ActivityState()) {
            case ACTIVITY_WALKING: label = "Walking"; break;
            case ACTIVITY_RUNNING: label = "Running"; break;
            case ACTIVITY_SHAKING: label = "Shaking"; break;
            default:              label = "Resting";  break;
            }
            OLED_DrawString(0, 2, label);
            OLED_Flush();
            break;
        }
        default:
            break;
        }
    }
}
