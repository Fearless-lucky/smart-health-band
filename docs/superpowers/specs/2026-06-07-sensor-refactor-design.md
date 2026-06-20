# 传感器层标准化重构设计

**日期**: 2026-06-07  
**范围**: 传感器采集层（第一期），不涉及显示/蓝牙/语音模块

## 1. 目标

将四个传感器驱动（MAX30102、MPU6050、DS18B20、DS1302）重构为标准化模块，每个模块：

- 独立 `.c` / `.h` / `_config.h` 三文件结构
- 统一 `Init()` → 返回 int 错误码
- 统一 `ReadData()` → 通过指针输出数据，返回 int 错误码
- 硬件参数全部提取到 `_config.h`，修改时无需进入实现代码
- 注释标注引脚、通信方式、I2C 地址

## 2. 文件变更清单

### 新增文件

| 文件 | 说明 |
|------|------|
| `crouse.h/max30102_config.h` | MAX30102 硬件参数集中管理 |
| `crouse.h/mpu6050_config.h` | MPU6050 硬件参数集中管理 |
| `crouse.h/ds18b20_config.h` | DS18B20 硬件参数集中管理 |
| `crouse.h/ds1302_config.h` | DS1302 硬件参数集中管理 |

### 重构文件（原地修改）

| 文件 | 变更 |
|------|------|
| `crouse.c/max30102.c` | 寄存器地址保留内部宏；LED电流/采样率参数改用 `#include "max30102_config.h"`；API 简化为 `MAX30102_Init()` + `MAX30102_ReadData()` |
| `crouse.h/max30102.h` | 只暴露 `Init()` 和 `ReadData()`，移除 `SetSampleRate`/`SetLEDCurrent`/`ClearFIFO`/`GetBuffer` |
| `crouse.c/mpu6050.c` | 寄存器地址保留内部宏；合并 `ReadAccel`+`ReadTemp` 为 `ReadData()`，移除三个 `GetAccelX/Y/Z` |
| `crouse.h/mpu6050.h` | 只暴露 `Init()` 和 `ReadData()`，移除 `ReadAccel`/`GetAccelX/Y/Z`/`ReadTemp` |
| `crouse.c/ds18b20.c` | 引脚改用 config 宏；`DS18B20_ReadTemp()` 重命名为 `DS18B20_ReadData()`（增加 int 返回码）；`RequestTemp()` 重命名为 `StartConversion()` |
| `crouse.h/ds18b20.h` | API 重命名: `Init()` / `StartConversion()` / `ReadData()` |
| `crouse.c/ds1302.c` | 引脚改用 config 宏；SPI-like 时序保留内部 static；API 保持不变（`Init`/`ReadTime`/`WriteTime`/`SelfTest`） |
| `crouse.h/ds1302.h` | 保持不变（本身已经标准化） |
| `crouse.c/algorithms.c` | 算法逻辑完全不变，只调整参数引用方式为 `#include "max30102_config.h"` |
| `crouse.c/main.c` | 简化：只做初始化+任务创建；移除内联的业务逻辑和自检代码；改为调用模块标准 API |

### 不变文件

| 文件 | 说明 |
|------|------|
| `crouse.h/algorithms.h` | API 不变 |
| `crouse.h/globals.h` | 三个全局变量不变 |
| `crouse.c/i2c_hal.c` | I2C 底层不变 |
| `crouse.h/i2c_hal.h` | 不变 |
| `crouse.c/uart_hal.c` | UART 底层不变 |
| `crouse.h/uart_hal.h` | 不变 |
| `crouse.c/oled_ssd1306.c` / `.h` | 显示模块不变 |
| `crouse.c/hc05.c` / `.h` | 蓝牙模块不变 |
| `crouse.c/asr_pro.c` / `.h` | 语音模块不变 |
| `crouse.c/keys.c` / `.h` | 按键模块不变 |
| `crouse.c/systick.c` / `.h` | 系统滴答不变 |
| `crouse.h/FreeRTOSConfig.h` | FreeRTOS 配置不变 |
| 所有 `Device/`、`Core/`、`freertos/`、`RTE/` | 不变 |

## 3. 标准化 API 设计

### 3.1 MAX30102（PPG 心率血氧传感器）

