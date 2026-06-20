## 第9章 语音识别怎么接入

### 9.1 ASR PRO 的工作模式

ASR PRO 是一个已经训练好的语音识别芯片。它独立完成语音采集→识别→匹配，然后通过 UART 发一个**单字节**命令码给 STM32。

```
你说"查看心率" → ASR PRO 识别 → UART2 发送 0x01 → STM32 收到 0x01 → 执行对应功能
```

ASR PRO 的固件需要单独配置——把"查看心率"这个词条和命令码 0x01 绑定。这部分在电脑上用厂家提供的配置工具完成，不在本项目的代码范围内。

### 9.2 命令码定义

```c
// 来自 asr_pro.h
#define ASR_CMD_HR        0x01  // "查看心率"
#define ASR_CMD_STEPS     0x02  // "查看步数"
#define ASR_CMD_STEPRST   0x03  // "归零步数"
#define ASR_CMD_SPO2      0x04  // "查看血氧"
#define ASR_CMD_TEMP      0x05  // "查看体温"
#define ASR_CMD_TIME      0x06  // "查看时间"
#define ASR_CMD_MAIN      0x07  // "显示主页"
#define ASR_CMD_NEXT      0x08  // "下一页"
#define ASR_CMD_ACTIVITY  0x09  // "查看状态"
```

### 9.3 命令分发

```c
void ASR_ProcessUART(void)
{
    // ① 清除 ORE — 同蓝牙，UART 溢出了就死掉了
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(USART2);
    }

    // ② 逐个接收字节，收到就处理
    while (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET) {
        uint8_t c = (uint8_t)USART_ReceiveData(USART2);

        switch (c) {
        case ASR_CMD_HR:       OLED_ShowHeartRate(Get_HeartRate());  break;
        case ASR_CMD_STEPS:    OLED_ShowSteps(Get_StepCount());      break;
        case ASR_CMD_STEPRST:  Reset_StepCount();
                               OLED_ShowSteps(Get_StepCount());      break;
        case ASR_CMD_SPO2:     OLED_ShowSpO2(Get_SpO2());           break;
        case ASR_CMD_TEMP:     /* 显示体温 */                        break;
        case ASR_CMD_TIME:     /* 显示时间 */                        break;
        case ASR_CMD_MAIN:     OLED_ShowMainPage();                 break;
        case ASR_CMD_NEXT:     g_page_advance = 1;                  break;
                               // ↑ 设置翻页标志位，Display 任务检测到后切换页面
        case ASR_CMD_ACTIVITY: /* 显示运动状态 */                    break;
        default: break;  // 不认识的命令——忽略
        }
    }
}
```

**关键设计模式**：ASR_ProcessUART 不直接操作硬件，它只是调用现成的函数：
- 显示 → `OLED_ShowHeartRate()` 等
- 数据访问 → `Get_HeartRate()` 等
- 页面切换 → 设置 `g_page_advance` 标志位

`g_page_advance` 是一个全局 volatile 变量，ASR 任务写入，Display 任务读取并清零。这种"写标志位 → 检测并清零"的模式是最简单的任务间通信方式。

---

## 第10章 OLED 显示怎么做的

### 10.1 SSD1306 的工作原理

SSD1306 是 OLED 屏幕的驱动芯片。屏幕有 128×64 = 8192 个像素点，每个像素只有亮（1）和灭（0）两种状态。8192 个像素 = 1024 字节 = 1KB 的显示数据。

**为什么只需要 1KB？** 因为这是单色屏，每个像素 1 bit。彩色屏每个像素要 16-24 bit。

### 10.2 Framebuffer — "画板"模式

