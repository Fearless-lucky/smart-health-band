# 代码文件简化重构设计

- 日期: 2026-06-25
- 分支: Simple
- 目标: 在不影响功能的前提下，全面精简应用层代码——既做文件组织与命名重构（合并文件、去名不符实的 `_hal`），也做代码内容层面的简化（删死代码、减过度封装、减过度解读注释、减无意义中间层），让项目更清爽、更像一人手写逻辑，便于移植。
- 约束: 不改变任何运行时行为（引脚、寄存器、算法系数、任务结构、协议全部保持原样）。本次不推送 git。

## 1. 现状与问题

应用层 `crouse.c/` 15 个 .c + `crouse.h/` 24 个 .h，合计 39 个文件（另根目录有 2 个 FCARM 残留）。主要冗余：

1. **I2C 文件双份并存**：`i2c.c/h`（游离，未加入 Keil 工程、未跟踪）与 `i2c_hal.c/h`（工程实际编译）内容几乎逐字相同，仅头文件名与初始化函数名不同（`I2C1_Init` vs `I2C_HAL_Init`）。`i2c` 是未完工的改名副本。
2. **每个传感器三件套**：max30102 / mpu6050 / ds1302 / ds18b20 各有 `.c + .h + _config.h` 三个文件，config.h 与 .h 可合并。
3. **config.h 混职责**：`max30102_config.h`、`mpu6050_config.h`、`ds18b20_config.h` 既含传感器硬件配置（I2C 地址/引脚/寄存器），又含算法参数（`PPG_*`/`STEP_*`/`TEMP_*`），而算法参数是 `algorithms.c` 在用、传感器驱动自己不用。这让算法层通过 `#include "xxx_config.h"` 间接耦合到传感器。
4. **命名名不符实**：`i2c_hal`、`uart_hal` 带 `_hal` 后缀，但项目用的是 StdPeriph 库不是 HAL 库。
5. **小文件碎片**：`keys.c/h`、`iwdg.c/h` 各几十行且只被 main.c 引用；`globals.h` 15 行只做全局变量声明。
6. **字库分散**：`font5x7.h`、`font_cn_12x12.h`、`font_cn_16x16.h` 三个文件，只 OLED 用，且中文只有 20 个字。
7. **FCARM 残留**：`Net_Config.h`、`fcarm_dummy.h`、`Auto_FcArm_Cmd.inp` 是 Keil FCARM 文件系统转换器副产品，项目已禁用 FCARM（见提交记录 `Fix: 完全移除 FCARM 配置块` 等），工程不再引用，`fcarm_dummy.h` 内嵌的恰是游离 `i2c.h`。

## 2. 重构决策（已与用户逐项确认）

| 项 | 决定 |
|---|---|
| 算法参数 `PPG_*/STEP_*/TEMP_*` | 从传感器 config.h 移到 `algorithms.h`，传感器 .h 只留硬件配置 |
| 游离 `i2c.c/h` | 删除 |
| `i2c_hal.c/h` | 重命名为 `i2c.c/h`，`I2C_HAL_Init()` → `I2C1_Init()`（与 `I2C2_Init()` 对称） |
| 传感器三件套 | 全合并为 `.c + .h` 两件，每个传感器一套 |
| `keys.c/iwdg.c` | 并入 `main.c`；`globals.h` 保留（跨任务共享状态契约） |
| `systick.c/h` | 保留独立（11 个模块共用的延时基础设施，不可并入单点） |
| `uart_hal.c/h` | 重命名为 `uart.c/h` |
| `hc05.c/h`、`asr_pro.c/h` | 保持独立（各有独立协议逻辑） |
| 字库 3 个 | 合并为 1 个 `font.h` |
| FCARM 残留 3 个 | 一并删除 |
| `algorithms.c/h` | 不拆分（PPG/SpO2/计步/温度补偿内聚良好），仅吸收算法参数宏 |
| `gen_docx.js`/`verify_docx.js`/`package.json`/`node_modules/`/`项目技术文档.*` | 文档工具，与代码重构无关，不动 |

## 3. 目标文件清单

### 3.1 `crouse.c/`（15 → 12 个）