```c
// === crouse.h/max30102.h ===
// 通信: I2C1, PB6=SCL, PB7=SDA
// 地址: 0xAE (8-bit write), 7-bit=0x57

int  MAX30102_Init(void);
// 返回: 0=成功, -1=I2C 通信失败

int  MAX30102_ReadData(int32_t *ir, int32_t *red, uint16_t *count);
// 读取 FIFO 全部可用样本，内部直接调用 PPG_ProcessSamples()
// ir/red: 输出缓冲区指针（传 NULL 则跳过该通道）
// count: 输入=缓冲区容量, 输出=实际样本数
// 返回: 0=成功, -1=无数据, -2=I2C 错误
```

内部保留的静态函数：`write_reg`、`read_reg`、`modify_reg`（不对外暴露）。

### 3.2 MPU6050（六轴加速度+温度）

```c
// === crouse.h/mpu6050.h ===
// 通信: I2C1, PB6=SCL, PB7=SDA (与 MAX30102 共享)
// 地址: 0xD0 (8-bit write), 7-bit=0x68

int  MPU6050_Init(void);
// 返回: 0=成功, -1=I2C 通信失败

int  MPU6050_ReadData(int16_t *ax, int16_t *ay, int16_t *az, float *chip_temp);
// ax/ay/az: 三轴加速度（传 NULL 跳过）
// chip_temp: 芯片温度 °C（传 NULL 跳过）
// 返回: 0=成功, -1=I2C 错误
```

### 3.3 DS18B20（1-Wire 温度传感器）

```c
// === crouse.h/ds18b20.h ===
// 通信: 1-Wire (单总线), 需 4.7kΩ 上拉
// 引脚: 见 ds18b20_config.h
//
// 使用方法: 先调用 StartConversion() 启动转换,
//           等待 ≥750ms 后调用 ReadData() 读取结果

int  DS18B20_Init(void);
// 返回: 0=成功, -1=总线无设备

void DS18B20_StartConversion(void);
// 发送 Skip ROM + Convert T 命令 (0xCC 0x44), 不阻塞
// 需等待 ≥750ms (12-bit) 后方可调用 ReadData()

int  DS18B20_ReadData(float *temperature);
// 发送 Skip ROM + Read Scratchpad, 读取9字节, CRC校验
// 返回: 0=成功, -1=CRC校验失败, -2=总线无响应
```

重要：1-Wire 温度转换需要 ~750ms。函数拆分为「启动」和「读取」两步是硬件要求，上层（vTaskSensors）通过 FreeRTOS 延时管理等待，避免阻塞高优先级任务。

### 3.4 DS1302（RTC 时钟）

```c
// === crouse.h/ds1302.h (保持不变) ===
// 通信: 三线 SPI-like (CE/DAT/CLK)
// 引脚: 见 ds1302_config.h

int  DS1302_Init(void);
int  DS1302_ReadTime(rtc_time_t *tm);
int  DS1302_WriteTime(const rtc_time_t *tm);
int  DS1302_SelfTest(void);   // 0=OK, -1=通信失败, -2=RAM R/W失败, -3=WP失败
```

DS1302 的 API 已经是标准化形式，重构仅提取引脚到 config 文件。

## 4. Config 文件详细定义

以下所有参数值均来自当前代码中实际使用的值，保持不变。

### 4.1 `max30102_config.h`