```
┌────────────────────────────┐
│  framebuf[8][128]          │  内存中的"画板"
│  [0]  ██░░░░░░████░░░░     │  每行 128 字节 = 128×8 = 1024 像素
│  [1]  ░░██░░██░░░░██░░     │
│  ...                       │
│  [7]  ░░░░░░░░████████     │
└────────────────────────────┘
        │ 只有 dirty 的 page 才发
        ▼
┌────────────────────────────┐
│  SSD1306 屏幕               │  实际显示
└────────────────────────────┘
```

```c
void OLED_Flush(void)
{
    for (uint8_t page = 0; page < 8; page++) {
        if (!dirty[page]) continue;   // ← 这一步节省了大量 I2C 传输！

        write_cmd(0xB0 + page);       // 选择第 page 页
        write_cmd(0x00);              // 设置列地址低4位
        write_cmd(0x10);              // 设置列地址高4位
        write_data(framebuf[page], 128); // 发送整个页 128 字节
        dirty[page] = 0;
    }
}
```

Dirty Flag 优化：不是每次都刷新整个屏幕。只把"变脏"（被修改过）的页发给 SSD1306。

```
传统做法（每50ms刷新一次）：
  OLED_Flush → I2C 传输 1024 字节 → 耗时 ~80ms

优化后（Dirty Flag）：
  只改了一行字 → 只传 128 字节 → 耗时 ~10ms
```

### 10.3 5 页显示切换

```c
switch (page) {
case 0: OLED_ShowHeartRate(Get_HeartRate());           break;  // "HeartRate: 72 bpm"
case 1: OLED_ShowSpO2(Get_SpO2());                     break;  // "SpO2: 98 %"
case 2: OLED_ShowSteps(Get_StepCount());               break;  // "Steps: 1234"
case 3: /* 体温: "BodyTemp: 36.5 C" 或 "--.- C" */     break;
case 4: /* 时间: "-- Time --" + "14:30:00" + 日期 */   break;
}
```

Display 任务每 50ms 执行一次，但实际刷新（tick >= 10）才每 500ms 发生一次。这个 500ms 间隔让用户有足够时间看清数字，又不会让屏幕闪烁太快。

### 10.4 字体渲染

5×7 像素字体来自 `font5x7.h`。每个字符对应 5 个字节，每个字节代表一列 8 个像素（只用低 7 位）：

```
字符 'A' (0x41):
列0: 0x3E = 00111110  = ··████··
列1: 0x09 = 00001001  = ····█··█
列2: 0x09 = 00001001  = ····█··█
列3: 0x09 = 00001001  = ····█··█
列4: 0x3E = 00111110  = ··████··
```

95 个可打印字符（ASCII 32-126）都有定义，每个 5 字节，总共 95×5=475 字节——非常省空间。

---

## 第11章 代码规范与设计模式

### 11.1 统一接口规范

本项目每个传感器都遵循同一套接口命名和返回值约定：

```
┌──────────────┐    ┌────────────────────────────────────┐
│  函数模式     │    │  说明                               │
├──────────────┤    ├────────────────────────────────────┤
│ Init()       │    │  初始化硬件，返回 int: 0=成功 负数=失败 │
│ ReadData()   │    │  读取数据，通过指针输出，返回错误码     │
│ StartXxx()   │    │  启动异步操作（如 DS18B20 转换）       │
├──────────────┤    └────────────────────────────────────┘
│  模块化       │
│  xxx.c       │    实现文件：只有功能代码
│  xxx.h       │    接口声明：只暴露必要的 API
│  xxx_config.h│    参数集中：所有可调参数在一个地方
└──────────────┘
```

**为什么要统一？** 任何一个初学者学会读懂一个传感器驱动后，可以用同样的方法读懂其他所有传感器。就像学会了开大众车，就能开奥迪、斯柯达——操作逻辑一样，只是按钮位置不同。

### 11.2 Config 文件集中管理

以前修改一个 LED 电流参数，需要翻遍整个 `max30102.c` 找 `0x50` 这个值。现在：

