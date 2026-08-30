/*
 * I2C1: PB8=SCL, PB9=SDA  — MAX30102 + MPU6050 (重映射)
 * I2C2: PB10=SCL, PB11=SDA — OLED SSD1306
 *
 * 两条总线各自有独立互斥锁，防止同一总线上多设备并发访问冲突。
 * 每次 I2C 错误后自动复位总线 (DeInit+ReInit)，保证自恢复。
 */
#include "i2c.h"
#include "errors.h"
#include "stm32f10x.h"
#include "stm32f10x_i2c.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "systick.h"
#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t g_i2c_mutex  = NULL;  // I2C1锁: MAX30102+MPU6050
static SemaphoreHandle_t g_i2c2_mutex = NULL;  // I2C2锁: OLED

static void i2c_bus_reset(I2C_TypeDef *i2c)//I2C总线重置
{
    I2C_InitTypeDef cfg;
    I2C_DeInit(i2c);
    I2C_StructInit(&cfg);//复位
    cfg.I2C_Mode                = I2C_Mode_I2C;
    cfg.I2C_DutyCycle           = I2C_DutyCycle_2;
    cfg.I2C_OwnAddress1         = 0x00;
    cfg.I2C_Ack                 = I2C_Ack_Enable;
    cfg.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    cfg.I2C_ClockSpeed          = 100000;
    I2C_Init(i2c, &cfg);
    I2C_Cmd(i2c, ENABLE);
}

static int i2c_wait(I2C_TypeDef *i2c, uint32_t flag, uint32_t timeout_ms)//等待标志位置位，超时返回-1
{
    uint32_t start = Systick_GetTick();
    while ((i2c->SR1 & flag) == 0) {
        /* 总线错误: AF(从机不应答) / BERR(总线错误) / ARLO(仲裁丢失)
         * 一旦置位, 目标标志永远不会出现, 立即返回避免傻等满超时,
         * 由调用方触发 i2c_bus_reset() 恢复总线。 */
        if (i2c->SR1 & (I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO))//这三种错误直接报错
            return ERR_IO;
        if ((Systick_GetTick() - start) > timeout_ms) return ERR_TIMEOUT;//超时也报错
    }
    return 0;
}

/* START → 发7位地址 → 等 ADDR → 清 SR2. 失败返回 ERR_IO. */
static int i2c_start_addr(I2C_TypeDef *i2c, uint16_t dev_addr, int is_read)
{
    I2C_GenerateSTART(i2c, ENABLE);
    if (i2c_wait(i2c, I2C_SR1_SB, 50) != 0) return ERR_IO;
    I2C_Send7bitAddress(i2c, dev_addr, is_read ? I2C_Direction_Receiver
                                               : I2C_Direction_Transmitter);
    if (i2c_wait(i2c, I2C_SR1_ADDR, 50) != 0) return ERR_IO;
    (void)i2c->SR2;
    return 0;
}

static int i2c_xfer(I2C_TypeDef *i2c, SemaphoreHandle_t mtx,
                    uint16_t dev_addr, uint8_t reg,
                    uint8_t *data, uint16_t len, int is_read)
{
    int ret = ERR_IO;
    if (!mtx) return ERR_PARAM;
    if (xSemaphoreTake(mtx, pdMS_TO_TICKS(100)) != pdTRUE) return ERR_TIMEOUT;//拿锁，最多等100ms（freertos独特机制）

    /* ---- 第1阶段: 发送寄存器地址 ---- */
    if (i2c_start_addr(i2c, dev_addr, 0) != 0) goto out;
    I2C_SendData(i2c, reg);
    if (i2c_wait(i2c, I2C_SR1_TXE, 50) != 0) goto out;

    if (is_read) {
        /* ---- 读事务: 重复 START → 接收数据 ---- */
        if (i2c_start_addr(i2c, dev_addr, 1) != 0) goto out;//读也要再发一次起始信号

        for (uint16_t i = 0; i < len; i++) {
            if (i == len - 1) {
                I2C_AcknowledgeConfig(i2c, DISABLE);
                I2C_GenerateSTOP(i2c, ENABLE);
            }
            if (i2c_wait(i2c, I2C_SR1_RXNE, 50) != 0) goto out;
            data[i] = I2C_ReceiveData(i2c);
        }
        I2C_AcknowledgeConfig(i2c, ENABLE);
    }
    else {
        /* ---- 写事务: 连续发送数据 ---- */
        for (uint16_t i = 0; i < len; i++) {
            I2C_SendData(i2c, data[i]);
            if (i2c_wait(i2c, I2C_SR1_TXE, 50) != 0) goto out;
        }
        I2C_GenerateSTOP(i2c, ENABLE);
    }
    ret = 0;

out:
    if (ret != 0) {
        I2C_GenerateSTOP(i2c, ENABLE);
        i2c_bus_reset(i2c);
    }
    /* 到此 mtx 必非空且锁已持有: 入口两条早退路径 (!mtx / take 失败) 都在 take 成功前返回 */
    xSemaphoreGive(mtx);//放锁(不论是否出错)
    return ret;
}

void I2C1_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    /* I2C1 重映射: 默认 PB6/PB7 → 重映射 PB8/PB9 */
    GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE);

    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);
    GPIO_SetBits(GPIOB, GPIO_Pin_8 | GPIO_Pin_9);//初始高电平

    i2c_bus_reset(I2C1);

    if (g_i2c_mutex == NULL)
        g_i2c_mutex = xSemaphoreCreateMutex();//取锁
}

int I2C_WriteReg(uint16_t dev_addr, uint8_t reg, uint8_t *data, uint16_t len)
{
    return i2c_xfer(I2C1, g_i2c_mutex, dev_addr, reg, data, len, 0);
}

int I2C_ReadReg(uint16_t dev_addr, uint8_t reg, uint8_t *data, uint16_t len)
{
    return i2c_xfer(I2C1, g_i2c_mutex, dev_addr, reg, data, len, 1);
}

void I2C2_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);

    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = GPIO_Pin_10 | GPIO_Pin_11;
    gpio.GPIO_Mode  = GPIO_Mode_AF_OD;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOB, &gpio);
    GPIO_SetBits(GPIOB, GPIO_Pin_10 | GPIO_Pin_11);

    i2c_bus_reset(I2C2);

    if (g_i2c2_mutex == NULL)
        g_i2c2_mutex = xSemaphoreCreateMutex();
}

int I2C2_WriteReg(uint16_t dev_addr, uint8_t reg, uint8_t *data, uint16_t len)
{
    return i2c_xfer(I2C2, g_i2c2_mutex, dev_addr, reg, data, len, 0);
}