```
删除 3 个: i2c.c(游离, 与 i2c_hal.c 同名合并)、i2c_hal.c(改名 i2c.c)、keys.c、iwdg.c
（i2c.c 游离副本删除 + i2c_hal.c 改名为 i2c.c = 净减 1，加 keys.c/iwdg.c 删除 = 共减 3）
保留/改名 11 个:
  main.c          ← 吸收 keys.c + iwdg.c + globals.h
  i2c.c           ← 由 i2c_hal.c 改名 (I2C_HAL_Init→I2C1_Init)
  uart.c          ← 由 uart_hal.c 改名
  algorithms.c    ← 不变
  max30102.c      ← 不变 (含 #include 改名)
  mpu6050.c       ← 不变
  ds18b20.c       ← 不变
  ds1302.c        ← 不变
  oled_ssd1306.c  ← 不变 (含 #include 字库改名)
  hc05.c          ← 不变
  asr_pro.c       ← 不变
  systick.c       ← 不变
```

### 3.2 `crouse.h/`（24 → 15 个）

```
删除 10 个:
  i2c.h(游离)、i2c_hal.h、keys.h、iwdg.h
  max30102_config.h、mpu6050_config.h、ds18b20_config.h、ds1302_config.h
  font5x7.h、font_cn_12x12.h、font_cn_16x16.h  → 三合一为 font.h
保留/改名 15 个:
  algorithms.h    ← 吸收 PPG_*/STEP_*/TEMP_* 算法参数宏
  i2c.h           ← 由 i2c_hal.h 改名 (I2C_HAL_Init→I2C1_Init 声明)
  uart.h          ← 由 uart_hal.h 改名
  max30102.h      ← 吸收 max30102_config.h 硬件配置部分 (I2C地址/寄存器)
  mpu6050.h       ← 吸收 mpu6050_config.h 硬件配置部分 (I2C地址/量程/温度系数)
  ds18b20.h       ← 吸收 ds18b20_config.h 硬件配置部分 (引脚)
  ds1302.h        ← 吸收 ds1302_config.h 硬件配置部分 (引脚)
  oled_ssd1306.h  ← 不变
  hc05.h          ← 不变
  asr_pro.h       ← 不变
  systick.h       ← 不变
  globals.h       ← 保留 (跨任务共享状态契约, 被 main/asr_pro/hc05/oled 4 处 include)
  FreeRTOSConfig.h ← 不变
  font.h          ← 由 font5x7 + font_cn_12x12 + font_cn_16x16 合并
```

### 3.3 根目录游离文件

```
删除 2 个 FCARM 残留: fcarm_dummy.h、Auto_FcArm_Cmd.inp
（注: Net_Config.h 位于 crouse.h/ 下, 已计入 3.2 删除项）
```

**净变化：crouse.c 15→12、crouse.h 24→14、根目录删 2，合计 41→26 个文件。**

## 4. 关键迁移规则

### 4.1 算法参数迁移（最重要，决定分层边界）

从三个 config.h 抽出算法参数宏，集中放入 `algorithms.h`：

- `max30102_config.h` → `algorithms.h`：`PPG_*` 全部、`SPO2_*` 全部、`HR_MAX_DELTA`
- `mpu6050_config.h` → `algorithms.h`：`STEP_*` 全部、`ACTIVITY_*` 全部
- `ds18b20_config.h` → `algorithms.h`：`TEMP_*` 全部

留在传感器 .h 的硬件配置：
- `max30102.h`：`MAX30102_I2C_ADDR`、`MAX30102_SPO2_CONFIG`、`MAX30102_LED1_CURRENT`、`MAX30102_LED2_CURRENT`、`MAX30102_FIFO_CONFIG`
- `mpu6050.h`：`MPU6050_I2C_ADDR`、`MPU6050_ACCEL_RANGE`、`MPU6050_TEMP_OFFSET`、`MPU6050_TEMP_SCALE`、`MPU6050_TEMP_FALLBACK`
- `ds18b20.h`：`DS18B20_PORT`、`DS18B20_PIN`
- `ds1302.h`：`DS1302_CLK_PORT/PIN`、`DS1302_DAT_PORT/PIN`、`DS1302_CE_PORT/PIN`

迁移后 `algorithms.c` 改为 `#include "algorithms.h"` 自给自足，不再 `#include` 任何 `xxx_config.h`，算法层与传感器驱动解耦。

### 4.2 include 改名清单