```c
// max30102_config.h — 所有可调参数一目了然
#define MAX30102_LED1_CURRENT      0x50   /* IR LED ~16mA */
#define PPG_EMA_ALPHA              0.15f  /* 心率平滑系数 */
#define SPO2_CAL_A                 110.0f /* 血氧校准A */
```

修改参数只需打开对应的 `_config.h` 文件，不需要理解驱动代码的内部逻辑。这就像汽车说明书告诉你"胎压数值在驾驶座门框上"而不是"拆开发动机找传感器"。

### 11.3 错误码约定

```c
返回值  含义
═════════════════════════════════
  0     成功
 -1     通用错误（通信失败、无数据）
 -2     参数错误（空指针、缓冲区溢出）
 -3     I2C 总线错误
```

不要返回浮点数特殊值（如 -999.0f），而是用指针输出数据 + int 返回值表示状态。这样错误处理逻辑统一：

```c
if (DS18B20_ReadData(&skin_temp) == 0) {
    // 成功——用 skin_temp
} else {
    // 失败——不回读、不处理
}
```

### 11.4 互斥锁使用模式

两条 I2C 总线各有独立 Mutex，防止同一总线上两个设备并发访问：

```c
// i2c_hal.c 中的并发保护
static int i2c_xfer(I2C_TypeDef *i2c, SemaphoreHandle_t mtx, ...)
{
    if (i2c_lock(mtx) != 0) return -1;     // 等锁，最多 100ms

    // ... 完整的 I2C 事务 ...

out:
    if (ret != 0) {
        I2C_GenerateSTOP(i2c, ENABLE);     // 发 STOP 释放总线
        i2c_bus_reset(i2c);                // 复位 I2C 外设
    }
    i2c_unlock(mtx);                       // 释放锁
    return ret;
}
```

**错误恢复机制**：即使 I2C 通信中途失败（从设备拉死 SDA 线），硬件也会自动复位到已知良好状态。这保证了单次通信失败不会导致整个系统挂掉。

### 11.5 全局变量约定

本项目只有 3 个全局 volatile 变量：

```c
volatile float  g_temperature  = 0.0f;   // 体温 °C (Sensors 写入)
volatile int    g_temp_valid   = 0;      // 数据有效标志 (Sensors 写入)
volatile int    g_page_advance = 0;      // 翻页请求 (ASR 写入 → Display 清零)
```

**为什么用 `volatile` ？** 告诉编译器"这个变量可能在其他地方被修改，不要做任何缓存优化"。编译器默认以为全局变量只在一个地方使用，可能会优化掉重复读取——但 FreeRTOS 任务切换会导致变量被另一个任务修改。

大多数数据通过函数调用传递（如 `Get_HeartRate()`、`DS1302_ReadTime()`），只有必须跨任务的才用全局变量。这减少了"隐式耦合"——你不会担心改了哪个变量会导致什么地方出错。

### 11.6 代码分层总结

```
┌─────────────────────────────────────┐
│ 应用层 (main.c, 任务函数)            │  ← "做什么"
│ 只调用下层的 API，不含硬件操作        │
├─────────────────────────────────────┤
│ 算法层 (algorithms.c)               │  ← "怎么算"
│ 纯算法，不依赖硬件                    │
├─────────────────────────────────────┤
│ 驱动层 (max30102.c, mpu6050.c ...)  │  ← "怎么读"
│ 封装硬件操作，提供统一接口             │
├─────────────────────────────────────┤
│ 硬件抽象层 (i2c_hal.c, uart_hal.c)   │  ← "怎么发"
│ 封装 STM32 外设寄存器操作             │
├─────────────────────────────────────┤
│ STM32 HAL / CMSIS / 寄存器           │  ← 硬件
└─────────────────────────────────────┘
```

**每一层只跟它的下一层打交道，不知道上层的存在。** 这就是"依赖倒置"原则——如果你换一块 OLED 屏幕，只需要改 `oled_ssd1306.c`，算法层和应用层完全不受影响。
