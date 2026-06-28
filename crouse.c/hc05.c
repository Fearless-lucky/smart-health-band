#include "hc05.h"
#include "uart.h"
#include "algorithms.h"
#include "globals.h"
#include "systick.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include "stm32f10x_usart.h"

static char txbuf[128];
static uint32_t last_report_tick = 0;
#define HC05_REPORT_INTERVAL_MS  10000

static int hc05_try_baud(uint32_t baud)//检测并适应波特率
{
    UART1_Init(baud);

    const char *at = "AT\r\n";
    UART1_Send((const uint8_t *)at, 4);

    uint32_t t0 = Systick_GetTick();
    char resp[16];
    int ri = 0;
    while ((Systick_GetTick() - t0) < 500 && ri < (int)sizeof(resp) - 1) {
        if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET) {
            resp[ri++] = (char)USART_ReceiveData(USART1);
        }
    }
    resp[ri] = '\0';
    return (ri >= 2 && strstr(resp, "OK") != NULL) ? 0 : -1;
}

void HC05_Init(uint32_t baud)
{
    if (hc05_try_baud(baud) == 0) return;

    static const uint32_t fallbacks[] = {38400, 115200, 57600, 19200};
    for (int i = 0; i < 4; i++) {
        if (hc05_try_baud(fallbacks[i]) == 0) {
            char cmd[32];
            int n = snprintf(cmd, sizeof(cmd), "AT+UART=%lu,0,0\r\n", (unsigned long)baud);
            UART1_Send((const uint8_t *)cmd, (uint16_t)n);
            Delay_ms(200);
            UART1_Init(baud);
            return;
        }
    }
    UART1_Init(baud);
}

void HC05_Process(void)
{
    uint32_t now = Systick_GetTick();
    if ((now - last_report_tick) >= HC05_REPORT_INTERVAL_MS) {
        last_report_tick = now;
        int hr    = Get_HeartRate();
        int spo2  = Get_SpO2();
        int steps = Get_StepCount();
        int   valid;
        float temp;
        Get_Temperature(&valid, &temp);
        int n = snprintf(txbuf, sizeof(txbuf),
            "\r\n==== 健康数据 ====\r\n"
            "心率: %d bpm\r\n"
            "血氧: %d %%\r\n"
            "步数: %d 步\r\n"
            "体温: %.2f C\r\n"
            "==================\r\n",
            hr, spo2, steps, (double)(valid ? temp : 0.0f));
        if (n > 0) UART1_Send((uint8_t *)txbuf, (uint16_t)n);
    }
}
