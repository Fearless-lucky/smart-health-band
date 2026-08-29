#include "asr_pro.h"
#include "uart.h"
#include "stm32f10x_usart.h"
#include "algorithms.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>

/* 语音翻页请求队列: Voice 任务写, Display 任务读。
 * 容量 4 足够 — Display 每 50ms 取一次, 远快于语音到达速率。 */
#define ASR_QUEUE_LEN 4
static QueueHandle_t s_asr_queue = NULL;

void ASR_Init(uint32_t baud)
{
    UART2_Init(baud);
    if (s_asr_queue == NULL) {
        s_asr_queue = xQueueCreate(ASR_QUEUE_LEN, sizeof(int));
        configASSERT(s_asr_queue);
    }
}

static void asr_push_page(int page)
{
    if (s_asr_queue == NULL) return;
    (void)xQueueSend(s_asr_queue, &page, 0);
}

/* ===== 临时调试: ASR 收到的字节/事件经 UART1 转发, 排查"第二句识别失败" =====
 * UART1 运行期为 9600 (HC05_Init 设置), 串口助手用 9600 打开 PA9 查看。
 *   cmd=0x0X  命中有效命令码
 *   raw=0xXX  收到非命令字节 (模块发了别的东西/帧错)
 *   ORE!      Overrun 字节丢失 (UART 来不及读, 卡死线索)
 * 第二句时若没有任何输出 → 模块根本没发数据 → 问题在 ASR 固件, 非 STM32。 */
static void asr_dbg_byte(const char *tag, uint8_t val)
{
    char buf[24];
    int n = snprintf(buf, sizeof(buf), "[ASR] %s=0x%02X\r\n", tag, val);
    if (n > 0) UART1_Send((const uint8_t *)buf, (uint16_t)n);
}

static void asr_dbg_msg(const char *msg)
{
    UART1_Send((const uint8_t *)msg, (uint16_t)strlen(msg));
}

void ASR_ProcessUART(void)
{
    /* 清除 Overrun Error，否则 UART 会停止接收 */
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(USART2);
        asr_dbg_msg("[ASR] ORE!\r\n");
    }

    while (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET)
    {
        uint8_t c = (uint8_t)USART_ReceiveData(USART2);

        switch (c) {
        case ASR_CMD_HR:         /* 查看心率 → page 1 */
            asr_dbg_byte("cmd", c);
            asr_push_page(1);
            break;
        case ASR_CMD_STEPS:      /* 查看步数 → page 3 */
            asr_dbg_byte("cmd", c);
            asr_push_page(3);
            break;
        case ASR_CMD_STEPRST:    /* 归零步数 + 跳到步数页 */
            asr_dbg_byte("cmd", c);
            Reset_StepCount();
            asr_push_page(3);
            break;
        case ASR_CMD_SPO2:       /* 查看血氧 → page 2 */
            asr_dbg_byte("cmd", c);
            asr_push_page(2);
            break;
        case ASR_CMD_TEMP:       /* 查看体温 → page 4 */
            asr_dbg_byte("cmd", c);
            asr_push_page(4);
            break;
        case ASR_CMD_ACTIVITY:   /* 查看状态 → page 5 */
            asr_dbg_byte("cmd", c);
            asr_push_page(5);
            break;
        default:
            asr_dbg_byte("raw", c);
            break;
        }
    }
}

int ASR_GetPageRequest(void)
{
    int page;
    if (s_asr_queue == NULL) return -1;
    if (xQueueReceive(s_asr_queue, &page, 0) == pdTRUE) return page;
    return -1;
}