```c
#ifndef __MAX30102_CONFIG_H
#define __MAX30102_CONFIG_H

/* ============================================================
 * MAX30102 硬件配置 — PPG 心率血氧传感器
 * 通信: I2C1
 * 引脚: PB6=SCL, PB7=SDA (共享 I2C 总线)
 * I2C 地址: 7-bit 0x57, 8-bit write 0xAE
 * ============================================================ */

#define MAX30102_I2C_ADDR          (0x57 << 1)  /* 8-bit write address = 0xAE */

/* ---- 初始化参数 ---- */
#define MAX30102_SAMPLE_RATE       0x01    /* SPO2_CONFIG[4:2]: 50Hz→0, 100Hz→1, 200Hz→2, 400Hz→3 */
#define MAX30102_PULSE_WIDTH       0x03    /* SPO2_CONFIG[1:0]: 0=69us, 1=118us, 2=215us, 3=411us */
#define MAX30102_ADC_RANGE         0x03    /* SPO2_CONFIG[6:5]: 0=2048, 1=4096, 2=8192, 3=16384 */
#define MAX30102_SPO2_CONFIG       ((MAX30102_ADC_RANGE << 5) | (MAX30102_SAMPLE_RATE << 2) | MAX30102_PULSE_WIDTH)
                                           /* 当前值 = 0x47 (4096 ADC, 100Hz, 411us) */

#define MAX30102_LED1_CURRENT      0x50    /* IR LED 电流, 约 16mA (0.2mA/step * 0x50 = 16mA) */
#define MAX30102_LED2_CURRENT      0x50    /* Red LED 电流 */

#define MAX30102_FIFO_CONFIG       0x10    /* FIFO 滚动覆盖模式, 不平均 */

/* ---- PPG 算法参数 (algorithms.c 使用) ---- */
#define PPG_SAMPLE_RATE            100.0f  /* Hz */
#define PPG_BUF_LEN                256     /* 环形缓冲区大小 */
#define PPG_WINDOW_SIZE            200     /* 分析窗口样本数 */
#define PPG_MIN_SAMPLES            50      /* 最小样本数才启动分析 */
#define PPG_LP_CUTOFF              5.0f    /* 低通截止频率 Hz */
#define PPG_HP_CUTOFF              0.5f    /* 高通截止频率 Hz */
#define PPG_EMA_ALPHA              0.15f   /* 心率/血氧指数移动平均平滑系数 */

#define PPG_PEAK_THRESH_RATIO      0.8f    /* 峰值检测: threshold = mean + ratio * std */
#define PPG_PEAK_MIN_DIST_S        0.4f    /* 峰间最短间隔 秒 */
#define PPG_PERIOD_MIN_S           0.3f    /* 最小心跳周期 秒 (≦200bpm) */
#define PPG_PERIOD_MAX_S           2.0f    /* 最大心跳周期 秒 (≧30bpm) */
#define PPG_PEAK_HISTORY_MAX       8       /* 最大峰数 */

#define PPG_STD_ACTIVE_MIN         0.2f    /* 标准差低于此值判定无信号 */

/* ---- SpO2 校准 ---- */
#define SPO2_CAL_A                 110.0f
#define SPO2_CAL_B                 25.0f
#define SPO2_CLAMP_MIN             50
#define SPO2_CLAMP_MAX             100

/* ---- 内部缓冲区 ---- */
#define MAX30102_BUF_LEN           128     /* IR/Red 本地缓冲区大小 */

#endif
```

### 4.2 `mpu6050_config.h`

```c
#ifndef __MPU6050_CONFIG_H
#define __MPU6050_CONFIG_H

/* ============================================================
 * MPU6050 硬件配置 — 六轴加速度传感器
 * 通信: I2C1
 * 引脚: PB6=SCL, PB7=SDA (与 MAX30102 共享 I2C 总线)
 * I2C 地址: 7-bit 0x68, 8-bit write 0xD0
 * ============================================================ */

#define MPU6050_I2C_ADDR           (0x68 << 1)  /* 8-bit write address = 0xD0 */

/* ---- 加速度量程 ---- */
#define MPU6050_ACCEL_RANGE        0x00    /* 0=±2g, 1=±4g, 2=±8g, 3=±16g */

/* ---- 芯片温度计算公式 ---- */
#define MPU6050_TEMP_OFFSET        36.53f  /* °C 偏移量 */
#define MPU6050_TEMP_SCALE         340.0f  /* LSB/°C 转换系数 */
#define MPU6050_TEMP_FALLBACK      25.0f   /* 读取失败时返回的默认值 */

/* ---- 计步算法参数 (algorithms.c 使用) ---- */
#define STEP_GRAVITY_EMA           0.98f   /* 重力估计指数移动平均系数 */
#define STEP_THRESH_EMA            0.995f  /* 阈值自适应指数移动平均系数 */
#define STEP_INIT_THRESH           500.0f  /* 初始阈值 (归一化后 LSB) */
#define STEP_SETTLE_COUNT          10      /* 前N次静默稳定期（约2秒） */
#define STEP_MIN_INTERVAL_MS       400     /* 最小步间隔 ms (>400ms → ≤150步/分) */
#define STEP_HISTORY_LEN           8       /* 步间隔历史缓冲区大小 */

/* ---- 运动状态分类 ---- */
#define ACTIVITY_SHAKE_ENERGY      400     /* 晃动能量阈值 */
#define ACTIVITY_IDLE_MS           2000    /* 判定无动作的时间窗口 ms */
#define ACTIVITY_WALK_SPM_MIN      40.0f   /* 步行步频下限 steps/min */
#define ACTIVITY_RUN_SPM_MIN       120.0f  /* 跑步步频下限 steps/min */

#endif
```