| 原引用 | 新引用 | 影响文件 |
|---|---|---|
| `i2c_hal.h` | `i2c.h` | main.c、max30102.c、mpu6050.c、oled_ssd1306.c、i2c.c 自身 |
| `uart_hal.h` | `uart.h` | main.c、hc05.c、asr_pro.c、uart.c 自身 |
| `max30102_config.h` | `algorithms.h`（拿算法参数） | algorithms.c、hc05.c、main.c |
| `mpu6050_config.h` | `algorithms.h` | algorithms.c、main.c |
| `ds18b20_config.h` | `algorithms.h` | algorithms.c |
| `ds1302_config.h` | `ds1302.h` | ds1302.c |
| `keys.h` / `iwdg.h` | 删除；`keys.c`/`iwdg.c` 实现并入 main.c | main.c 吸收实现；这两个头无其他引用者 |
| `globals.h` | **保留不删** | 被 main/asr_pro/hc05/oled 4 处 include，是跨任务共享状态（g_temperature/g_temp_valid/g_page_advance）的 extern 契约，删了需在每个 .c 重复 extern，反而不清爽 |
| `font5x7.h` / `font_cn_12x12.h` / `font_cn_16x16.h` | `font.h` | oled_ssd1306.c |

注意：`max30102.c` 同时需要硬件配置（地址/寄存器，来自 `max30102.h`）和算法喂入接口（`PPG_ProcessSamples`，来自 `algorithms.h`），故 max30102.c 仍同时 include 两者，但不再 include `max30102_config.h`。

### 4.3 函数改名

- `I2C_HAL_Init()` → `I2C1_Init()`：定义在 i2c.c，声明在 i2c.h，调用点在 main.c（`main.c:233`）。

### 4.4 字库合并与死字库删除

合并为单个 `font.h`，内部顺序：
1. `#include <stdint.h>` + 头文件守卫 `__FONT_H`
2. `font5x7[95][5]`（ASCII 5x7）
3. `CN_*` 枚举（原 font_cn_16x16.h 中定义，保留——12x12 字库索引仍用它）
4. `font_cn_12[CN_COUNT][24]`（12x12 中文，主页在用）

**删除 16x16 中文字库与绘制函数**（审计确认零调用，属死代码）：
- 删 `oled_ssd1306.c` 中 `oled_draw_cn`、`oled_draw_cn_str` 两个函数（仅 16x16 路径使用）
- 删 `font_cn[CN_COUNT][32]`（16x16 字模数据，原 font_cn_16x16.h）
- `oled_ssd1306.c` 的 `#include "font_cn_16x16.h"` 一并去掉
- **注意**：`CN_*` 枚举保留，因 `font_cn_12` 仍以它为索引；`oled_draw_cn12` / `oled_draw_cn12_str`（12x12）保留，主页在用。

原 `font_cn_12x12.h` 中的 `#include "font_cn_16x16.h"` 去掉（已同文件）。

### 4.5 main.c 吸收内容

- `globals.h`：**保留**。它只含 `g_temperature`/`g_temp_valid`/`g_page_advance` 的 extern 声明 + `Get_Temperature()` 声明，定义本就在 main.c。但它被 asr_pro.c/hc05.c/oled_ssd1306.c 三个非 main 文件 include 以拿到 extern 声明，是跨任务共享状态的契约头，删除会导致这三处需重复 extern，违背清爽目标。
- `keys.c`：`Keys_Init()`、`Key_Get()`、内部 `key_read()` 搬入 main.c
- `iwdg.c`：`IWDG_Init()`、`IWDG_Feed()` 搬入 main.c

合并后 main.c 会吸收约 80 行，但仍远小于 oled_ssd1306.c/algorithms.c，可接受。

## 5. 代码内容简化（中等尺度）

除文件合并外，对代码内容做以下简化。尺度：删死代码 + 清冗余注释 + 安全内联/换库；**不**做提取 `center_x`、**不**统一 `read_reg` 风格（避免新增间接层/改逻辑）。

### 5.1 确定的死代码删除（零风险，grep 确认零调用）

| 位置 | 删除内容 | 证据 |
|---|---|---|
| oled_ssd1306.c | `oled_draw_cn` + `oled_draw_cn_str` + `#include "font_cn_16x16.h"` | 16x16 路径全项目零调用，主页用 12x12 |
| crouse.h/font_cn_16x16.h | `font_cn[CN_COUNT][32]` 字模（并入 font.h 时不再收录） | 同上，死数据 |
| hc05.c / hc05.h | `HC05_SendData` 函数 + 声明 | 全项目零调用，内部直接用 `UART1_Send` |
| i2c_hal.c / i2c.h | `I2C2_ReadReg` 函数 + 声明 | OLED 只写不读，零调用 |
| max30102.c:108-110 | `if (to_read > sizeof(raw)) to_read = sizeof(raw);` 截断分支 | `samples & 0x1F` 已限 0~31，×6 ≤ 192，永不触发 |

### 5.2 不实注释删除

