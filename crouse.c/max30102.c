#include "max30102.h"
#include "i2c.h"
#include "errors.h"
#include "systick.h"
#include "algorithms.h"
#include <string.h>

enum {
    REG_INTR_STATUS_1 = 0x00,  // 中断状态 1
    REG_INTR_STATUS_2 = 0x01,  // 中断状态 2
    REG_INTR_ENABLE_1 = 0x02,  // 中断使能 1
    REG_INTR_ENABLE_2 = 0x03,  // 中断使能 2
    REG_FIFO_WR_PTR   = 0x04,  // FIFO 写指针
    REG_OVF_COUNTER   = 0x05,  // FIFO 溢出计数
    REG_FIFO_RD_PTR   = 0x06,  // FIFO 读指针
    REG_FIFO_DATA     = 0x07,  // FIFO 数据寄存器
    REG_FIFO_CONFIG   = 0x08,  // FIFO 配置
    REG_MODE_CONFIG   = 0x09,  // 模式配置
    REG_SPO2_CONFIG   = 0x0A,  // 采样率/脉宽配置
    REG_LED1_PA       = 0x0C,  // LED1 (红光) 电流
    REG_LED2_PA       = 0x0D,  // LED2 (红外) 电流
    REG_PILOT_PA      = 0x10,  // 环境消除 LED 电流
    REG_TEMP_INTEGER  = 0x1F,  // 片内温度整数部分
    REG_TEMP_FRACTION = 0x20,  // 片内温度小数部分
    REG_TEMP_CONFIG   = 0x21,  // 温度传感器配置
    REG_REV_ID        = 0xFE,  // 版本号
    REG_PART_ID       = 0xFF   // 器件 ID (=0x15 表示 MAX30102)
};

static void write_reg(uint8_t reg, uint8_t val)//向寄存器写值
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
        return ERR_IO;

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

int MAX30102_ReadData(void)//从FIFO中读取数据
{
    /* 读取 FIFO 读写指针，计算可用样本数 */
    uint8_t wr = read_reg(REG_FIFO_WR_PTR);//写到哪
    uint8_t rd = read_reg(REG_FIFO_RD_PTR);//读到哪

    int samples = (int)((wr - rd) & 0x1F);
    if (samples == 0) return ERR_NO_DATA;
    if (((rd ^ wr) & 0x20) && samples > 0) return ERR_NO_DATA;   /* OVR 置位: 数据已丢失,弃帧 */

    /* 一次性读取全部可用样本 (每样本 6 字节: R[18:0] + IR[18:0]) */
    uint8_t raw[192];//最多6×32=192字节
    int to_read = samples * 6;

    if (I2C_ReadReg(MAX30102_I2C_ADDR, REG_FIFO_DATA, raw, to_read) != 0)
        return ERR_IO;

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
        red_dec[i] = r  & 0x3FFFF;//MAX30102 的 ADC 是 18 位
        ir_dec[i]  = ri & 0x3FFFF;
    }

    /* 送入 PPG 算法层处理 */
    PPG_ProcessSamples(ir_dec, red_dec, (uint16_t)samples);

    return 0;
}