### 4.3 `ds18b20_config.h`

```c
#ifndef __DS18B20_CONFIG_H
#define __DS18B20_CONFIG_H

#include "stm32f10x.h"

/* ============================================================
 * DS18B20 硬件配置 — 1-Wire 数字温度传感器
 * 通信: 1-Wire (单总线), 需 4.7kΩ 上拉到 3.3V
 * 引脚: PA0 = DQ
 *       (PB10 已被 I2C2 SCL 占用, 故 DS18B20 使用 PA0)
 * ============================================================ */

#define DS18B20_PORT               GPIOA
#define DS18B20_PIN                GPIO_Pin_0

/* ---- 温度补偿参数 (algorithms.c 使用) ---- */
#define TEMP_BASE_OFFSET           2.5f    /* 基础温差 skin→core °C */
#define TEMP_AMBIENT_REF           25.0f   /* 参考环境温度 °C */
#define TEMP_AMBIENT_COEFF         0.05f   /* 环境温度修正系数 */
#define TEMP_OFFSET_MIN            1.0f    /* 最小补偿量 °C */
#define TEMP_OFFSET_MAX            3.5f    /* 最大补偿量 °C */
#define TEMP_CRC_INVALID           -999.0f /* CRC 校验失败返回值 */

#endif
```

### 4.4 `ds1302_config.h`

```c
#ifndef __DS1302_CONFIG_H
#define __DS1302_CONFIG_H

#include "stm32f10x.h"

/* ============================================================
 * DS1302 硬件配置 — RTC 时钟模块
 * 通信: 三线 SPI-like (CE + DAT + CLK)
 * 引脚: PB1=CLK, PB12=DAT, PB9=CE
 *       模块已内置 32.768kHz 晶振和上拉电阻
 * ============================================================ */

#define DS1302_CLK_PORT            GPIOB
#define DS1302_CLK_PIN             GPIO_Pin_1

#define DS1302_DAT_PORT            GPIOB
#define DS1302_DAT_PIN             GPIO_Pin_12

#define DS1302_CE_PORT             GPIOB
#define DS1302_CE_PIN              GPIO_Pin_9

#endif
```

## 5. main.c 重构

重构后 `main.c` 只做四件事，不含任何内联业务逻辑：

