#include "keys.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "FreeRTOS.h"
#include "task.h"

/* 板载按键:
 * PA0 = WK_UP — 翻页, 高电平有效 (按下→高电平)
 * 用开漏输出+低电平模拟强下拉，按钮按下时 VCC 拉高 PA0 */

void Keys_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin   = GPIO_Pin_0;
    cfg.GPIO_Mode  = GPIO_Mode_Out_OD;
    cfg.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &cfg);
    GPIO_ResetBits(GPIOA, GPIO_Pin_0);  /* 驱动低电平 → 强下拉 */
}

/* 无阻塞消抖: 调用周期 50ms, 远大于机械抖动 (<10ms),
 * 两次采样间毛刺不构成"持续按下", 无需 vTaskDelay 消抖。
 * 仅保留 300ms 防连按。 */
static int key_read(GPIO_TypeDef *port, uint16_t pin,
                    int active_high, uint32_t *last_tick)
{
    uint8_t cur = GPIO_ReadInputDataBit(port, pin);
    int pressed = active_high ? (cur == Bit_SET) : (cur == Bit_RESET);
    if (!pressed) return 0;

    uint32_t now = xTaskGetTickCount();
    if (now - *last_tick <= pdMS_TO_TICKS(300)) return 0;

    *last_tick = now;
    return 1;
}

int Key_Get(void)
{
    static uint32_t t;
    return key_read(GPIOA, GPIO_Pin_0, 1, &t);
}
