#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "systick.h"
#include "i2c_hal.h"
#include "uart_hal.h"
#include "max30102.h"
#include "max30102_config.h"
#include "ds18b20.h"
#include "mpu6050.h"
#include "mpu6050_config.h"
#include "ds1302.h"
#include "oled_ssd1306.h"
#include "hc05.h"
#include "asr_pro.h"
#include "algorithms.h"
#include "keys.h"
#include "iwdg.h"
#include "globals.h"
#include <stdio.h>
#include <string.h>

/* ==================================================================
 * 显示控制参数
 * ================================================================== */
#define DISPLAY_REFRESH_CYCLES  10   /* 10 × 50ms = 500ms 刷新间隔 */
#define DISPLAY_FORCE_REFRESH   16   /* 翻页后强制立即刷新 (> REFRESH_CYCLES) */

/* ==================================================================
 * 全局共享变量 — 跨任务通信
 * ================================================================== */
volatile float  g_temperature  = 0.0f;
volatile int    g_temp_valid   = 0;
volatile int    g_page_advance = 0;

/* ==================================================================
 * 共享状态访问器 — 原子读取体温快照
 * ================================================================== */
void Get_Temperature(int *valid, float *temp)
{
    taskENTER_CRITICAL();
    *valid = g_temp_valid;
    *temp  = g_temperature;
    taskEXIT_CRITICAL();
}

/* ==================================================================
 * 任务: 传感器采集 (200ms周期, 优先级3)
 *
 * 轮询读取所有传感器:
 *   - MAX30102: FIFO → PPG_ProcessSamples()
 *   - MPU6050:   加速度 → Step_ProcessAccel()
 *                芯片温度 → 体温补偿参考
 *   - DS18B20:   每 4 周期读一次 (~800ms, 1-Wire 需 ≥750ms 转换时间)
 * ================================================================== */
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

/* ==================================================================
 * 任务: 显示 (50ms周期, 优先级2)
 *
 * 5 个页面轮转: 心率 → 血氧 → 步数 → 体温 → 时间
 * 按键/语音触发翻页
 * ================================================================== */

static void vTaskDisplay(void *pvParameters)
{
    (void)pvParameters;
    int page = 0;
    int tick = 0;

    for (;;) {
        if (Key_Get() || g_page_advance) {
            page = (page + 1) % 5;
            g_page_advance = 0;
            tick = DISPLAY_FORCE_REFRESH;
        }

        if (tick >= DISPLAY_REFRESH_CYCLES) {
            tick = 0;
            switch (page) {
            case 0: /* 心率 */
                OLED_ShowHeartRate(Get_HeartRate());
                break;
            case 1: /* 血氧 */
                OLED_ShowSpO2(Get_SpO2());
                break;
            case 2: /* 步数 */
                OLED_ShowSteps(Get_StepCount());
                break;
            case 3: /* 体温 */
                OLED_ShowTemperature();
                break;
            case 4: /* 时间 */
                OLED_ShowTime();
                break;
            default:
                break;
            }
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ==================================================================
 * 任务: 语音识别 (100ms周期, 优先级2)
 * ================================================================== */
static void vTaskVoice(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        ASR_ProcessUART();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ==================================================================
 * 任务: 蓝牙通信 (20ms周期, 优先级2)
 *
 * 每 2 秒自动上报 JSON: {"hr":N,"spo2":N,"steps":N,"temp":N.NN}
 * 接收 AT/CAL/TIME 指令
 * ================================================================== */
static void vTaskBluetooth(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        HC05_Process();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ==================================================================
 * FreeRTOS 栈溢出钩子 — 通过 UART1 输出任务名
 * ================================================================== */
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

/* ==================================================================
 * configASSERT 钩子 — FreeRTOS 内部断言失败时打印位置后死循环
 *
 * 注意: configASSERT 可能在 (1) 调度器启动前 (2) UART1 已被 HC05_Init
 * 切到 9600 的运行期 两种情形下触发。
 *   - 情形 1: UART1 未初始化, 必须先 UART1_Init(115200) 才能打印;
 *   - 情形 2: UART1 已是 9600, 重新 Init(115200) 只是临时切波特率,
 *             反正随后即死循环, 不影响蓝牙。
 * 死循环期间 IWDG 不再被喂狗 → 4s 后系统复位, 自动尝试恢复。
 * ================================================================== */
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

/* ==================================================================
 * main() — 初始化所有模块, 创建任务, 启动调度器
 * ================================================================== */
int main(void)
{
    /* ===== 第1步: 系统内核初始化 ===== */
    SystemInit();
    SystemCoreClockUpdate();
    Systick_Init();

    /* ===== 第2步: 通信总线初始化 ===== */
    I2C_HAL_Init();     /* I2C1: PB8=SCL, PB9=SDA — MAX30102 + MPU6050 (重映射) */
    I2C2_Init();        /* I2C2: PB10=SCL, PB11=SDA — OLED SSD1306 */
    UART1_Init(115200); /* PA9=TX, PA10=RX — 调试 + HC-05 */

    /* ===== 第3步: 算法层初始化 ===== */
    PPG_Init();

    /* ===== 第4步: 传感器初始化 ===== */
    MAX30102_Init();    /* I2C1, 0x57 — 心率血氧 PPG */
    MPU6050_Init();     /* I2C1, 0x68 — 六轴加速度 + 芯片温度 */
    DS18B20_Init();     /* 1-Wire, PA1 — 皮肤温度 */
    DS1302_Init();      /* 三线, PB1/PB14/PB13 — RTC 时钟 */

    /* ===== 第5步: 外设初始化 ===== */
    OLED_Init();        /* I2C2, 0x3C — 128x64 显示屏 */
    Keys_Init();        /* WK_UP(PA0)=翻页 */
    HC05_Init(9600);    /* UART1, 9600bps — 蓝牙 */
    ASR_Init(9600);     /* UART2, 9600bps — 语音识别 */

    /* ===== 第6步: DS1302 自检 ===== */
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

    /* ===== 第7步: 读取并输出当前 RTC 时间 (验证用) ===== */
    {
        rtc_time_t tm;
        DS1302_ReadTime(&tm);
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "RTC: %02d:%02d:%02d %02d/%02d/20%02d\r\n",
                 tm.hour, tm.min, tm.sec, tm.day, tm.month, tm.year);
        UART1_Send((const uint8_t *)buf, (uint16_t)strlen(buf));
    }

    /* ===== 第8步: 创建 FreeRTOS 任务 ===== */
    xTaskCreate(vTaskSensors,   "Sensors", 512, NULL, 3, NULL);
    xTaskCreate(vTaskDisplay,   "Display", 512, NULL, 2, NULL);
    xTaskCreate(vTaskVoice,     "Voice",   384, NULL, 2, NULL);
    xTaskCreate(vTaskBluetooth, "BT",      384, NULL, 2, NULL);

    /* ===== 第9步: 启动独立看门狗 (4s 超时) =====
     * 放在所有初始化完成后、调度器启动前。一旦开启无法关闭,
     * 由 vTaskSensors 每周期喂狗。若调度器未能启动 (configASSERT 触发等),
     * 看门狗会在 4s 后复位——但这正是我们想要的故障恢复行为。 */
    IWDG_Init();

    /* ===== 第10步: 启动调度器 ===== */
    vTaskStartScheduler();

    /* 永远不会执行到这里 */
    for (;;) {}
}