```c
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "systick.h"
#include "i2c_hal.h"
#include "uart_hal.h"
#include "max30102.h"
#include "ds18b20.h"
#include "mpu6050.h"
#include "ds1302.h"
#include "oled_ssd1306.h"
#include "hc05.h"
#include "asr_pro.h"
#include "algorithms.h"
#include "keys.h"
#include "globals.h"

/* ===== 传感器读周期计数器 ===== */
static uint8_t ds18b20_tick = 0;   /* DS18B20 每4周期读一次 */

/* ===== 任务: 传感器采集 ===== */
static void vTaskSensors(void *pvParameters)
{
    (void)pvParameters;

    for (;;) {
        /* MAX30102 — 读 FIFO, 内部自动调 PPG_ProcessSamples() */
        int32_t  ir_buf[MAX30102_BUF_LEN];
        int32_t  red_buf[MAX30102_BUF_LEN];
        uint16_t count = MAX30102_BUF_LEN;
        MAX30102_ReadData(ir_buf, red_buf, &count);

        /* MPU6050 — 读加速度 + 芯片温度 */
        int16_t ax, ay, az;
        float   chip_temp;
        MPU6050_ReadData(&ax, &ay, &az, &chip_temp);
        Step_ProcessAccel(ax, ay, az);

        /* DS18B20 — 每4周期读一次 (200ms*4=800ms ≥ 750ms转换时间) */
        if (ds18b20_tick == 0) {
            DS18B20_StartConversion();
        } else if (ds18b20_tick >= 4) {
            float skin_temp;
            if (DS18B20_ReadData(&skin_temp) == 0) {
                g_temperature = Compensate_Temperature(skin_temp, chip_temp);
                g_temp_valid  = 1;
            }
            ds18b20_tick = 0;
            continue;
        }
        ds18b20_tick++;

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ===== 任务: 显示 ===== */
static void vTaskDisplay(void *pvParameters)
{
    (void)pvParameters;
    int page = 0, tick = 0;

    for (;;) {
        if (Key_GetBack()) {
            page = (page == 0) ? 4 : (page - 1);
            tick = 16;
        }
        if (Key_GetPressed() || g_page_advance) {
            page = (page + 1) % 5;
            g_page_advance = 0;
            tick = 16;
        }

        if (tick >= 10) {
            tick = 0;
            switch (page) {
            case 0: OLED_ShowHeartRate(Get_HeartRate()); break;
            case 1: OLED_ShowSpO2(Get_SpO2());           break;
            case 2: OLED_ShowSteps(Get_StepCount());     break;
            case 3: /* 体温 — 使用全局变量 */
                {
                    char buf[16];
                    OLED_Clear();
                    OLED_DrawString(0, 1, "BodyTemp:");
                    if (g_temp_valid)
                        snprintf(buf, sizeof(buf), "%.1f C", (double)g_temperature);
                    else
                        snprintf(buf, sizeof(buf), "--.- C");
                    OLED_DrawString(0, 3, buf);
                    OLED_Flush();
                }
                break;
            case 4: /* 时间 — 直接读 RTC */
                {
                    rtc_time_t tm;
                    DS1302_ReadTime(&tm);
                    char buf[20];
                    OLED_Clear();
                    OLED_DrawString(0, 0, "-- Time --");
                    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.hour, tm.min, tm.sec);
                    OLED_DrawString(0, 2, buf);
                    snprintf(buf, sizeof(buf), "%02d/%02d/20%02d", tm.day, tm.month, tm.year);
                    OLED_DrawString(0, 4, buf);
                    OLED_Flush();
                }
                break;
            default: break;
            }
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ===== 任务: 语音识别 ===== */
static void vTaskVoice(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        ASR_ProcessUART();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ===== 任务: 蓝牙 ===== */
static void vTaskBluetooth(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        HC05_Process();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ===== 栈溢出钩子 ===== */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    UART1_Init(115200);
    const char *prefix = "STACK OVERFLOW: ";
    UART1_Send((const uint8_t *)prefix, (uint16_t)strlen(prefix));
    if (pcTaskName)
        UART1_Send((const uint8_t *)pcTaskName, (uint16_t)strlen(pcTaskName));
    UART1_Send((const uint8_t *)"\r\n", 2);
    for (;;) {}
}

/* ==================================================================
 * main() — 只负责初始化和启动任务, 无业务逻辑
 * ================================================================== */
int main(void)
{
    /* ===== 第1步: 系统初始化 ===== */
    SystemInit();
    SystemCoreClockUpdate();
    Systick_Init();

    /* 板载蜂鸣器 — 静音 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    {
        GPIO_InitTypeDef bzr = {
            .GPIO_Pin   = GPIO_Pin_8,
            .GPIO_Mode  = GPIO_Mode_Out_PP,
            .GPIO_Speed = GPIO_Speed_2MHz
        };
        GPIO_Init(GPIOB, &bzr);
        GPIO_ResetBits(GPIOB, GPIO_Pin_8);
    }

    /* ===== 第2步: 通信总线初始化 ===== */
    I2C_HAL_Init();     /* I2C1: PB6=SCL, PB7=SDA */
    I2C2_Init();        /* I2C2: PB10=SCL, PB11=SDA */
    UART1_Init(115200); /* 调试串口 */

    /* ===== 第3步: 各模块初始化 ===== */
    PPG_Init();
    MAX30102_Init();
    MPU6050_Init();
    DS18B20_Init();
    DS1302_Init();
    OLED_Init();
    Keys_Init();
    HC05_Init(9600);
    ASR_Init(9600);

    /* ===== 第4步: DS1302 自检 (调试输出) ===== */
    {
        int r = DS1302_SelfTest();
        static const char *msgs[] = {
            [0]   = "DS1302: OK\r\n",
            [-1]  = "DS1302: NO COMM (check wires!)\r\n",
            [-2]  = "DS1302: RAM R/W FAIL\r\n",
            [-3]  = "DS1302: WP REG FAIL\r\n"
        };
        const char *msg = (r >= -3 && r <= 0) ? msgs[-r] : NULL;
        if (msg) UART1_Send((const uint8_t *)msg, (uint16_t)strlen(msg));
    }

    /* ===== 第5步: 读取并输出当前RTC时间 ===== */
    {
        rtc_time_t tm;
        DS1302_ReadTime(&tm);
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "RTC: %02d:%02d:%02d %02d/%02d/20%02d\r\n",
                 tm.hour, tm.min, tm.sec, tm.day, tm.month, tm.year);
        UART1_Send((const uint8_t *)buf, (uint16_t)strlen(buf));
    }

    /* ===== 第6步: 创建 FreeRTOS 任务 ===== */
    xTaskCreate(vTaskSensors,   "Sensors", 512, NULL, 3, NULL);
    xTaskCreate(vTaskDisplay,   "Display", 512, NULL, 2, NULL);
    xTaskCreate(vTaskVoice,     "Voice",   384, NULL, 2, NULL);
    xTaskCreate(vTaskBluetooth, "BT",      384, NULL, 2, NULL);

    /* ===== 第7步: 启动调度器 ===== */
    vTaskStartScheduler();

    /* 永远不会到这里 */
    for (;;) {}
}
```

