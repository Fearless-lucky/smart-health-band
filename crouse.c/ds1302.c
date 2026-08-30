#include "ds1302.h"
#include "errors.h"
#include "systick.h"
#include "FreeRTOS.h"
#include "task.h"

static void ce_low(void)  { GPIO_ResetBits(DS1302_CE_PORT, DS1302_CE_PIN); }//片选信号
static void ce_high(void) { GPIO_SetBits(DS1302_CE_PORT, DS1302_CE_PIN); }
static void clk_low(void) { GPIO_ResetBits(DS1302_CLK_PORT, DS1302_CLK_PIN); }
static void clk_high(void){ GPIO_SetBits(DS1302_CLK_PORT, DS1302_CLK_PIN); }

static void dat_out(void)//单线输出
{
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin   = DS1302_DAT_PIN;
    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DS1302_DAT_PORT, &cfg);
}

static void dat_in(void)//单线输入
{
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin   = DS1302_DAT_PIN;
    cfg.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(DS1302_DAT_PORT, &cfg);
}

static void write_byte(uint8_t b)
{
    dat_out();
    for (int i = 0; i < 8; i++) {
        clk_low();
        if (b & 0x01) GPIO_SetBits(DS1302_DAT_PORT, DS1302_DAT_PIN);
        else          GPIO_ResetBits(DS1302_DAT_PORT, DS1302_DAT_PIN);
        Delay_us(1);
        clk_high();
        Delay_us(1);
        b >>= 1;
    }
    clk_low();
}

static uint8_t read_byte(void)
{
    uint8_t b = 0;
    dat_in();
    for (int i = 0; i < 8; i++) {
        clk_low();
        Delay_us(1);
        if (GPIO_ReadInputDataBit(DS1302_DAT_PORT, DS1302_DAT_PIN)) b |= (1 << i);
        clk_high();
        Delay_us(1);
    }
    clk_low();
    return b;
}

static uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }

int DS1302_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin   = DS1302_CLK_PIN | DS1302_CE_PIN;
    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DS1302_CLK_PORT, &cfg);

    ce_low();
    clk_low();
    dat_in();

    taskENTER_CRITICAL();
    ce_low(); clk_low(); Delay_us(4);
    ce_high(); Delay_us(4);
    write_byte(0x81);
    uint8_t sec = read_byte();
    if (sec == 0xFF) {
        ce_low();
        taskEXIT_CRITICAL();
        return ERR_IO;                  /* 通信失败, 芯片无回应 */
    }
    if (sec & 0x80) {                   /* CH=1, 启动晶振 */
        ce_low(); Delay_us(4); ce_high();
        write_byte(0x8E); write_byte(0x00); ce_low(); Delay_us(4); ce_high();
        write_byte(0x80); write_byte(0x00);  ce_low(); Delay_us(4); ce_high();
        write_byte(0x8E); write_byte(0x80);
    }
    ce_low();
    taskEXIT_CRITICAL();
    return 0;
}

void DS1302_ReadTime(rtc_time_t *t)
{
    if (!t) return;

    taskENTER_CRITICAL();
    ce_low();
    clk_low();
    Delay_us(4);
    ce_high();

    write_byte(0xBF);//连续读
    uint8_t s  = read_byte();
    uint8_t m  = read_byte();
    uint8_t h  = read_byte();
    uint8_t d  = read_byte();
    uint8_t mo = read_byte();
    (void)read_byte();  /* 跳过星期 (Day of Week), 本项目未使用 */
    uint8_t y  = read_byte();
    ce_low();
    taskEXIT_CRITICAL();

    t->sec   = bcd2dec(s  & 0x7F);
    t->min   = bcd2dec(m  & 0x7F);
    t->hour  = bcd2dec(h  & 0x3F);
    t->day   = bcd2dec(d  & 0x3F);
    t->month = bcd2dec(mo & 0x1F);
    t->year  = bcd2dec(y);
}
