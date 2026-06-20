#include "hc05.h"
#include "uart_hal.h"
#include "algorithms.h"
#include "max30102_config.h"
#include "ds1302.h"
#include "globals.h"
#include "systick.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include "stm32f10x_usart.h"

static char txbuf[128];
static uint32_t last_report_tick = 0;
#define HC05_REPORT_INTERVAL_MS  2000

static int hc05_try_baud(uint32_t baud)
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

void HC05_SendData(const uint8_t *buf, uint16_t len)
{
    UART1_Send(buf, len);
}

void HC05_Process(void)
{
    static char rxbuf[128];
    static uint16_t rxi = 0;

    /* 清除 Overrun Error，否则 UART 会停止接收 */
    if (USART_GetFlagStatus(USART1, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(USART1);
    }

    while (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET) {
        char c = (char)USART_ReceiveData(USART1);
        if (c == '\n' || c == '\r') {
            if (rxi > 0) {
                rxbuf[rxi] = '\0';
                if (strncmp(rxbuf, "CAL", 3) == 0) {
                    float A = SPO2_CAL_A, B = SPO2_CAL_B;
                    if (sscanf(rxbuf + 3, "%f %f", &A, &B) >= 1) {
                        Set_SpO2_Calibration(A, B);
                    }
                } else if (strncmp(rxbuf, "TIME", 4) == 0) {
                    int h, m, s, d, mo, y;
                    if (sscanf(rxbuf + 4, "%d:%d:%d %d/%d/%d",
                               &h, &m, &s, &d, &mo, &y) >= 5) {
                        rtc_time_t tm;
                        tm.hour  = (uint8_t)h;
                        tm.min   = (uint8_t)m;
                        tm.sec   = (uint8_t)s;
                        tm.day   = (uint8_t)d;
                        tm.month = (uint8_t)mo;
                        tm.year  = (uint8_t)y;
                        DS1302_WriteTime(&tm);
                    }
                }
            }
            rxi = 0;
        } else {
            if (rxi < sizeof(rxbuf) - 1) rxbuf[rxi++] = c;
        }
    }

    uint32_t now = Systick_GetTick();
    if ((now - last_report_tick) >= HC05_REPORT_INTERVAL_MS) {
        last_report_tick = now;
        int hr    = Get_HeartRate();
        int spo2  = Get_SpO2();
        int steps = Get_StepCount();
        int   valid;
        float temp;
        Get_Temperature(&valid, &temp);
        float temp_out = valid ? temp : 0.0f;
        int n = snprintf(txbuf, sizeof(txbuf),
            "{\"hr\":%d,\"spo2\":%d,\"steps\":%d,\"temp\":%.2f}\r\n",
            hr, spo2, steps, (double)temp_out);
        if (n > 0) UART1_Send((uint8_t *)txbuf, (uint16_t)n);
    }
}
