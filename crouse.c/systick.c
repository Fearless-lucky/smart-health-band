#include "systick.h"
#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"

void Systick_Init(void)
{
    SystemCoreClockUpdate();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL      |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT     = 0;
}

void Delay_us(uint32_t us)
{
    uint32_t start  = DWT->CYCCNT;
    uint32_t target = us * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - start) < target) {}
}

void Delay_ms(uint32_t ms)
{
    uint32_t start  = DWT->CYCCNT;
    uint32_t target = ms * (SystemCoreClock / 1000u);
    while ((DWT->CYCCNT - start) < target) {}
}

uint32_t Systick_GetTick(void)
{
    /* Use FreeRTOS tick counter (no overflow issue). Before scheduler
     * starts or from ISR context, fall back to DWT for short intervals. */
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        return (uint32_t)xTaskGetTickCount();
    }
    return DWT->CYCCNT / (SystemCoreClock / 1000u);
}