## 6. 功能兼容性对照

| 原功能 | 重构后 | 说明 |
|--------|--------|------|
| MAX30102_Init() 配置 100Hz/411us/16mA | MAX30102_Init() — 相同寄存器值 | 宏定义值不变 |
| MAX30102_ReadFIFO() 读 32 样本上限 | MAX30102_ReadData() — 相同逻辑 | 内部实现完全复用 |
| MAX30102_GetBuffer() 暴露内部指针 | MAX30102_ReadData() 直接拷贝 | 不再暴露内部指针 |
| MPU6050_ReadAccel() + 三个 Get() | MPU6050_ReadData() 一次返回三轴 | 移除静态变量 |
| MPU6050_ReadTemp() 读芯片温度 | 合并入 MPU6050_ReadData() | 减少 I2C 读操作：原两次读（6+2 bytes），现一次读 8 bytes |
| DS18B20_RequestTemp() → DS18B20_StartConversion() | 重命名，功能不变 | 两函数仍然分步调用，由 vTaskSensors 管理 800ms 等待 |
| DS18B20_ReadTemp() → DS18B20_ReadData() | 重命名 + 增加返回错误码 | CRC 失败返回 -1，温度通过指针输出；不再用 -999.0f 特殊值 |
| DS1302_Init/ReadTime/WriteTime/SelfTest | 完全不变 | 只提取引脚到 config |
| PPG_ProcessSamples/Step_ProcessAccel | 完全不变 | algorithms.c 不改变逻辑 |
| g_temperature/g_temp_valid/g_page_advance | 不变 | globals.h 不变 |
| vTaskSensors 200ms 周期 | 不变 | DS18B20 读频率不变 (~800ms) |
| 4 个 FreeRTOS 任务的栈大小/优先级 | 不变 | xTaskCreate 参数不变 |

## 7. 自检清单（实现后验证）

- [ ] Keil MDK 编译零错误零警告
- [ ] DS1302_SelfTest() 启动自检正常输出
- [ ] OLED 5 页面切换正常，心率/血氧/步数/体温/时间可显示
- [ ] DS18B20 CRC 失败时显示 `--.- C` 而非错误值
- [ ] HC-05 蓝牙 JSON 上报正常（心率/血氧/步数/温度）
- [ ] ASR PRO 语音切页正常
- [ ] 按键上下翻页正常
- [ ] 栈溢出钩子正常（可通过临时缩小任务栈验证）
