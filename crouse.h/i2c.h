#ifndef __I2C_H
#define __I2C_H

#include <stdint.h>

/* I2C1: PB8=SCL, PB9=SDA (Remap) — MAX30102 + MPU6050 */
void I2C1_Init(void);
int  I2C_WriteReg(uint16_t dev_addr, uint8_t reg, uint8_t *data, uint16_t len);
int  I2C_ReadReg(uint16_t dev_addr, uint8_t reg, uint8_t *data, uint16_t len);

/* I2C2: PB10=SCL, PB11=SDA — OLED SSD1306 */
void I2C2_Init(void);
int  I2C2_WriteReg(uint16_t dev_addr, uint8_t reg, uint8_t *data, uint16_t len);

#endif
