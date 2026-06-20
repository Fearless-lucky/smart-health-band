## 第6章 从加速度到步数：计步算法

### 6.1 MPU6050 能告诉我们什么？

MPU6050 内部有一个微小的质量块，通过测量质量块受力后的位移来感知加速度。静止时它只能测到重力加速度（指向地心，约 1g ≈ 16384 LSB）。

```
           Z轴(上下)
            ↑
            │  走路时：X/Y/Z 都会随步伐摆动
            │  静止时：只有 Z ≈ 1g (重力)
            │
            └────→ Y轴(左右)
           ╱
          ╱
         X轴(前后)
```

### 6.2 分离重力：什么是"真正的手部运动"

加速度计读到的原始数据包含两部分：

```
原始加速度 = 重力加速度(1g, 方向固定) + 运动加速度(手腕摆动)
```

我们要计步，需要的是**运动加速度**，不是重力。

```c
// 重力估计：长时间平均趋近于重力方向
if (grav == 0.0f) grav = mag;   // 第一次：直接用当前值初始化
grav = 0.98f * grav + 0.02f * mag;  // 之后：慢慢追踪

// 运动加速度 = 总加速度 - 重力
float acc = mag - grav;
```

**类比**：你坐在一辆加速的汽车里，身体感到的"推背感"来自汽车加速，而不是重力。`grav` 就像汽车匀速行驶时的感觉，`acc` 就是加速/刹车时的额外感觉。

`0.98` 这个系数意味着重力估计每次只更新 2%——它变化很慢，像一个"迟钝的朋友"，不会被短暂的甩手动作迷惑。

### 6.3 自适应阈值：为什么不用固定值检测步态

```c
thr = 0.995f * thr + 0.005f * fabsf(acc);
```

**类比**：阈值像一个"自动调节的感应门"。如果进来的人都很高（大步走），门开得大；如果都是小孩（小步走），门开得小。`0.995` 意味着阈值变化非常缓慢。

```c
// 第一步检测
if (acc > thr && (now - last_step) > 400) {
    //  加速度超过阈值     上次步子距今 > 400ms (防连触发)
    steps++;            // 计一步
    last_step = now;    // 记录时间
    // ...记录步间距...
}
```

**为什么最少间隔 400ms？** 正常人跑步最高步频约 180 步/分钟 = 3 步/秒 ≈ 333ms/步。400ms 相当于 150 步/分钟的上限，足以过滤掉所有"假步子"（甩手、震动）。

### 6.4 运动状态分类

```c
if (idle_time > 2000) {
    // 超过 2 秒没走路
    if (shaking_energy > 400)   activity_state = ACTIVITY_SHAKING;  // 在晃但没走路
    else                        activity_state = ACTIVITY_UNKNOWN;  // 静止了
} else if (step_hist_cnt >= 3) {
    // 最近有至少 3 步，可以计算步频
    float spm = 60000.0f / avg_interval;  // 步/分钟
    if (spm > 120.0f)           activity_state = ACTIVITY_RUNNING;   // >120步/分=跑步
    else if (spm > 40.0f)       activity_state = ACTIVITY_WALKING;   // 40-120步/分=走路
    else                        activity_state = ACTIVITY_UNKNOWN;
}
```

**晃动能量的概念**：即使没在走路，手腕乱晃时加速度也会剧烈变化。`shaking_energy` 是加速度变化的累加值——如果这个值很大但又没有规律性步频，说明在"乱甩"而不是走路。

---

## 第7章 皮肤温度 → 体温：补偿算法

### 7.1 为什么手腕温度 ≠ 体温？

你测手腕温度只有 33°C，但实际体温是 36.5°C。这三度的差值来自：

```
核心体温 (36.5°C)
    │
    │  热量通过血液、组织传导
    │
    ▼
皮肤表面温度 (32-34°C)
    │
    │  受环境温度影响
    │
    ▼
环境温度 (25°C 室内 / 30°C 室外)
```

手腕皮肤是身体的"散热器"——与核心体温之间始终存在温差。

### 7.2 补偿公式怎么来的

```c
float Compensate_Temperature(float raw_temp, float ambient_temp)
{
    // 环境温差 = 当前环境温度 - 参考温度(25°C)
    float delta = ambient_temp - 25.0f;

    // 补偿量随环境温度变化
    float offset = 2.5f - 0.05f * delta;
    // 25°C 环境: offset = 2.5 - 0.05×0  = 2.5°C  (基础补偿)
    // 30°C 环境: offset = 2.5 - 0.05×5  = 2.25°C (天热了，温差变小)
    // 20°C 环境: offset = 2.5 - 0.05×(-5) = 2.75°C (天冷了，温差变大)

    // 安全范围
    if (offset < 1.0f) offset = 1.0f;   // 最少补 1°C
    if (offset > 3.5f) offset = 3.5f;   // 最多补 3.5°C

    return raw_temp + offset;
}
```

**环境温度从哪来？** MPU6050 芯片在运行时自身会微微发热，读取它的片内温度传感器（`MPU6050_ReadTemp()`），作为环境温度的参考。虽然不是精确的室温，但相对变化能反映环境温度趋势。

---

## 第8章 蓝牙通信怎么实现

