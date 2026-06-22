# FreeRTOS 入门：跟着智能手环项目学

> 适用读者：会 C 语言、玩过单片机、但没真正用过 RTOS 的同学
> 学习方式：项目驱动，**每学一个 FreeRTOS 概念，就看本项目里它出现在哪里、为什么这么写**
> 项目原型：`智能健康手环` (STM32F103ZE + 4 个 FreeRTOS 任务)

---

## 目录

- [序章：为什么这个手环要上 RTOS](#序章为什么这个手环要上-rtos)
- [第 0 章：嵌入式小公司开张](#第-0-章嵌入式小公司开张)
- [第 1 章：任务的诞生 —— `xTaskCreate`](#第-1-章任务的诞生--xtaskcreate)
- [第 2 章：谁的活儿先干 —— 优先级与调度](#第-2-章谁的活儿先干--优先级与调度)
- [第 3 章：报时钟的脉搏 —— `SysTick`、`vTaskDelay`、`pdMS_TO_TICKS`](#第-3-章报时钟的脉搏--systickvtaskdelaypdmsto_ticks)
- [第 4 章：共享资源的洗手间 —— 互斥锁 `Mutex`](#第-4-章共享资源的洗手间--互斥锁-mutex)
- [第 5 章：老板亲自出马 —— 临界区 `taskENTER_CRITICAL`](#第-5-章老板亲自出马--临界区-taskenter_critical)
- [第 6 章：贴在墙上的明信片 —— `volatile` 全局变量](#第-6-章贴在墙上的明信片--volatile-全局变量)
- [第 7 章：工厂的安全网 —— 栈溢出、断言、IWDG](#第-7-章工厂的安全网--栈溢出断言iwdg)
- [第 8 章：自由王国也有宪法 —— `FreeRTOSConfig.h` 逐行解读](#第-8-章自由王国也有宪法--freertosconfigh-逐行解读)
- [第 9 章：整张调度时序图 + FAQ](#第-9-章整张调度时序图--faq)

---

## 序章：为什么这个手环要上 RTOS

这个手环要做的事不少：

- 每 200ms 读一次心率血氧传感器（MAX30102），内部 FIFO 每 320ms 就会塞满
- 每 50ms 刷新一下 OLED 屏幕，5 个页面轮转
- 每 100ms 询问一下语音模块（ASR PRO）有没有收到新指令
- 每 20ms 摸一下蓝牙（HC-05），看有没有 AT 指令；每 2 秒主动发一次 JSON 报告
- 此外还有 DS18B20 体温、MPU6050 步数、DS1302 时钟……

如果你用裸机（`while(1) { 读传感器; 刷屏; 查语音; 查蓝牙; }`）写，最大的麻烦是：**所有事情只能串行**。当蓝牙在发 200 字节 JSON 报告（耗时可能 20ms）时，OLED 屏幕就在黑屏等；MAX30102 的 FIFO 可能在那一瞬间塞满丢数据。

**上 RTOS 的本质，就是把"一双手"变成"多双手"。** 4 件事可以"同时"进行（其实是高速切换的拟态），互不阻塞；硬件 FIFO 永远有人及时读；按键/语音一来屏幕立刻翻页。

FreeRTOS 就是这套机制的免费、开源、轻量实现，单片机裸机几 KB RAM 就能跑。

---

## 第 0 章：嵌入式小公司开张

为了后面讲起来不抽象，我们把单片机想象成一家小工厂。

| 现实元素 | 工厂比喻 | FreeRTOS 术语 |
|---------|---------|---------------|
| 工厂本身 | 一颗 STM32F103ZE 芯片 | MCU |
| 公司章程 | 各项配置参数 | `FreeRTOSConfig.h` |
| 公司总经办 | 整点报时钟 + 调度员 | SysTick 中断 + 调度器 |
| 员工 | 各自干一件事的"线程" | **任务 (Task)** |
| 员工工位 | 各自的办公桌 | **栈 (Stack)** |
| 员工工牌 | 谁的名字/职责 | 任务函数 / 任务名 |
| 员工职级 | 经理先于员工被叫去干活 | **优先级 (Priority)** |
| 共享设备 | 洗手间 / 复印机 | **共享资源** (I2C 总线、OLED 显存) |
| 洗手间门牌 | 一次只能一人 | **互斥锁 (Mutex)** |
| 老板亲自处理 | 全部员工原地立正 | **临界区 (Critical Section)** |
| 贴在墙上的公告 | 跨部门临时传话 | **volatile 全局变量** |
| 巡检保安 | 4 秒不来就拉警报 | **IWDG 看门狗** |

记住这张表，后面所有概念都可以"回厂"找对应。

---

## 第 1 章：任务的诞生 —— `xTaskCreate`

### 1.1 任务是什么？

任务就是一个 **死循环函数**。`for(;;) { 干活; 等一会儿; }`，就这样，没了。

```c
// 极简任务
static void vTaskXxx(void *pv) {
    for (;;) {
        DoSomething();
        vTaskDelay(pdMS_TO_TICKS(100));   // 等 100ms, 让出 CPU
    }
}
```

> **🤔 为什么是死循环？**
> 因为 FreeRTOS 期望一个任务"要么在干活，要么在等事件"，没有"干完就退出"的概念。如果真要退出，应该在退出前调 `vTaskDelete(NULL)` 把自己的栈释放掉。

### 1.2 本项目的 4 个员工

`main.c` 第 277-281 行是招聘现场：

```c
xTaskCreate(vTaskSensors,   "Sensors", 512, NULL, 3, NULL);   // 传感器
xTaskCreate(vTaskDisplay,   "Display", 512, NULL, 2, NULL);   // 显示
xTaskCreate(vTaskVoice,     "Voice",   384, NULL, 2, NULL);   // 语音
xTaskCreate(vTaskBluetooth, "BT",      384, NULL, 2, NULL);   // 蓝牙
```

| 参数 | 含义 | 本项目实例 |
|------|------|----------|
| 第 1 个 | 任务函数（员工本人） | `vTaskSensors` |
| 第 2 个 | 任务名（工牌） | `"Sensors"`，调试时用 |
| 第 3 个 | **栈大小，单位"字"（1 字 = 4 字节）** | 512 = 2 KB |
| 第 4 个 | 传给函数的参数 | `NULL` |
| 第 5 个 | **优先级**（0 最低，数字越大越高） | 3 / 2 / 2 / 2 |
| 第 6 个 | 任务句柄（身份证） | 暂不用，全 `NULL` |

### 1.3 栈大小为什么是"字"而不是字节？

FreeRTOS 是 1990 年代就有的老牌 RTOS，那时 RAM 按"字"算便宜。1 字 = 4 字节，所以 `512` 实际是 **2 KB**。

> **🤔 为什么 Sensors 要 2 KB 那么大？**
> 它内部要调 `MAX30102_ReadData()`，里面有个 `uint8_t raw[192]` 的本地数组。192 字节 + 函数调用链 + 中断嵌套，差不多要 1.8 KB。给 2 KB 是踩着上限但不浪费。

### 1.4 任务创建的内部流程

```
xTaskCreate(vTaskSensors, "Sensors", 512, NULL, 3, NULL)
   │
   ├─► 从 configTOTAL_HEAP_SIZE (本项目是 15 KB) 里"切"出:
   │     - 一个 TCB (Task Control Block, 任务控制块, 约 100 字节)
   │     - 一块 512 字的栈内存
   │
   ├─► 把 vTaskSensors 塞进 TCB
   ├─► 把"已就绪"标记塞进优先级 3 的就绪链表
   │
   └─► 返回 pdPASS 或 errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY
```

**4 次招聘下来，总共吃掉**：

| 项 | 字节数 |
|----|--------|
| 4 × TCB (~100 字节) | ~400 |
| Sensors + Display 栈 2 KB × 2 | 4096 |
| Voice + BT 栈 1.5 KB × 2 | 3072 |
| 内核杂项 (链表、就绪位图等) | ~1 KB |
| 空闲任务 + 钩子 | ~500 |
| **小计** | **~9 KB** |
| **本项目 `configTOTAL_HEAP_SIZE`** | **15 KB** |
| **剩余** | ~6 KB（OLED/算法等仍可用） |

> **🤔 栈溢出会怎样？**
> 见第 7 章：本项目开了 `configCHECK_FOR_STACK_OVERFLOW = 1`，会触发 `vApplicationStackOverflowHook`，UART1 打出 `STACK OVERFLOW: Sensors` 后死循环，4 秒后看门狗复位。

---

## 第 2 章：谁的活儿先干 —— 优先级与调度

### 2.1 优先级数字约定

| 值 | 含义 |
|----|------|
| `0` | 最低（空闲任务就这级别） |
| `1` ~ `configMAX_PRIORITIES-1` | 普通员工 |
| 越大越优先 | FreeRTOS 唯一约定 |

本项目配的是 `configMAX_PRIORITIES = 5`，所以优先级范围是 `0~4`。

### 2.2 调度器是"按优先级抢"的

FreeRTOS 默认是 **抢占式 (preemptive)** —— 一旦出现更高优先级的任务"就绪"（能跑了），调度器立刻让当前任务**让出 CPU**，让高优先级的先跑。

本项目的优先级设计：

```
优先级 3:  Sensors   ← 经理 (因为 MAX30102 FIFO 等不及)
优先级 2:  Display   ┐
           Voice     ├─ 普通员工
           BT        ┘
优先级 1:  (无)
优先级 0:  空闲任务   ← 老板不在时看大门的
```

### 2.3 一次 200ms 内 4 任务的交错时序

```
时间轴 (ms)  0       50      100     150     200
             │       │       │       │       │
Sensors (P3) ████████████░░░░░░░░░░░░████████████░
Display (P2) ░░██░░░░██░░░░██░░░░██░░░░██░░░░████░
Voice   (P2) ░░░░░░░░░░░░██░░░░░░░░░░░░██░░░░░░░░░
BT      (P2) ░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░██░

图例: █=在跑  ░=被调度器挂起 (在等 vTaskDelay)
```

**发生了什么？**

1. **t=0**：所有 4 个任务刚被 `vTaskStartScheduler()` 唤醒。P3 最高，先跑 Sensors。
2. **t≈5ms**（示意图）：Sensors 读 FIFO、调算法、写体温、读加速度，然后 `vTaskDelay(200)` —— 它主动让出 CPU，自己去睡觉。
3. **t≈5ms 起**：P2 三个任务开始抢。BT 周期最短（20ms），所以它跑得最频繁；Display (50ms) 和 Voice (100ms) 交替。
4. **t=200ms**：Sensors 睡醒，重新成为"最高优先级且就绪"的任务，立刻**抢回 CPU**。
5. **周而复始。**

### 2.4 同优先级下谁先跑？—— 时间片轮转

三个 P2 任务同优先级时，FreeRTOS **按时间片轮转**。一个时间片默认 = 1 个 tick（1ms）。也就是说：

> 三个 P2 任务看起来是"一起跑"，其实是各跑 1ms 就让出，按顺序循环。

> **🤔 为什么 Sensors 优先级 = 3？**
> MAX30102 的 FIFO 是 32 槽、100Hz 采样，约 320ms 就塞满。如果 Sensors 排到 2 级，跟别的同优先级抢着跑，万一某个任务卡了（比如 I2C 总线被锁），FIFO 就溢出丢数据。给到 3 级，**保证它睡醒时第一时间抢到 CPU**，把数据救出来。

### 2.5 "就绪"和"运行"和"阻塞"三种状态

```
┌─────────┐  vTaskDelay   ┌─────────┐
│ 运行中   │ ──────────► │ 阻塞    │ (在等时间/事件)
│ Running │              │ Blocked │
└────▲────┘              └────┬────┘
     │    调度器选中         │ 延时到/事件触发
     │                       ▼
┌────┴────┐  vTaskSuspend  ┌─────────┐
│ 就绪    │ ◄──────────── │ 挂起    │ (被人叫醒)
│ Ready   │              │ Suspend │
└─────────┘              └─────────┘
```

- **就绪**：能跑，但当前 CPU 被别人占着
- **运行**：CPU 正在执行我
- **阻塞**：在等一个时间/事件，主动放弃 CPU
- **挂起**：被人用 `vTaskSuspend()` 强行叫停

`vTaskDelay()` 就是让任务从 **运行 → 阻塞**，阻塞 200ms 后自动回到 **就绪**。

---

## 第 3 章：报时钟的脉搏 —— `SysTick`、`vTaskDelay`、`pdMS_TO_TICKS`

### 3.1 SysTick：调度器的心跳

FreeRTOS 跑起来后，靠一颗叫 **SysTick** 的硬件定时器（STM32 内部都有）每 1ms 中断一次，调度器就借着这一次次"嘀嗒"来：

- 把"睡眠任务"的时间戳减 1ms
- 时间到了就把任务扔回"就绪"列表
- 看看是不是该换人了

> **🤔 SysTick 是什么硬件？**
> ARM Cortex-M3 核内部自带一个 24 位倒计时定时器，FreeRTOS 移植层（`port.c`）会接管它。我们不用写任何初始化代码，调 `vTaskStartScheduler()` 时它就自动跑起来了。

### 3.2 `configTICK_RATE_HZ` 决定心跳速度

`FreeRTOSConfig.h` 第 14 行：

```c
#define configTICK_RATE_HZ    ( ( TickType_t ) 1000 )    // 1000Hz = 1ms 一次
```

| 值 | 一次 tick | 影响 |
|----|----------|------|
| `100` | 10ms | 任务切换粒度粗，省电，但延时精度差 |
| `1000` | **1ms（本项目）** | 精度 1ms，常见平衡点 |
| `10000` | 0.1ms | 切换开销大，浪费 CPU |

### 3.3 `vTaskDelay()`：主动让出 CPU 一段时间

`vTaskSensors` 末尾：

```c
vTaskDelay(pdMS_TO_TICKS(200));
```

`pdMS_TO_TICKS(200)` 是个**编译期宏**：

```c
#define pdMS_TO_TICKS( ms )   ( ( TickType_t ) ( ( ms ) * configTICK_RATE_HZ / 1000U ) )
// 1000Hz 下: 200 * 1000 / 1000 = 200 ticks = 200ms
```

> **🤔 为什么不用 `HAL_Delay(200)`？**
> `HAL_Delay()` 是裸机死等，期间 SysTick 中断照样来，但**任务函数根本不会被换下 CPU**。其他任务都得等这 200ms 干耗。`vTaskDelay()` 是**非阻塞**地"挂起 200ms"，期间调度器让别人跑。

### 3.4 `Delay_us` 和 `Delay_ms`：另一种延时

`crouse.c/systick.c` 里有：

```c
void Delay_us(uint32_t us) {
    uint32_t start  = DWT->CYCCNT;
    uint32_t target = us * (SystemCoreClock / 1000000u);   // 72 cycles/µs
    while ((DWT->CYCCNT - start) < target) {}
}
```

这是 **DWT 周期计数器的忙等延时**，跟 FreeRTOS 一点关系没有。什么时候用？

- FreeRTOS **调度器还没启动**的初始化阶段（`main()` 里 `I2C_HAL_Init` 之前）
- 1-Wire 这种时序极严的协议（DS18B20 要 15µs 拉低拉高，精度高）

> **⚠️ 千万别在任务函数里用 `Delay_ms()`！**
> 那会"霸占 CPU 整整 N 毫秒"，其他任务全部被卡死。所有延时场景都应该用 `vTaskDelay()`。

### 3.5 `Systick_GetTick()` 的两套实现

```c
uint32_t Systick_GetTick(void) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        return (uint32_t)xTaskGetTickCount();          // 调度器活着 → 用 FreeRTOS tick
    }
    return DWT->CYCCNT / (SystemCoreClock / 1000u);    // 否则 → 用 DWT
}
```

**为什么需要两套？**

- 调度器启动后，`xTaskGetTickCount()` 是个 32 位单调递增计数器，**不会溢出**（49.7 天才到顶）
- 没启动时用 DWT 是因为 `xTaskGetTickCount` 函数体根本还没链接进去（有些 INCLUDE 宏没开），且 DWT 是硬件寄存器，启动阶段最简单可靠

> DWT 在 72MHz 下每 1ms 计 72000 个 cycle，约 **59.6 秒**会绕回 0。但启动阶段撑死几百 ms，没问题。

---

## 第 4 章：共享资源的洗手间 —— 互斥锁 `Mutex`

### 4.1 为什么要互斥？

本项目里 **I2C1 总线**（PB8/PB9）上挂了 **2 个设备**：

- **MAX30102**（心率血氧，地址 0x57）
- **MPU6050**（加速度+温度，地址 0x68）

两个传感器由两个不同任务在读（Sensors 任务读 MAX30102 时可能也想读 MPU6050 内部温度）。如果两个"读操作"撞在一起——比如 Sensors 在发 MAX30102 的寄存器地址，刚发到一半，BT 任务也想读 MPU6050——I2C 总线上的电平就全乱了。

**互斥锁 = 一次只准一个任务进洗手间。**

### 4.2 三把锁

| 锁 | 变量 | 保护的资源 | 持有者 |
|----|------|-----------|--------|
| I2C1 锁 | `g_i2c_mutex` | I2C1 总线 (MAX30102 + MPU6050) | Sensors |
| I2C2 锁 | `g_i2c2_mutex` | I2C2 总线 (OLED) | Display |
| OLED 锁 | `g_oled_mutex` | OLED 帧缓冲区 | Display (本项目内唯一写者) |

### 4.3 `i2c_hal.c` 里的加锁/解锁

```c
static int i2c_lock(SemaphoreHandle_t mtx) {
    if (!mtx) return -1;
    return (xSemaphoreTake(mtx, pdMS_TO_TICKS(100)) == pdTRUE) ? 0 : -1;
}

static void i2c_unlock(SemaphoreHandle_t mtx) {
    if (mtx) xSemaphoreGive(mtx);
}

static int i2c_xfer(I2C_TypeDef *i2c, SemaphoreHandle_t mtx, ...) {
    int ret = -1;
    if (i2c_lock(mtx) != 0) return -1;       // ① 上锁
    I2C_GenerateSTART(i2c, ENABLE);          // ② 操作共享资源
    if (i2c_wait(...)) goto out;
    ...
out:
    i2c_unlock(mtx);                          // ③ 放锁
    return ret;
}
```

模式永远是：**上锁 → 干活 → 放锁**。和洗手间完全一样。

### 4.4 锁竞争时序图

```
任务 A (Sensors)            I2C1 总线         任务 B (假设有)
     │                         │                    │
     │ ① 拿锁 (mutex=1)        │                    │
     │◄────────────────────────┤                    │
     │                         │                    │
     │ ② START + 写 MAX30102    │                    │
     │ 等待 ADDR 标志...         │                    │
     │                         │                    │
     │  想再读 MPU6050         │                    │
     │  调 i2c_xfer(... 同一把锁)                    │
     │  i2c_lock(等待)          │                    │
     │  ━━━━━━━━━━━━━━━━━━━▶   │                    │
     │                         │                    │
     │  ③ 完成 MAX30102 读      │                    │
     │  ④ 放锁 (mutex=0)        │                    │
     │ ──────────────────────► │                    │
     │                         │   ⑤ 拿锁 (mutex=1) │
     │                         │ ◄━━━━━━━━━━━━━━━━━ │
     │                         │   ⑥ 写 MPU6050     │
     │                         │                    │
```

**关键点**：
- `xSemaphoreTake(mtx, pdMS_TO_TICKS(100))` 的 `100ms` 是**最大等待时间**。如果 100ms 内没拿到锁，函数返回 `pdFALSE`，外层会走总线复位
- **本项目里实际只有 Sensors 一个任务会操作 I2C1**，所以竞争场景少。但锁仍然必要——OLED 写时不希望被半路打断，且**代码的健壮性不依赖"现在只有一个使用者"**

### 4.5 互斥锁 vs 二值信号量

| 维度 | Mutex | 二值信号量 |
|------|-------|----------|
| 谁能放 | 必须是**拿锁的那个任务** | 任何任务/中断 |
| 用途 | "我独占这个资源" | "我告诉别人事件发生了" |
| 优先级继承 | ✅ 有 | ❌ 无 |
| 推荐场景 | 共享资源保护 | 任务间发信号 |

本项目用 Mutex 而非信号量，是因为**资源的所有权很清晰**——拿到锁的人保证是用完会放的。

### 4.6 OLED 锁的"防插队"

`oled_ssd1306.c` 第 50 行：

```c
return (xSemaphoreTake(g_oled_mutex, pdMS_TO_TICKS(100)) == pdTRUE) ? 0 : -1;
```

它锁的不是"物理 I2C"，而是**整个 `OLED_Show*` 函数序列**（Clear → DrawString → Flush）。为什么？

- 想象 Display 任务刚画到一半："心率: 72"，还没 Flush
- 突然 Voice 任务收到翻页命令，也想调 `OLED_ShowSteps(...)` 重画整个屏幕
- 两边同时操作帧缓冲区（`framebuf[8][128]`），结果就是"心率: 72\n 步数: 1234\n 心率: 7"这种乱码

锁住整个序列，Voice 任务必须等 Display 的"清屏→画字→Flush"完整走完再动。

> **🤔 为什么不直接锁住 I2C2 就行？**
> 因为 OLED 是在**双阶段**写的：先改 `framebuf`（无锁，CPU 内存访问），再 `Flush` 到 I2C2（有锁）。如果只锁 I2C2，两边可以同时改 `framebuf`，然后同时 Flush，顺序乱了。锁整个序列才能保证显示完整、不撕裂。

---

## 第 5 章：老板亲自出马 —— 临界区 `taskENTER_CRITICAL`

### 5.1 为什么需要临界区？

`g_temperature` 是 float（4 字节），`g_temp_valid` 是 int（4 字节）。Cortex-M3 上 4 字节对齐读本身是原子的，但**两个变量一起读**就可能撕裂：

```
Sensors 任务正在写：
  g_temp_valid  = 1        // 第一步: 把"有效"标志先置 1
  g_temperature = 36.5f    // 第二步: 写温度

Display 任务正在读：
  v = g_temp_valid          // 读到 1 ✓
  t = g_temperature          // 读到 0.0 ✗ (新值还没写完)
```

Display 就会显示"体温 = 0.0°C"这种荒谬值。

### 5.2 `taskENTER_CRITICAL` / `taskEXIT_CRITICAL` 的本质

```c
void Get_Temperature(int *valid, float *temp) {
    taskENTER_CRITICAL();   // 关中断 (或降到最低)
    *valid = g_temp_valid;
    *temp  = g_temperature;
    taskEXIT_CRITICAL();    // 开中断
}
```

`taskENTER_CRITICAL()` 实际上把 **BASEPRI 寄存器**设为某个值（参考 `FreeRTOSConfig.h` 第 38 行 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`），让 **优先级 ≤ 5 的中断**全部被屏蔽。

**屏蔽了什么？**

- SysTick 中断（优先级最低，默认 15）→ 调度器被冻结
- 任何优先级 5~15 的外设中断
- 任务切换本身（因为切换靠 SysTick）

**所以临界区里的代码必须极短**——长到 SysTick 都进不来，其他任务会感到"卡顿"。

### 5.3 临界区 vs 互斥锁

| 维度 | 临界区 | 互斥锁 |
|------|--------|--------|
| 粒度 | 几条指令 | 一段代码甚至一个完整操作 |
| 保护 | 数据一致性 | 资源独占 |
| 中断友好 | 关中断，**中断也进不来** | 互斥锁在 ISR 里也能用（API 后缀 FromISR） |
| 影响其他任务 | ❌ 全部阻塞 | ✅ 没抢到锁的可去睡觉 |
| 本项目用途 | 体温快照（2 个变量原子读） | I2C 总线、OLED 帧缓冲 |

### 5.4 本项目用临界区而非互斥锁的原因

临界区是"零开销锁"——既不申请内存，也不排队列。体温快照场景：

- 持续时间：3 条赋值语句，**几十纳秒**
- 竞争烈度：低
- 替代方案 mutex：需要 100ms 超时、申请信号量、可能切换任务——开销完全不值

> **⚠️ 千万不能调任何 FreeRTOS API 在临界区里！**
> 比如 `printf`、`xQueueSend`、`vTaskDelay` 都会触发调度，而调度器已死锁。要用就用 `xQueueSendFromISR` 这种 ISR-safe 版本。

---

## 第 6 章：贴在墙上的明信片 —— `volatile` 全局变量

### 6.1 本项目的"明信片"

```c
volatile float  g_temperature  = 0.0f;   // 皮肤+补偿后的体温
volatile int    g_temp_valid   = 0;      // 温度是否有效
volatile int    g_page_advance = 0;      // 语音/按键请求翻页
```

三张明信片，**三个写者，三个读者**：

| 变量 | 写者 | 读者 |
|------|------|------|
| `g_temperature` | Sensors 任务 | Display (页面3)、BT 任务 |
| `g_temp_valid` | Sensors 任务 | 同上 |
| `g_page_advance` | Voice 任务 (`g_page_advance++`)、按键 ISR | Display 任务 |

### 6.2 `volatile` 关键字解决什么问题？

裸机 C 语言里，你写：

```c
while (g_page_advance == 0) {
    // 啥也不做
}
```

**编译器**会以为 "`g_page_advance` 一直没人在写"，自作聪明地把它**优化成"只读一次"**：

```c
// 编译器优化后
if (g_page_advance == 0) {
    while (1) {}    // 死循环
}
```

而实际场景是：**Voice 任务在另一个任务里**会改 `g_page_advance`。CPU 没在循环里读内存，**永远看不到新值**，死锁。

加 `volatile` 后：

```c
while (g_page_advance == 0) {
    // 每次循环都重新从内存读 g_page_advance
}
```

### 6.3 `volatile` 不能解决什么

`volatile` 只保证"**每次读都从内存来、每次写都到内存去**"，但**不保证原子性**。

```c
g_page_advance = g_page_advance + 1;   // 读+改+写, 不是原子的
```

如果两个任务同时 `++`，可能一次 `+2` 中间有另一个 `+1` 进来，**丢一次增加**。本项目里这个变量只做"翻页触发"，被丢一次无所谓；真要严格计数，得用 FreeRTOS 的**原子宏**或**信号量**。

### 6.4 `g_temperature` 没用 `volatile`？—— 是的

看 `main.c` 第 34-36 行：

```c
volatile float  g_temperature  = 0.0f;   // ← 用了
volatile int    g_temp_valid   = 0;      // ← 用了
volatile int    g_page_advance = 0;      // ← 用了
```

**全部都用了**。因为：

- Sensors 任务和 Display 任务**优先级不同**，编译器如果只看到一个任务的代码，**会假设 `g_temperature` 永远不变**而缓存到寄存器
- 加了 `volatile`，每次读都重新访问内存
- 跨任务的所有共享变量都该加

### 6.5 `g_temperature` 整型读为什么"碰巧"原子

第 5 章说两个变量一起读会撕裂——但**单个 4 字节变量**的读本身是原子的。Cortex-M3 是 32 位架构，4 字节对齐读是一条 `ldr` 指令，**不可能读到一半被打断**。

所以：

```c
int valid; float t;
Get_Temperature(&valid, &t);    // 临界区: 一起读
// 等价于
taskENTER_CRITICAL();
valid = g_temp_valid;
t = g_temperature;
taskEXIT_CRITICAL();
```

如果只读一个 `g_temperature` 本身，**不加锁也是安全的**（Cortex-M3 上的巧合）。但为了"`valid` 标志和 `t` 值一致"这个语义，必须用临界区把它们一起读出来。

---

## 第 7 章：工厂的安全网 —— 栈溢出、断言、IWDG

FreeRTOS 给你三道安全防线，本项目全都用上了。

### 7.1 第一道：栈溢出检测

`FreeRTOSConfig.h` 第 24 行：

```c
#define configCHECK_FOR_STACK_OVERFLOW    1
```

值 `1` 表示用"**方法 1**"：每次任务切换时，检查栈顶一个"标记字"是否被覆盖。如果被覆盖了，说明栈溢出了。

`main.c` 第 184-195 行是钩子函数：

```c
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    UART1_Init(115200);                              // 强制 115200
    const char *prefix = "STACK OVERFLOW: ";
    UART1_Send((const uint8_t *)prefix, ...);
    if (pcTaskName) {
        UART1_Send((const uint8_t *)pcTaskName, ...);  // 打印任务名
    }
    UART1_Send((const uint8_t *)"\r\n", 2);
    for (;;) {}                                       // 死循环
}
```

**为什么死循环？** 因为死循环里没人喂狗，4 秒后 IWDG 会复位整个系统——比无限挂起要好，至少会自动恢复。

### 7.2 第二道：`configASSERT` 断言

`FreeRTOSConfig.h` 第 43 行：

```c
#define configASSERT( x )  do { if ((x) == 0) Assert_Handler(__FILE__, __LINE__); } while (0)
```

`Assert_Handler` 在 `main.c` 第 207-220 行：

```c
void Assert_Handler(const char *file, int line) {
    taskDISABLE_INTERRUPTS();          // 关中断
    UART1_Init(115200);                // 强制 115200
    const char *prefix = "ASSERT FAIL: ";
    UART1_Send(...);
    if (file) UART1_Send(file, ...);
    char buf[16];
    int n = snprintf(buf, sizeof(buf), ":%d\r\n", line);
    if (n > 0) UART1_Send(buf, n);
    for (;;) {}                        // 死循环 → IWDG 复位
}
```

**FreeRTOS 内部用 `configASSERT` 检查的参数**（不通过就崩溃）：

- 优先级 ≤ `configMAX_PRIORITIES`
- 堆分配成功
- 队列长度合法
- 中断优先级在允许范围内
- 等等

> **🤔 为什么不在 Assert_Handler 里用 `printf`？**
> `printf` 内部会调 `malloc`，而 `configASSERT` 失败时堆可能已经满了。所以本项目直接用底层 `UART1_Send` 字节级发。

### 7.3 第三道：IWDG 硬件看门狗

`crouse.c/iwdg.c` + `main.c` 第 287 行：

```c
IWDG_Init();                  // LSI 40kHz, /256, reload 625 ≈ 4s
vTaskStartScheduler();
```

`vTaskSensors` 第 67 行：

```c
for (;;) {
    IWDG_Feed();              // 喂狗: 把倒计时重置为 4s
    ...
}
```

**机制**：
- IWDG 是 STM32 内部独立硬件，**独立于主时钟**，即使主时钟挂了也能工作
- 倒计时到了没喂 → **产生系统复位**
- 4 秒内只要 Sensors 任务至少执行一次 `IWDG_Feed()`，就一切正常
- **故意不喂狗的场景**：`Assert_Handler` / `vApplicationStackOverflowHook` ——死循环 → 不喂狗 → 4 秒后复位

### 7.4 三道防线的协作

```
              出现 bug
                 │
                 ▼
        ┌────────────────┐
        │ 1. 栈溢出检测  │ ──► "STACK OVERFLOW: Sensors" → 死循环
        └────────────────┘                              │
                 │                                     │
                 ▼                                     │
        ┌────────────────┐                              │
        │ 2. configASSERT│ ──► "ASSERT FAIL: foo.c:42" → 死循环
        └────────────────┘                              │
                 │                                     │
                 ▼                                     │
        ┌────────────────┐                              │
        │ 3. IWDG 看门狗 │ ◄─────────────────────────────┘
        └────────────────┘
                 │ (4 秒不喂狗)
                 ▼
            芯片 RESET
                 │
                 ▼
            从 main() 重新开始
```

> **🤔 这不就是"崩溃-重启-崩溃-重启"？**
> 是的。但对于一个戴在手腕上的手环来说，**自动恢复 > 静默卡死**。重启比挂死安全。

---

## 第 8 章：自由王国也有宪法 —— `FreeRTOSConfig.h` 逐行解读

| 行号 | 宏 | 本项目值 | 含义 / 为什么这样选 |
|------|------|---------|---------------------|
| 10 | `configUSE_PREEMPTION` | `1` | 启用抢占调度。**关掉就成了协程**，高优先级任务不能打断低优先级。 |
| 13 | `configCPU_CLOCK_HZ` | `72000000` | 72 MHz，与 `SystemCoreClock` 一致。FreeRTOS 用它算 SysTick 重载值。 |
| 14 | `configTICK_RATE_HZ` | `1000` | 1ms 一次 SysTick。本项目任务延时都是 50/100/200ms 的整数倍。 |
| 15 | `configMAX_PRIORITIES` | `5` | 优先级 0~4。够用就好，每个优先级都要占一个就绪链表节点。 |
| 16 | `configMINIMAL_STACK_SIZE` | `128` | 空闲任务的栈（512 字节），其他任务在此基础上加。 |
| 17 | `configTOTAL_HEAP_SIZE` | `15 KB` | FreeRTOS 所有动态分配（任务栈、TCB、队列、信号量）都从这里出。 |
| 21 | `configIDLE_SHOULD_YIELD` | `1` | 同优先级下空闲任务主动让出时间片给其他同优先级任务。 |
| 22 | `configUSE_MUTEXES` | `1` | 启用互斥锁。**用 mutex 必须开**。 |
| 24 | `configCHECK_FOR_STACK_OVERFLOW` | `1` | 启用栈溢出检测（方法 1）。 |
| 25 | `configUSE_RECURSIVE_MUTEXES` | `1` | 启用递归互斥锁。本项目没用，但开着不亏（不占运行时间）。 |
| 26 | `configUSE_COUNTING_SEMAPHORES` | `1` | 启用计数信号量。本项目没用，同上。 |
| 27 | `configUSE_TIMERS` | `0` | **本项目关键决策**。软件定时器关掉，省 1.2 KB 堆。HC05 的 2 秒上报用 `last_report_tick` 轮询代替。 |
| 35 | `configPRIO_BITS` | `4` | STM32F1 的 NVIC 用 4 位表示中断优先级。 |
| 37-38 | `configLIBRARY_*_INTERRUPT_PRIORITY` | `0x0F` / `5` | **关键**：`FromISR` API 能被哪些中断调用。SysTick = 15 最低，>5 都不受影响。 |
| 43 | `configASSERT` | 自定义 | 把 FreeRTOS 内部断言失败转成 UART 输出 + 死循环 → IWDG 复位。 |
| 52 | `INCLUDE_xTaskGetSchedulerState` | `1` | **必须开**！`Systick_GetTick` 用 `xTaskGetSchedulerState()` 判断调度器是否启动。 |
| 54-64 | `INCLUDE_vTask*` / `INCLUDE_xTask*` | `1` | 把不需要的 API 设为 0 可以省代码体积；本项目用得着的全开。 |

### 8.1 内存预算明细

```
configTOTAL_HEAP_SIZE = 15 KB = 15360 字节
├─ 空闲任务栈 (MINIMAL_STACK_SIZE = 128 字 = 512 B)  512
├─ Sensors 任务栈                                     2048
├─ Display 任务栈                                     2048
├─ Voice 任务栈                                       1536
├─ BT 任务栈                                          1536
├─ 4 × TCB (任务控制块)                              ~400
├─ 3 × 信号量 (I2C1/I2C2/OLED mutex)                  ~300
├─ 链表 / 队列等内核结构                             ~500
├─ 实际剩余                                           ~6480
└─ 合计                                               ~15360 ✓
```

> **🤔 15 KB 怎么定的？**
> 算出来 ≈ 8.5 KB 必须，留 50% 余量给未来的队列/算法缓冲，定 15 KB 正好。
> STM32F103ZE 有 **64 KB RAM**，15 KB 占 23%。还有大把空间给 `algorithms.c` 里的 `ppg_ir[256]` 等静态数组（BSS 区，不走堆）。

### 8.2 `configUSE_TIMERS = 0` 的取舍

| 用软件定时器 | 不用软件定时器 (本项目) |
|------------|---------------------|
| `xTimerCreate("...", 2000, pdTRUE, ..., cb)` 即可周期回调 | 自己用 `last_report_tick = xTaskGetTickCount()` 在 `HC05_Process` 里轮询判断 |
| 自动管理回调、停止、复位 | 全手写 |
| 多了 Timer 任务（256 字栈 + TCB + 命令队列） ≈ 1.2 KB 堆开销 | 省 1.2 KB 堆，但代码要写 5~10 行 |
| **适合**：定时器多、改动频繁 | **适合**：本项目只有 1 个 2 秒定时任务 |

---

## 第 9 章：整张调度时序图 + FAQ

### 9.1 启动 → 稳态 完整时序

```
时间(ms)  0    50   100  150  200  250  300  350  400  450  500
          │    │    │    │    │    │    │    │    │    │    │
main()    ██████████████                                                ← 初始化
          │ SystemInit/时钟/总线/传感器/外设
          │ xTaskCreate × 4
          │ IWDG_Init
          ▼
vTaskStartScheduler()
          │
          ▼ (SysTick 接管, 1ms 中断)
Sensors  ░░████████████████████████████████████░░░░░░████████████
          │ (200ms 周期)
Display  ░░█░░█░░█░░█░░█░░█░░█░░█░░█░░█░░█░░█░░█░░█░░█░░█░░█░░
          │ (50ms 周期)
Voice    ░░░░░░██░░░░░░██░░░░░░██░░░░░░██░░░░░░██░░░░░░██░░░░░░
          │ (100ms 周期)
BT       ░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█░█
          │ (20ms 周期)
          │
          │  ↑ Sensors 睡醒抢回 CPU  (优先级 3 vs 2)
          │  ↑
          │  ↑ 这一刻: Display/Voice/BT 同时挂起, 等 Sensors 让出
          ▼
```

### 9.2 资源争抢场景

```
t = 120ms
─────────────────────────────────────────────────
Sensors (P3) ── 读到一半, 申请 I2C1 锁, 成功
                │
                │  (此时 BT 任务也想发 JSON, 但不需要 I2C1)
                │  (Display 想翻页, 用 I2C2, 跟 I2C1 无关)
                │
                ▼
              Sensors 完成 MAX30102 读, 释放 I2C1 锁
─────────────────────────────────────────────────
t = 120ms 期间, 实际执行序列:
  Sensors 读 MAX30102  (持有 I2C1 锁)         ~3ms
  Sensors 读 MPU6050   (再次申请 I2C1 锁)      ~2ms
  Sensors 写 g_temperature, g_temp_valid       (临界区, 几条指令)  ~1µs
  Sensors 调 vTaskDelay(200)                   → 让出 CPU
  Display (P2) 抢到 CPU, 画心率页              ~5ms
  BT (P2) 抢到, 发送 JSON 报告                  ~3ms
  ...
```

### 9.3 FAQ：常见疑问 10 连

#### Q1：FreeRTOS 是操作系统吗？

**A**：严格说算"实时内核"——有任务调度、信号量、队列，但没文件系统、网络协议栈、用户态。和 Linux 不是一个量级，但单片机场景下比 Linux 实时得多、占用小得多。

#### Q2：为什么用 RTOS 而不是 `while(1) + 状态机`？

**A**：状态机能解决"顺序执行多个状态"，但解决不了"等待一个事件时 CPU 不被浪费"。RTOS 的 `vTaskDelay` 和信号量就是干这事的——等待时 CPU 干别的，事件到了自动回来。

#### Q3：xTaskCreate 的优先级数字大还是小优先？

**A**：**大优先**。`configMAX_PRIORITIES = 5` 时，4 比 0 优先。

#### Q4：同优先级下，几个任务怎么"排队"？

**A**：时间片轮转。本项目用 1ms 一个时间片，三个 P2 任务轮流跑。**A 跑 1ms → B 跑 1ms → C 跑 1ms → A 跑 1ms → ...**

#### Q5：为什么 Sensors 优先级 = 3 而不是 4？

**A**：技术上 4 也行。但 FreeRTOS 推荐"够用就好"——给最高优先级会"挤占"低优先级任务的执行机会。Sensors 只要比 P2 高就行。

#### Q6：`vTaskDelay(0)` 是什么意思？

**A**：让出当前时间片。相当于"我不睡，但让别人先跑"。本项目没用，但很常用。

#### Q7：互斥锁 vs 信号量 vs 队列 怎么选？

**A**：**互斥锁**：独占一个资源；**信号量**：发一个事件通知；**队列**：传一段数据。本项目里三个全用——mutex 保护资源、volatile 全局变量当"旗语"通知翻页。

#### Q8：为什么 `xSemaphoreTake(mtx, 100ms)` 这种写法？

**A**：100ms 是"最坏情况下的等待时间"。超过 100ms 拿不到锁，说明系统出问题了（死锁、总线被锁）。本项目把它当"看门狗"用：100ms 还没拿到 → 总线复位。

#### Q9：IWDG 和 FreeRTOS 的关系？

**A**：IWDG 是 STM32 内部硬件，独立于 FreeRTOS。**它只看一个东西：4 秒内是否有人喂狗。** 哪怕 FreeRTOS 调度器完全挂了，只要 Sensors 任务还在跑 `IWDG_Feed()`，IWDG 就不会复位。

#### Q10：调试时怎么观察任务状态？

**A**：
```c
// 1. uxTaskGetStackHighWaterMark: 查任务栈剩余最小值
//    返回值 = 0 说明曾经贴着用, 危险; 返回值 < 50 说明不够
// 2. vTaskList(): 打印所有任务的当前状态 (需开启 trace facility)
// 3. uxTaskGetSystemState(): 程序化查询
```

本项目用第一种：每次改任务栈大小时，调 `uxTaskGetStackHighWaterMark(NULL)` 看返回值。

---

## 附录：本项目 FreeRTOS 关键代码速查

| 想知道 | 看哪里 |
|--------|--------|
| 4 个任务长什么样 | `crouse.c/main.c` 第 58-179 行 |
| 任务怎么创建 | `crouse.c/main.c` 第 277-281 行 |
| I2C 互斥锁怎么用 | `crouse.c/i2c_hal.c` 第 53-62、154、186 行 |
| OLED 互斥锁怎么用 | `crouse.c/oled_ssd1306.c` 第 50-55、85 行 |
| 体温原子读 | `crouse.c/main.c` 第 41-47 行 |
| 栈溢出钩子 | `crouse.c/main.c` 第 184-195 行 |
| 断言钩子 | `crouse.c/main.c` 第 207-220 行 |
| IWDG 喂狗 | `crouse.c/main.c` 第 67 行 |
| 任务延时 | `crouse.c/main.c` 第 103、150、162、177 行 |
| FreeRTOS 配置 | `crouse.h/FreeRTOSConfig.h` |

---

## 写在最后

**学习 FreeRTOS 的关键不是 API，而是"心智模型"**：

1. **任务就是死循环函数**（第 1 章）
2. **CPU 同一时刻只能跑一个任务**，"同时"是高速切换的拟态（第 2 章）
3. **共享资源必须用 mutex 保护**（第 4 章）
4. **跨任务传简单标志用 volatile 全局**（第 6 章）
5. **跨任务传数据用队列**（本项目没用，但要知道）
6. **任何等待用 vTaskDelay/信号量，绝不用裸 Delay**（第 3 章）
7. **健壮的系统 = 任务 + mutex + 临界区 + 栈检测 + 断言 + 看门狗**（第 7 章）

把这 7 条刻进脑子，看任何 RTOS 项目都心里有底。

---

**文档版本**：v1.0
**配套项目**：智能健康手环 (STM32F103ZE + FreeRTOS)
**配套 IDE**：Keil MDK-ARM V5
**写作日期**：2026-06-22
