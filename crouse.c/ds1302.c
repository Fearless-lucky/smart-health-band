#include "ds1302.h"
#include "systick.h"
#include "FreeRTOS.h"
#include "task.h"

static void ce_low(void)  { GPIO_ResetBits(DS1302_CE_PORT, DS1302_CE_PIN); }
static void ce_high(void) { GPIO_SetBits(DS1302_CE_PORT, DS1302_CE_PIN); }
static void clk_low(void) { GPIO_ResetBits(DS1302_CLK_PORT, DS1302_CLK_PIN); }
static void clk_high(void){ GPIO_SetBits(DS1302_CLK_PORT, DS1302_CLK_PIN); }

static void dat_out(void)
{
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin   = DS1302_DAT_PIN;
    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;
    cfg.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DS1302_DAT_PORT, &cfg);
}

static void dat_in(void)
{
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin   = DS1302_DAT_PIN;
    cfg.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(DS1302_DAT_PORT, &cfg);
}

static void dat_set(uint8_t v)
{
    if (v) GPIO_SetBits(DS1302_DAT_PORT, DS1302_DAT_PIN);
    else   GPIO_ResetBits(DS1302_DAT_PORT, DS1302_DAT_PIN);
}

static void write_byte(uint8_t b)
{
    dat_out();
    for (int i = 0; i < 8; i++) {
        clk_low();
        dat_set(b & 0x01);
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

static uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }

void DS1302_Init(void)
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

    /* 启动时钟振荡器: 读秒寄存器, 若 CH 位(最高位)为 1 则清除并写回,
     * 否则 DS1302 出厂/换电池后晶振不振荡, 时间永远不走。
     * 仅在 CH=1 时写入, 不改动用户已设置过的秒值。 */
    taskENTER_CRITICAL();
    ce_low(); clk_low(); Delay_us(4);
    ce_high();
    write_byte(0x81);                 /* 读秒寄存器 (burst-read 单字节) */
    uint8_t sec = read_byte();
    ce_low();
    if (sec & 0x80) {                 /* CH=1, 振荡器停振 */
        Delay_us(4);
        ce_high();
        write_byte(0x8E); write_byte(0x00);   /* 关写保护 */
        ce_low();
        Delay_us(4);
        ce_high();
        write_byte(0x80); write_byte(sec & 0x7F);  /* 写秒, 清 CH, 保留秒值 */
        ce_low();
        Delay_us(4);
        ce_high();
        write_byte(0x8E); write_byte(0x80);   /* 开写保护 */
        ce_low();
    }
    taskEXIT_CRITICAL();
}

void DS1302_ReadTime(rtc_time_t *t)
{
    if (!t) return;

    taskENTER_CRITICAL();
    ce_low();
    clk_low();
    Delay_us(4);
    ce_high();

    write_byte(0xBF);
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

void DS1302_WriteTime(const rtc_time_t *t)
{
    if (!t) return;

    taskENTER_CRITICAL();
    ce_low();
    clk_low();
    Delay_us(4);
    ce_high();

    write_byte(0x8E);
    write_byte(0x00);
    ce_low();

    Delay_us(4);
    ce_high();

    write_byte(0xBE);
    write_byte(dec2bcd(t->sec  > 59 ? 59 : t->sec) & 0x7F);
    write_byte(dec2bcd(t->min  > 59 ? 59 : t->min));
    write_byte(dec2bcd(t->hour > 23 ? 23 : t->hour) & 0x3F);
    write_byte(dec2bcd(t->day  > 31 ? 31 : t->day));
    write_byte(dec2bcd(t->month > 12 ? 12 : t->month));
    write_byte(0x01);
    write_byte(dec2bcd(t->year > 99 ? 99 : t->year));
    ce_low();

    Delay_us(4);
    ce_high();

    write_byte(0x8E);
    write_byte(0x80);
    ce_low();
    taskEXIT_CRITICAL();
}

int DS1302_SelfTest(void)
{
    uint8_t sec, rd, orig;
    taskENTER_CRITICAL();

    /* 1. 通信校验: 读秒寄存器, 0xFF 说明三线未接通 */
    ce_low(); clk_low(); Delay_us(4);
    ce_high(); write_byte(0x81); sec = read_byte(); ce_low();
    if (sec == 0xFF) { taskEXIT_CRITICAL(); return -1; }

    /* 2. RAM 写读校验: 备份原值 → 写 0xA5 → 回读比对 → 还原。
     * 必须先关写保护 (WP), 否则写不进去。 */
    ce_high(); write_byte(0x8E); write_byte(0x00); ce_low(); Delay_us(4);
    ce_high(); write_byte(0xC1); orig = read_byte(); ce_low(); Delay_us(4);
    ce_high(); write_byte(0xC0); write_byte(0xA5); ce_low(); Delay_us(4);
    ce_high(); write_byte(0xC1); rd = read_byte(); ce_low();
    if (rd != 0xA5) { ce_high(); write_byte(0xC0); write_byte(orig); ce_low();
        ce_high(); write_byte(0x8E); write_byte(0x80); ce_low();
        taskEXIT_CRITICAL(); return -2; }
    ce_high(); write_byte(0xC0); write_byte(orig); ce_low(); Delay_us(4);

    /* 3. 写保护校验: 开 WP 后写 0xFF, 回读不应为 0xFF (说明 WP 生效) */
    ce_high(); write_byte(0x8E); write_byte(0x80); ce_low(); Delay_us(4);
    ce_high(); write_byte(0xC0); write_byte(0xFF); ce_low(); Delay_us(4);
    ce_high(); write_byte(0xC1); rd = read_byte(); ce_low();
    if (rd == 0xFF) { taskEXIT_CRITICAL(); return -3; }

    taskEXIT_CRITICAL();
    return 0;
}
