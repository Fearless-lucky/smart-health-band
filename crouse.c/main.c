#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_iwdg.h"
#include "FreeRTOS.h"
#include "task.h"
#include "systick.h"
#include "i2c.h"
#include "uart.h"
#include "max30102.h"
#include "ds18b20.h"
#include "mpu6050.h"
#include "ds1302.h"
#include "oled_ssd1306.h"
#include "hc05.h"
#include "asr_pro.h"
#include "algorithms.h"
#include "globals.h"
#include <stdio.h>
#include <string.h>

#define DISPLAY_REFRESH_CYCLES  10   /* 10 × 50ms = 500ms 刷新间隔 */
#define DISPLAY_FORCE_REFRESH   16   /* 翻页后强制立即刷新 (> REFRESH_CYCLES) */

volatile float  g_temperature  = 0.0f;
volatile int    g_temp_valid   = 0;
volatile int    g_page_advance = 0;

void Get_Temperature(int *valid, float *temp)
{
    taskENTER_CRITICAL();
    *valid = g_temp_valid;
    *temp  = g_temperature;
    taskEXIT_CRITICAL();
}

/* 板载按键:
 * PA0 = WK_UP — 翻页, 高电平有效 (按下→高电平)
 * 用开漏输出+低电平模拟强下拉，按钮按下时 VCC 拉高 PA0 */
static void Keys_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef cfg;
    cfg.GPIO_Pin   = GPIO_Pin_0;
    cfg.GPIO_Mode  = GPIO_Mode_Out_OD;
    cfg.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &cfg);
    GPIO_ResetBits(GPIOA, GPIO_Pin_0);  /* 驱动低电平 → 强下拉 */
}

/* 无阻塞消抖: 调用周期 50ms, 远大于机械抖动 (<10ms),
 * 两次采样间毛刺不构成"持续按下", 无需 vTaskDelay 消抖。
 * 仅保留 300ms 防连按。 */
static int key_read(GPIO_TypeDef *port, uint16_t pin,
                    int active_high, uint32_t *last_tick)
{
    uint8_t cur = GPIO_ReadInputDataBit(port, pin);
    int pressed = active_high ? (cur == Bit_SET) : (cur == Bit_RESET);
    if (!pressed) return 0;

    uint32_t now = xTaskGetTickCount();
    if (now - *last_tick <= pdMS_TO_TICKS(300)) return 0;

    *last_tick = now;
    return 1;
}

static int Key_Get(void)
{
    static uint32_t t;
    return key_read(GPIOA, GPIO_Pin_0, 1, &t);
}

/* 独立看门狗 (IWDG) — 基于 LSI (~40kHz), 与主时钟无关。
 *   LSI 40kHz / 256 预分频 / 重载 625 = 4.0 秒超时。
 *   4s 未喂狗几乎可断定总线死锁或任务阻塞, 复位是正确处置。
 *   vTaskSensors 每周期喂狗; main() 启动调度器前调用 IWDG_Init()。 */
static void IWDG_Init(void)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_256);
    IWDG_SetReload(625);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

static void IWDG_Feed(void)
{
    IWDG_ReloadCounter();
}

