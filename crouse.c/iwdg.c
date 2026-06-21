/*
 * 独立看门狗 (IWDG)
 *
 * 时基推导:
 *   LSI 标称 40kHz (STM32F103 实测 30~60kHz, 误差较大但够看门狗用)
 *   预分频 IWDG_Prescaler_256 → 计数频率 = 40000 / 256 ≈ 156.25 Hz
 *   重装载值 625 → 超时 = 625 / 156.25 = 4.0 秒
 *
 * 选 4 秒的理由:
 *   - 正常 vTaskSensors 周期 200ms, 余量 20 倍, 即使某次 I2C/1-Wire
 *     重试拉长到 1~2 秒也不会误触发复位。
 *   - DS18B20 单次转换 750ms + I2C/DS1302 时序均远小于 4s。
 *   - 若 4s 未喂狗, 几乎可断定是总线死锁或任务阻塞, 复位是正确处置。
 */
#include "iwdg.h"
#include "stm32f10x_iwdg.h"
#include "stm32f10x_rcc.h"

void IWDG_Init(void)
{
    /* 1. 使能写访问 (PR/RLR 寄存器解除写保护) */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);

    /* 2. 预分频 256 → ~156 Hz */
    IWDG_SetPrescaler(IWDG_Prescaler_256);

    /* 3. 重装载 625 → 4 秒超时 */
    IWDG_SetReload(625);

    /* 4. 先重载一次, 防止启动后立刻超时 */
    IWDG_ReloadCounter();

    /* 5. 开启看门狗 (一旦开启无法关闭, 只能靠复位) */
    IWDG_Enable();
}

void IWDG_Feed(void)
{
    IWDG_ReloadCounter();
}