| 位置 | 问题 | 处理 |
|---|---|---|
| oled_ssd1306.c `OLED_ShowTemperature` 注释 | 称"消除 main.c/asr_pro.c/ShowMainPage 三处重复"，实际 ShowMainPage 不调它 | 删除该不实描述（函数体自说明） |
| oled_ssd1306.c `OLED_ShowTime` 注释 | 称"消除 main.c/asr_pro.c 两处重复"，实际 main.c 不调 | 删除该不实描述 |
| oled_ssd1306.c `OLED_ShowActivity` 注释 | "统一带锁"是所有 Show* 共性，非本函数特点 | 删除该冗余说明 |

### 5.3 注释清理（激进尺度：只留三类 why）

**判定准则**：只保留以下三类注释，其余一律删除——

1. **算法 why**：algorithms.c 中解释信号处理逻辑原因的注释（环形缓冲为何替代 memmove、滞回峰值检测为何抗抖、步态连续性确认为何过滤孤立扰动、EMA 系数选值、信号质量门控为何要锁定计数、变化率限幅为何防跳变等）。
2. **传输 why**：I2C/UART/1-Wire/DS1302 时序与协议相关注释（I2C 总线错误为何 reset、读 SR2 清 ADDR、重映射原因、UART 每字节独立超时为何、RX 上拉防噪声、DS18B20 命令字 0xCC/0x44 含义、DS1302 CH 位为何要清、HC-05 AT/自动波特率/JSON 协议、ASR 命令码是固件约定不可改等）。
3. **踩坑 why**：记录"不这么做就会出错"的陷阱注释（OLED framebuf 互斥锁防两 prio2 任务抢花屏、DS18B20 CRC 校验、IWDG 4s 超时选值理由、step_armed 清零防误计、shaking_energy 必须 float 防 uint 截断等）。

**全部删除**：
- 装饰框：`/* ======== 内部/公开 API ======== */`、main.c `===== 第N步 =====` 等。
- 函数名已自说明的 what 注释：`oled_hline` 上方"画一条水平线"、`oled_setpixel` 上方"设置单个像素"、`OLED_ShowTime` 行尾"时间页: 大号时间显示"等。
- 显而易见的行尾/算术注释：`/* 0~31 (5-bit FIFO 深度) */`、`/* 32 × 6 = 192 */`、`/* 读取失败默认值 */` 等。
- 引脚/寄存器位含义复述：DS1302/DS18B20 enum 行里重复解释引脚作用、mpu6050 enum 行"读到 0x42=8字节"等。
- main.c 任务职责描述块（`任务: 传感器采集(200ms,优先级3)... 轮询读取...`）——函数体已自说明。
- DS1302 SelfTest 内逐行英文标签（`/* test communication */` `/* WP off */` 等）——保留 2-3 段关键踩坑说明即可。
- 返回码行尾注释（ds18b20.c `return -1; /* CRC 校验失败 */`）——头文件已记载返回码含义。

**保留示例**（三类 why，实现时逐条对照，宁可保守不删）：
- algorithms.c:25-28 step_armed 为何提到文件作用域的踩坑说明
- algorithms.c:29-36 步态连续性确认的 why
- algorithms.c:102-104 显式环形缓冲替代 memmove 的 why
- algorithms.c:228-235 信号质量门控为何要锁定计数
- algorithms.c:335-337 shaking_energy 必须 float 的踩坑
- i2c_hal.c:43-45 总线错误 AF/BERR/ARLO 的 why
- i2c_hal.c:141 重映射原因
- uart_hal.c:24-25 每字节独立超时 why
- uart_hal.c:68-69 RX 上拉防噪声 why
- ds18b20.c:131-132 0xCC/0x44 命令字含义
- ds1302.c:84-86 CH 位清零 why
- oled_ssd1306.c:20-31 framebuf 互斥锁防花屏（可压缩到 4-6 行，保留踩坑核心）
- hc05.c:65 OERR 清除 why

> 说明：此尺度比初版 5.3 更激进——OLED 互斥锁设计、main 任务说明、引脚复述等非算法/非传输/非踩坑的注释现在也清。判定三问：①解释算法原因？②解释传输时序/协议？③不这么做会踩坑？三者皆否则删。

### 5.4 安全内联 / 换库

| 位置 | 简化 | 理由 |
|---|---|---|
| oled_ssd1306.c 四处手写 `uint8_t len=0; while(s[len])len++;` | 改用 `<string.h>` 的 `strlen` + `(uint8_t)` 强转 | 文件已 include string.h，省 4 处重复循环 |
| hc05.c `float temp_out = valid ? temp : 0.0f;` | 内联进 snprintf：`(double)(valid ? temp : 0.0f)` | 中间变量只用一次 |
| oled_ssd1306.c `write_data` static helper | 内联进 `OLED_Flush`（唯一调用点） | 为对称而封装，只一处用 |
| i2c_hal.c `i2c_lock` / `i2c_unlock` static helper | 内联进 `i2c_xfer`（唯一调用点，前后各 2 行） | 单调用点 helper |