### 8.1 HC-05 是什么？

HC-05 是一个蓝牙串口透传模块。对它来说，蓝牙连接 = 无线串口线。你在手机上发的数据，它原封不动地用 UART 传给 STM32。STM32 发的数据，它原封不动地通过蓝牙发给手机。

```
STM32  PA9(TX) ──→ HC-05 RX ── 蓝牙 ──→ 手机 APP
       PA10(RX) ←── HC-05 TX ←── 蓝牙 ←── 手机 APP
```

### 8.2 初始化：自动波特率探测

```c
void HC05_Init(uint32_t baud)
{
    // ① 先尝试用户指定的波特率（默认 9600）
    if (hc05_try_baud(baud) == 0) return;  // 成功了

    // ② 没成功？尝试其他常见波特率
    static const uint32_t fallbacks[] = {38400, 115200, 57600, 19200};
    for (int i = 0; i < 4; i++) {
        if (hc05_try_baud(fallbacks[i]) == 0) {
            // ③ 找到了！发 AT 命令把波特率改回用户指定的值
            snprintf(cmd, sizeof(cmd), "AT+UART=%lu,0,0\r\n", baud);
            UART1_Send((const uint8_t *)cmd, (uint16_t)n);
            Delay_ms(200);
            UART1_Init(baud);   // 重新初始化 USART 为新波特率
            return;
        }
    }
    // ④ 全失败了——用用户指定的波特率硬上
    UART1_Init(baud);
}
```

**为什么需要自动探测？** 你不知道上一个使用者把 HC-05 设成了什么波特率。这段代码像"万能钥匙"——先试你的钥匙，不行就换 4 把候补钥匙，找到锁芯后用 AT 命令换成你的钥匙。

### 8.3 主循环：收发处理

```c
void HC05_Process(void)
{
    // ① 清除 UART 溢出错误 — 这是关键一步！
    if (USART_GetFlagStatus(USART1, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(USART1);  // 读一次清掉 ORE 标志
    }
    // 什么是 ORE？如果硬件收到新字节但程序还没读走上一个字节，
    // 硬件会置 ORE 标志并停止接收！不清除的话后续数据全丢。

    // ② 逐字节接收，遇到换行表示一条命令结束
    while (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET) {
        char c = (char)USART_ReceiveData(USART1);
        if (c == '\n' || c == '\r') {
            // 收到换行 → 处理整条命令
            rxbuf[rxi] = '\0';     // 字符串结尾

            if (strncmp(rxbuf, "CAL", 3) == 0) {
                // CAL 110 25 → 校准血氧
                float A = 110.0f, B = 25.0f;
                sscanf(rxbuf + 3, "%f %f", &A, &B);
                Set_SpO2_Calibration(A, B);
            } else if (strncmp(rxbuf, "TIME", 4) == 0) {
                // TIME 14:30:00 21/05/26 → 设置时钟
                int h, m, s, d, mo, y;
                sscanf(rxbuf + 4, "%d:%d:%d %d/%d/%d", &h, &m, &s, &d, &mo, &y);
                rtc_time_t tm;
                tm.hour = (uint8_t)h; tm.min = (uint8_t)m; tm.sec = (uint8_t)s;
                tm.day = (uint8_t)d; tm.month = (uint8_t)mo; tm.year = (uint8_t)y;
                DS1302_WriteTime(&tm);
            }
            rxi = 0;  // 缓冲区索引归零，准备下一条命令
        } else {
            if (rxi < sizeof(rxbuf) - 1) rxbuf[rxi++] = c;
        }
    }

    // ③ 定时上报 JSON 数据 (每 2 秒)
    uint32_t now = Systick_GetTick();
    if ((now - last_report_tick) >= 2000) {
        last_report_tick = now;
        // 组装 JSON: {"hr":72,"spo2":98,"steps":1234,"temp":36.50}
        int n = snprintf(txbuf, sizeof(txbuf),
            "{\"hr\":%d,\"spo2\":%d,\"steps\":%d,\"temp\":%.2f}\r\n",
            Get_HeartRate(), Get_SpO2(), Get_StepCount(),
            (double)(g_temp_valid ? g_temperature : 0.0f));
        UART1_Send((uint8_t *)txbuf, (uint16_t)n);
    }
}
```

### 8.4 主循环为什么是 20ms？

```
9600 波特率 ≈ 每秒收 960 字节 ≈ 每毫秒收 1 字节
128 字节接收缓冲区 / 1 字节每毫秒 ≈ 128ms 才会溢出
20ms 周期 → 最多堆积 20 字节，远小于 128 字节缓冲区
```

如果周期设为 200ms，蓝牙可能发来 200 字节而缓冲区只有 128 字节——数据就丢了。20ms 给了足够的余量。

### 8.5 JSON 数据格式

```json
{"hr":72,"spo2":98,"steps":1234,"temp":36.50}
```

每个字段的含义：
- `hr`: 心率，单位 BPM（每分钟心跳数）
- `spo2`: 血氧饱和度，单位 %
- `steps`: 累计步数
- `temp`: 体温，单位 °C（数据无效时为 0.00）

手机端只需要一个蓝牙串口 APP 就能接收这些数据。任何支持 BLE 串口的应用都能对接这个手环。