/* 传感器采集任务 — 200ms, prio 3 */
static void vTaskSensors(void *pvParameters)
{
    (void)pvParameters;
    static uint8_t ds18b20_tick = 0;

    for (;;) {
        /* 喂狗: IWDG 4s 超时, 正常每轮 ≤ 1s, 余量充足。
         * 放在循环顶部保证两条出口路径 (正常 vTaskDelay / DS18B20 continue)
         * 都能在每轮触发, 不会因 continue 跳过。 */
        IWDG_Feed();

        /* MAX30102 — 读 FIFO, 内部自动调用 PPG_ProcessSamples()
         * 传 NULL: 原始样本已在驱动内部送入 PPG 算法层, 这里无需拷贝,
         * 避免在任务栈上分配 1KB 缓冲区 (Sensors 栈仅 2KB)。 */
        {
            uint16_t count = 0;
            MAX30102_ReadData(NULL, NULL, &count);
        }

        /* MPU6050 — 读加速度和芯片温度 (一次 I2C 批量读取 8 字节)
         * mpu_ok 标志: 仅在温度传感器可用时才标记体温有效,
         * 避免用 fallback 25°C 补偿的假数据被外界当作真实体温。 */
        int16_t ax = 0, ay = 0, az = 0;
        float   chip_temp = MPU6050_TEMP_FALLBACK;
        int     mpu_ok   = 0;

        if (MPU6050_ReadData(&ax, &ay, &az, &chip_temp) == 0) {
            Step_ProcessAccel(ax, ay, az);
            mpu_ok = 1;
        }

        /* DS18B20 — 两步操作: 启动转换 → 等待 → 读取 */
        if (ds18b20_tick == 0) {
            DS18B20_StartConversion();
        } else if (ds18b20_tick >= 5) {
            float skin_temp;
            if (DS18B20_ReadData(&skin_temp) == 0) {
                g_temperature = Compensate_Temperature(skin_temp, chip_temp);
                g_temp_valid  = mpu_ok;  /* 仅 MPU6050 在线时才标有效 */
            }
            ds18b20_tick = 0;
            continue;   /* 跳过 vTaskDelay, 立即开始下一轮 */
        }
        ds18b20_tick++;

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* 显示任务 — 50ms, prio 2 */
static void vTaskDisplay(void *pvParameters)
{
    (void)pvParameters;
    int page = 0;
    int tick = DISPLAY_FORCE_REFRESH;   /* 复位后立即刷新首页, 无需等500ms */

    for (;;) {
        if (Key_Get() || g_page_advance) {
            page = (page + 1) % 5;
            g_page_advance = 0;
            tick = DISPLAY_FORCE_REFRESH;
        }

        if (tick >= DISPLAY_REFRESH_CYCLES) {
            tick = 0;
            switch (page) {
            case 0: /* 主页 (大号时间 + 语音指令指南) */
                OLED_ShowMainPage();
                break;
            case 1: /* 心率 */
                OLED_ShowHeartRate(Get_HeartRate());
                break;
            case 2: /* 血氧 */
                OLED_ShowSpO2(Get_SpO2());
                break;
            case 3: /* 步数 */
                OLED_ShowSteps(Get_StepCount());
                break;
            case 4: /* 体温 */
                OLED_ShowTemperature();
                break;
            default:
                break;
            }
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* 语音识别任务 — 100ms, prio 2 */
static void vTaskVoice(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        ASR_ProcessUART();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* 蓝牙通信任务 — 20ms, prio 2 */
static void vTaskBluetooth(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        HC05_Process();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    UART1_Init(115200);
    const char *prefix = "STACK OVERFLOW: ";
    UART1_Send((const uint8_t *)prefix, (uint16_t)strlen(prefix));
    if (pcTaskName) {
        UART1_Send((const uint8_t *)pcTaskName, (uint16_t)strlen(pcTaskName));
    }
    UART1_Send((const uint8_t *)"\r\n", 2);
    for (;;) {}
}

/* configASSERT 钩子: 可能在 (1) 调度器启动前 (2) UART1 已被 HC05_Init
 * 切到 9600 的运行期 两种情形触发。
 *   - 情形 1: UART1 未初始化, 必须先 UART1_Init(115200) 才能打印;
 *   - 情形 2: UART1 已是 9600, 重新 Init(115200) 只是临时切波特率,
 *             反正随后即死循环, 不影响蓝牙。
 * 死循环期间 IWDG 不再被喂狗 → 4s 后系统复位, 自动尝试恢复。 */
void Assert_Handler(const char *file, int line)
{
    taskDISABLE_INTERRUPTS();
    UART1_Init(115200);
    const char *prefix = "ASSERT FAIL: ";
    UART1_Send((const uint8_t *)prefix, (uint16_t)strlen(prefix));
    if (file) {
        UART1_Send((const uint8_t *)file, (uint16_t)strlen(file));
    }
    char buf[16];
    int n = snprintf(buf, sizeof(buf), ":%d\r\n", line);
    if (n > 0) UART1_Send((const uint8_t *)buf, (uint16_t)n);
    for (;;) {}
}

int main(void)
{
    SystemInit();
    SystemCoreClockUpdate();
    Systick_Init();

    I2C1_Init();        /* I2C1: PB8=SCL, PB9=SDA — MAX30102 + MPU6050 (重映射) */
    I2C2_Init();        /* I2C2: PB10=SCL, PB11=SDA — OLED SSD1306 */
    UART1_Init(115200); /* PA9=TX, PA10=RX — 调试 + HC-05 */

    PPG_Init();

    MAX30102_Init();    /* I2C1, 0x57 — 心率血氧 PPG */
    MPU6050_Init();     /* I2C1, 0x68 — 六轴加速度 + 芯片温度 */
    DS18B20_Init();     /* 1-Wire, PA1 — 皮肤温度 */
    DS1302_Init();      /* 三线, PB1/PB14/PB13 — RTC 时钟 */

    OLED_Init();        /* I2C2, 0x3C — 128x64 显示屏 */
    Keys_Init();        /* WK_UP(PA0)=翻页 */
    HC05_Init(9600);    /* UART1, 9600bps — 蓝牙 */
    ASR_Init(9600);     /* UART2, 9600bps — 语音识别 */

    {
        int r = DS1302_SelfTest();
        const char *msg;
        switch (r) {
        case  0: msg = "DS1302: OK\r\n";                      break;
        case -1: msg = "DS1302: NO COMM (check wires!)\r\n";  break;
        case -2: msg = "DS1302: RAM R/W FAIL\r\n";            break;
        case -3: msg = "DS1302: WP REG FAIL\r\n";             break;
        default: msg = "DS1302: UNKNOWN\r\n";                 break;
        }
        UART1_Send((const uint8_t *)msg, (uint16_t)strlen(msg));
    }

    {
        rtc_time_t tm;
        DS1302_ReadTime(&tm);
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "RTC: %02d:%02d:%02d %02d/%02d/20%02d\r\n",
                 tm.hour, tm.min, tm.sec, tm.day, tm.month, tm.year);
        UART1_Send((const uint8_t *)buf, (uint16_t)strlen(buf));
    }

    xTaskCreate(vTaskSensors,   "Sensors", 512, NULL, 3, NULL);
    xTaskCreate(vTaskDisplay,   "Display", 512, NULL, 2, NULL);
    xTaskCreate(vTaskVoice,     "Voice",   384, NULL, 2, NULL);
    xTaskCreate(vTaskBluetooth, "BT",      384, NULL, 2, NULL);

    /* 启动独立看门狗 (4s 超时): 放在所有初始化完成后、调度器启动前。
     * 一旦开启无法关闭, 由 vTaskSensors 每周期喂狗。若调度器未能启动
     * (configASSERT 触发等), 看门狗会在 4s 后复位——但这正是我们想要的故障恢复行为。 */
    IWDG_Init();

    vTaskStartScheduler();

    /* 永远不会执行到这里 */
    for (;;) {}
}