**不做**（避免新增间接层，与"像人写的"一致）：
- 不提取 `center_x()` helper（4 处居中算式保留直白写法）
- 不统一 max30102 `read_reg` 风格（混用无碍可读性）

### 5.5 自审与验证补充

代码内容简化后，验证需额外确认：
- grep 全仓无 `oled_draw_cn\b`（16x16，非 12）、`HC05_SendData`、`I2C2_ReadReg` 残留。
- grep 确认 `font_cn\[`（16x16 数组）无引用，`font_cn_12[`（12x12）仍被 `oled_draw_cn12` 引用。
- 编译后 0 error / 0 warning，烧录功能与重构前一致（见第 9 节验证方式）。

## 6. Keil 工程文件同步

`TASK1.uvprojx` / `TASK1.uvoptx` 需同步修改文件引用：

1. 移除 `<FilePath>` / `<FilePath>` 中：`i2c_hal.c/.h`、`uart_hal.c/.h`、`keys.c/.h`、`iwdg.c/.h`、`max30102_config.h`、`mpu6050_config.h`、`ds18b20_config.h`、`ds1302_config.h`、`globals.h`、`font5x7.h`、`font_cn_12x12.h`、`font_cn_16x16.h`
2. 新增/改名引用：`i2c.c/.h`、`uart.c/.h`、`font.h`
3. 其余 .c/.h 引用路径不变（max30102.c 等文件名未变）

> 注：uvprojx/uvoptx 当前已是 modified 状态（git status 显示 M），本次重构会进一步修改它们。这是预期的。

## 7. 不做的事（YAGNI 边界）

- 不改任何引脚分配、寄存器值、算法系数、任务优先级/周期、协议格式。
- 不拆分 `algorithms.c`（内聚良好，拆开反而碎）。
- 不合并 `hc05` 与 `asr_pro`（各自有独立协议逻辑，揉一起更乱）。
- 不动 `systick`（基础设施，11 处共用）。
- 不动 FreeRTOS 内核、StdPeriph 驱动、Device/、Core/、RTE/。
- 不动文档工具链（gen_docx.js 等）。
- 不提取 `center_x`、不统一 `read_reg`（见 5.4，避免新增间接层）。
- 不重写算法逻辑（algorithms.c 信号处理代码原样保留，只做 include 改名 + 按 5.3 删非算法注释）。
- 本次不推 git、不提交。

## 8. 验证方式

无 CLI 构建脚本，验证依赖 Keil MDK：

1. 重构后在 Keil MDK 打开 `TASK1.uvprojx`，确认工程文件列表与磁盘一致、无红色缺失文件。
2. Rebuild 全量编译，确认 0 error / 0 warning（与重构前基线一致）。
3. 烧录后跑通：OLED 五页显示、语音指令翻页、蓝牙 JSON 上报、按键翻页、体温/心率/血氧/计步读数与重构前一致。
4. 因无自动化测试，验证以"编译通过 + 烧录跑通原有功能"为准。

## 9. 风险与缓解

| 风险 | 缓解 |
|---|---|
| 算法参数宏迁移时漏搬或重名 | 迁移后 grep 全仓确认无 `PPG_/STEP_/TEMP_/SPO2_/HR_/ACTIVITY_` 残留在传感器 .h；确认 `algorithms.c` 不再 include 任何 `*_config.h` |
| include 改名遗漏导致编译失败 | 按 4.2 清单逐文件改，改完 grep 残留 `i2c_hal\|uart_hal\|_config.h\|keys.h\|iwdg.h\|globals.h\|font5x7\|font_cn_` |
| uvprojx 文件项错配 | 同步第 6 节清单，改完在 Keil 里目视核对 |
| 字库合并后数组重名 | font5x7 / font_cn_12 两个数组名保持不变，只换宿主文件；删 font_cn(16x16) 后确认无引用 |
| 死代码删除误删 | 5.1 每项均经 grep 确认零调用；删后 grep 复核无残留引用 |
| 注释清理误删三类 why | 5.3 判定三问（算法？传输？踩坑？）三者皆否则删；拿不准时保守保留，宁可多留不误删 |
| 功能回归 | 不改任何运行时逻辑，纯文件搬迁 + 命名 + 死代码/注释清理；编译+烧录双重验证 |
