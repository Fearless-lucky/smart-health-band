#include "mpu6050.h"
#include "i2c.h"
#include "systick.h"

enum {
    PWR_MGMT_1     = 0x6B,
    ACCEL_CONFIG   = 0x1C,
    ACCEL_XOUT_H   = 0x3B
};

int MPU6050_Init(void)
{
    uint8_t val;

    /* 唤醒芯片 (退出睡眠模式) */
    val = 0x00;
    if (I2C_WriteReg(MPU6050_I2C_ADDR, PWR_MGMT_1, &val, 1) != 0)
        return -1;
    Delay_ms(10);

    /* 加速度量程 ±2g (默认 16384 LSB/g) */
    val = MPU6050_ACCEL_RANGE;
    if (I2C_WriteReg(MPU6050_I2C_ADDR, ACCEL_CONFIG, &val, 1) != 0)
        return -1;

    return 0;
}

int MPU6050_ReadData(int16_t *ax, int16_t *ay, int16_t *az, float *chip_temp)
{
    /* 从 ACCEL_XOUT_H(0x3B) 连续读 8 字节:
     * [0:1]=X, [2:3]=Y, [4:5]=Z, [6:7]=TEMP */
    uint8_t raw[8];
    if (I2C_ReadReg(MPU6050_I2C_ADDR, ACCEL_XOUT_H, raw, 8) != 0)
        return -1;

    if (ax) *ax = (int16_t)((raw[0] << 8) | raw[1]);
    if (ay) *ay = (int16_t)((raw[2] << 8) | raw[3]);
    if (az) *az = (int16_t)((raw[4] << 8) | raw[5]);

    if (chip_temp) {
        int16_t temp_raw = (int16_t)((raw[6] << 8) | raw[7]);
        *chip_temp = MPU6050_TEMP_OFFSET
                   + (float)temp_raw / MPU6050_TEMP_SCALE;
    }

    return 0;
}
