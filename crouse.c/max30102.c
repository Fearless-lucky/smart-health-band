#include "max30102.h"
#include "i2c.h"
#include "systick.h"
#include "algorithms.h"
#include <string.h>

enum {
    REG_INTR_STATUS_1 = 0x00,
    REG_INTR_STATUS_2 = 0x01,
    REG_INTR_ENABLE_1 = 0x02,
    REG_INTR_ENABLE_2 = 0x03,
    REG_FIFO_WR_PTR   = 0x04,
    REG_OVF_COUNTER   = 0x05,
    REG_FIFO_RD_PTR   = 0x06,
    REG_FIFO_DATA     = 0x07,
    REG_FIFO_CONFIG   = 0x08,
    REG_MODE_CONFIG   = 0x09,
    REG_SPO2_CONFIG   = 0x0A,
    REG_LED1_PA       = 0x0C,
    REG_LED2_PA       = 0x0D,
    REG_PILOT_PA      = 0x10,
    REG_TEMP_INTEGER  = 0x1F,
    REG_TEMP_FRACTION = 0x20,
    REG_TEMP_CONFIG   = 0x21,
    REG_REV_ID        = 0xFE,
    REG_PART_ID       = 0xFF
};

static void write_reg(uint8_t reg, uint8_t val)
{
    I2C_WriteReg(MAX30102_I2C_ADDR, reg, &val, 1);
}

static uint8_t read_reg(uint8_t reg)
{
    uint8_t val = 0;
    I2C_ReadReg(MAX30102_I2C_ADDR, reg, &val, 1);
    return val;
}

int MAX30102_Init(void)
{
    /* 器件在线校验: PART_ID(0xFF) 固定读出 0x15。
     * 不匹配说明未接线/地址错误, 直接返回, 不再做后续寄存器配置。 */
    if (read_reg(REG_PART_ID) != 0x15)
        return -1;

    /* 软件复位 */
    write_reg(REG_MODE_CONFIG, 0x40);
    Delay_ms(10);

    /* 关闭所有中断 */
    write_reg(REG_INTR_ENABLE_1, 0x00);
    write_reg(REG_INTR_ENABLE_2, 0x00);

    /* FIFO: 滚动覆盖模式, 不平均 */
    write_reg(REG_FIFO_CONFIG, MAX30102_FIFO_CONFIG);

    /* 清空 FIFO 读写指针 (burst 读 6 字节后指针自动推进) */
    write_reg(REG_FIFO_WR_PTR, 0x00);
    write_reg(REG_FIFO_RD_PTR, 0x00);
    write_reg(REG_OVF_COUNTER, 0x00);

    /* 退出复位 */
    write_reg(REG_MODE_CONFIG, 0x00);
    Delay_ms(10);

    /* SPO2 模式: ADC量程, 采样率, 脉宽由 config 宏合成 (0x47 = 4096/100Hz/411us) */
    write_reg(REG_SPO2_CONFIG, MAX30102_SPO2_CONFIG);

    /* LED 电流 */
    write_reg(REG_LED1_PA, MAX30102_LED1_CURRENT);
    write_reg(REG_LED2_PA, MAX30102_LED2_CURRENT);

    /* 进入 SpO2 双 LED 模式 */
    write_reg(REG_MODE_CONFIG, 0x03);
    Delay_ms(50);

    return 0;
}

int MAX30102_ReadData(int32_t *ir, int32_t *red, uint16_t *count)
{
    if (!count) return -2;
    uint16_t capacity = *count;
    *count = 0;

    /* 读取 FIFO 读写指针，计算可用样本数 */
    uint8_t wr = read_reg(REG_FIFO_WR_PTR);
    uint8_t rd = read_reg(REG_FIFO_RD_PTR);

    int samples = (int)((wr - rd) & 0x1F);
    if (samples == 0) return -1;

    /* 一次性读取全部可用样本 (每样本 6 字节: R[18:0] + IR[18:0]) */
    uint8_t raw[192];
    int to_read = samples * 6;

    if (I2C_ReadReg(MAX30102_I2C_ADDR, REG_FIFO_DATA, raw, to_read) != 0)
        return -2;

    /* 解码到临时数组 (栈上最大 32 元素 × 2 通道) */
    int32_t ir_dec[32], red_dec[32];
    for (int i = 0; i < samples; i++) {
        int idx = i * 6;
        int32_t r = ((int32_t)raw[idx] << 16)
                  | ((int32_t)raw[idx + 1] << 8)
                  | raw[idx + 2];
        int32_t ri = ((int32_t)raw[idx + 3] << 16)
                   | ((int32_t)raw[idx + 4] << 8)
                   | raw[idx + 5];
        red_dec[i] = r  & 0x3FFFF;
        ir_dec[i]  = ri & 0x3FFFF;
    }

    /* 输出到调用方缓冲区 */
    int out_n = (samples < (int)capacity) ? samples : (int)capacity;
    for (int i = 0; i < out_n; i++) {
        if (ir)  ir[i]  = ir_dec[i];
        if (red) red[i] = red_dec[i];
    }
    *count = (uint16_t)out_n;

    /* 送入 PPG 算法层处理 */
    PPG_ProcessSamples(ir_dec, red_dec, (uint16_t)samples);

    return 0;
}
