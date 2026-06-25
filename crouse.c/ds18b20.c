#include "ds18b20.h"
#include "systick.h"
#include "FreeRTOS.h"
#include "task.h"

static void dq_output(void)
{
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin   = DS18B20_PIN;
    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DS18B20_PORT, &cfg);
}

static void dq_input(void)
{
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin   = DS18B20_PIN;
    cfg.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(DS18B20_PORT, &cfg);
}

static void dq_low(void)  { dq_output(); GPIO_ResetBits(DS18B20_PORT, DS18B20_PIN); }
static void dq_high(void) { dq_output(); GPIO_SetBits(DS18B20_PORT, DS18B20_PIN); }
static uint8_t dq_read(void) { dq_input(); return GPIO_ReadInputDataBit(DS18B20_PORT, DS18B20_PIN); }

static int ow_reset(void)
{
    dq_low();
    Delay_us(500);
    dq_high();
    Delay_us(60);
    dq_input();
    uint8_t presence = dq_read();
    Delay_us(420);
    return (presence == 0) ? 0 : -1;
}

static void ow_write_bit(uint8_t bit)
{
    if (bit) {
        dq_low();
        Delay_us(5);
        dq_high();
        Delay_us(65);
    } else {
        dq_low();
        Delay_us(65);
        dq_high();
        Delay_us(5);
    }
}

static uint8_t ow_read_bit(void)
{
    dq_low();
    Delay_us(2);
    dq_high();
    dq_input();
    Delay_us(8);
    uint8_t bit = dq_read();
    Delay_us(55);
    return bit;
}

static void ow_write_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(b & 0x01);
        b >>= 1;
    }
}

static uint8_t ow_read_byte(void)
{
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) {
        if (ow_read_bit()) v |= (1 << i);
    }
    return v;
}

static uint8_t ow_crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        uint8_t b = data[i];
        for (int j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ b) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            b >>= 1;
        }
    }
    return crc;
}

int DS18B20_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    dq_input();

    /* 自检: 总线上是否应答 presence 脉冲。无应答说明未接线/虚焊,
     * 返回 -1 便于上电排错。调度器尚未启动, taskENTER_CRITICAL 安全。 */
    taskENTER_CRITICAL();
    int present = ow_reset();
    taskEXIT_CRITICAL();
    return (present == 0) ? 0 : -1;
}

void DS18B20_StartConversion(void)
{
    taskENTER_CRITICAL();
    ow_reset();
    ow_write_byte(0xCC);   /* Skip ROM — 单设备模式 */
    ow_write_byte(0x44);   /* Convert T — 启动温度转换 (~750ms) */
    taskEXIT_CRITICAL();
}

int DS18B20_ReadData(float *temperature)
{
    if (!temperature) return -2;

    uint8_t sp[9];
    int reset_ok;
    taskENTER_CRITICAL();
    reset_ok = ow_reset();
    if (reset_ok == 0) {
        ow_write_byte(0xCC);   /* Skip ROM */
        ow_write_byte(0xBE);   /* Read Scratchpad */
        for (int i = 0; i < 9; i++) sp[i] = ow_read_byte();
    }
    taskEXIT_CRITICAL();

    if (reset_ok != 0)
        return -2;

    if (ow_crc8(sp, 9) != 0)
        return -1;

    int16_t raw = ((int16_t)sp[1] << 8) | sp[0];
    *temperature = raw / 16.0f;
    return 0;
}
