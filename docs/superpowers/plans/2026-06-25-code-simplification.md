# 代码简化重构实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不影响功能的前提下，把应用层 41 个文件精简到 25 个，并做代码内容层面的死代码删除、注释清理、安全内联，让项目更清爽、更像一人手写。

**Architecture:** 两层工作。第一层文件组织：传感器三件套合并为 .c+.h、算法参数迁入 algorithms.h 解耦分层、i2c/uart 去 `_hal` 改名、keys/iwdg/globals 并入 main.c、字库三合一（删 16x16 死字库）、清 FCARM 残留。第二层代码内容：删 5 处确定死代码、按"算法/传输/踩坑三类 why"清注释、安全内联 4 处 helper。改完同步 Keil 工程文件，最终由用户在 Keil MDK 编译+烧录验证。

**Tech Stack:** STM32F103ZE + FreeRTOS + StdPeriph 库，Keil MDK 5 (ARMCLANG AC6)，纯 C。无 CLI 构建、无自动化测试——每步用 grep 残留检查 + 结构核对验证，最终编译/烧录为人工关卡。

**关键约束（每个任务都要遵守）:**
- 不改任何运行时逻辑（引脚、寄存器值、算法系数、任务结构、协议）。
- 注释只保留三类 why：①算法原因 ②传输时序/协议 ③不这么做会踩坑。其余删。拿不准时保守保留。
- 每个任务结束做 grep 残留检查，确认无遗漏。
- 本次不 `git push`。每个任务末尾可 commit（用户未要求提交，故 commit 步骤标注为"可选"——默认不提交，除非用户说提交）。

**设计文档:** [2026-06-25-code-simplification-design.md](../specs/2026-06-25-code-simplification-design.md)

---

## 文件结构总览（改后）

### crouse.c/（15 → 11）
| 文件 | 动作 | 职责 |
|---|---|---|
| i2c.c | 由 i2c_hal.c 改名+内联 lock/unlock | I2C1/I2C2 底层 |
| uart.c | 由 uart_hal.c 改名 | UART1/UART2 底层 |
| algorithms.c | include 改名 + 注释清理 | PPG/SpO2/计步/温度补偿 |
| max30102.c | include 改名 + 删死分支 + 注释清理 | PPG 传感器 |
| mpu6050.c | include 改名 + 注释清理 | IMU 传感器 |
| ds18b20.c | include 改名 + 注释清理 | 1-Wire 温度 |
| ds1302.c | include 改名 + 注释清理 | RTC |
| oled_ssd1306.c | include 改名 + 删16x16死代码 + 换strlen + 内联write_data + 注释清理 | OLED 显示 |
| hc05.c | 删 HC05_SendData + 内联temp_out + 注释清理 | 蓝牙 |
| asr_pro.c | 注释清理 | 语音 |
| main.c | 吸收 keys.c/iwdg.c/globals.h + include 改名 + 注释清理 | 主程序+任务 |
| systick.c | 注释清理（极小） | 延时工具 |

删除：i2c.c(游离)、i2c_hal.c、keys.c、iwdg.c

### crouse.h/（24 → 14）
| 文件 | 动作 |
|---|---|
| i2c.h | 由 i2c_hal.h 改名 + 删 I2C2_ReadReg 声明 |
| uart.h | 由 uart_hal.h 改名 |
| algorithms.h | 吸收 PPG_*/STEP_*/TEMP_*/SPO2_*/HR_*/ACTIVITY_* 宏 |
| max30102.h | 吸收 max30102_config.h 硬件配置部分 |
| mpu6050.h | 吸收 mpu6050_config.h 硬件配置部分 |
| ds18b20.h | 吸收 ds18b20_config.h 硬件配置（引脚） |
| ds1302.h | 吸收 ds1302_config.h 硬件配置（引脚） |
| font.h | 由 font5x7.h + font_cn_12x12.h 合并（删 font_cn_16x16.h） |
| oled_ssd1306.h / hc05.h / asr_pro.h / systick.h / FreeRTOSConfig.h | 不变 |

删除：i2c.h(游离)、i2c_hal.h、keys.h、iwdg.h、globals.h、4个_config.h、3个字库.h

### 根目录
删除：fcarm_dummy.h、Auto_FcArm_Cmd.inp

---

## Task 1: 算法参数迁移到 algorithms.h（解耦分层基础）

**Files:**
- Modify: `crouse.h/algorithms.h`
- Delete (内容迁出后): `crouse.h/max30102_config.h`、`crouse.h/mpu6050_config.h`、`crouse.h/ds18b20_config.h` 的**算法参数部分**（本任务只迁移，不删文件——文件删除在 Task 5 合并传感器时一起做）

**目标：** 把三个 config.h 里的算法参数宏（PPG_*/STEP_*/TEMP_*/SPO2_*/HR_*/ACTIVITY_*）集中到 algorithms.h，让 algorithms.c 不再依赖任何 `*_config.h`。

- [ ] **Step 1: 读取三个 config.h 确认算法参数边界**

Run: `cat crouse.h/max30102_config.h crouse.h/mpu6050_config.h crouse.h/ds18b20_config.h`
Expected: 确认 PPG_*/SPO2_*/HR_MAX_DELTA（max30102）、STEP_*/ACTIVITY_*（mpu6050）、TEMP_*（ds18b20）是要迁移的算法参数；MAX30102_I2C_ADDR/SPO2_CONFIG/LED_*/FIFO_CONFIG、MPU6050_I2C_ADDR/ACCEL_RANGE/TEMP_*、DS18B20_PORT/PIN 是留传感器的硬件配置。

- [ ] **Step 2: 把算法参数宏追加到 algorithms.h**

在 `crouse.h/algorithms.h` 的 `#endif` 之前、最后一个函数声明之后，插入以下三组宏（从对应 config.h 原样复制，保留原有的中文注释——这些是算法 why 注释，按 5.3 保留）：

```c
/* ============================================================
 * PPG 算法参数 (原 max30102_config.h) — algorithms.c 使用
 * ============================================================ */
#define PPG_SAMPLE_RATE            100.0f
#define PPG_BUF_LEN                256
#define PPG_WINDOW_SIZE            200
#define PPG_MIN_SAMPLES            50
#define PPG_LP_CUTOFF              5.0f
#define PPG_HP_CUTOFF              0.5f
#define PPG_EMA_ALPHA              0.15f
#define PPG_PEAK_THRESH_RATIO      0.8f
#define PPG_PEAK_MIN_DIST_S        0.4f
#define PPG_PERIOD_MIN_S           0.3f
#define PPG_PERIOD_MAX_S           2.0f
#define PPG_PEAK_HISTORY_MAX       8
#define PPG_STD_ACTIVE_MIN         0.2f
#define PPG_BAD_HOLD_MAX           3
#define HR_MAX_DELTA               10
#define SPO2_MAX_DELTA             2
#define PPG_IR_DC_MIN              5000.0f
#define PPG_LOCK_COUNT             4

/* SpO2 校准 */
#define SPO2_CAL_A                 110.0f
#define SPO2_CAL_B                 25.0f
#define SPO2_CLAMP_MIN             50
#define SPO2_CLAMP_MAX             100

/* ============================================================
 * 计步算法参数 (原 mpu6050_config.h) — algorithms.c 使用
 * ============================================================ */
#define STEP_GRAVITY_EMA           0.98f
#define STEP_THRESH_EMA            0.995f
#define STEP_INIT_THRESH           500.0f
#define STEP_SETTLE_COUNT          10
#define STEP_MIN_INTERVAL_MS       300
#define STEP_MIN_THRESH            200.0f
#define STEP_HISTORY_LEN           8
#define STEP_VALID_WINDOW_MS       3000
#define STEP_CONFIRM_MIN           2

/* 运动状态分类 */
#define ACTIVITY_SHAKE_ENERGY      400
#define ACTIVITY_IDLE_MS           2000
#define ACTIVITY_WALK_SPM_MIN      40.0f
#define ACTIVITY_RUN_SPM_MIN       120.0f

/* ============================================================
 * 温度补偿参数 (原 ds18b20_config.h) — algorithms.c 使用
 * ============================================================ */
#define TEMP_BASE_OFFSET           3.0f
#define TEMP_AMBIENT_REF           25.0f
#define TEMP_AMBIENT_COEFF         0.05f
#define TEMP_OFFSET_MIN            1.5f
#define TEMP_OFFSET_MAX            4.0f
```

注：原 config.h 里每个宏的行尾中文注释（如 "Hz"、"环形缓冲区大小"）按 5.3 判定——这些是简短参数说明，多数属"显而易见"，但在算法参数上下文里有助理解，**保留原 config.h 里的注释**。复制时带上原注释。上面代码块为简洁未全标，实际复制时以 config.h 原文为准（含行尾注释）。

- [ ] **Step 3: 改 algorithms.c 的 include**

修改 `crouse.c/algorithms.c` 第 1-10 行 include 区：

```c
#include "algorithms.h"
#include "systick.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
```

删除原第 2-4 行的 `#include "max30102_config.h"`、`#include "mpu6050_config.h"`、`#include "ds18b20_config.h"`（现在算法参数已在 algorithms.h 内）。

- [ ] **Step 4: grep 验证 algorithms.c 不再依赖 config.h**

Run: `grep -n "config.h" crouse.c/algorithms.c`
Expected: 无输出（空）。若有残留，删干净。

Run: `grep -nE "PPG_|STEP_|TEMP_|SPO2_|HR_MAX|ACTIVITY_" crouse.h/algorithms.h`
Expected: 能看到所有迁移过来的宏。

- [ ] **Step 5（可选）: Commit**

```bash
git add crouse.h/algorithms.h crouse.c/algorithms.c
git commit -m "Refactor: 算法参数宏迁入 algorithms.h, 解耦算法层与传感器"
```

---

## Task 2: 传感器三件套合并（max30102/mpu6050/ds18b20/ds1302）

**Files:**
- Modify: `crouse.h/max30102.h`、`crouse.h/mpu6050.h`、`crouse.h/ds18b20.h`、`crouse.h/ds1302.h`
- Delete: `crouse.h/max30102_config.h`、`crouse.h/mpu6050_config.h`、`crouse.h/ds18b20_config.h`、`crouse.h/ds1302_config.h`
- Modify: `crouse.c/max30102.c`、`crouse.c/mpu6050.c`、`crouse.c/ds18b20.c`、`crouse.c/ds1302.c`（include 改名）

**目标：** 每个传感器只留 .c+.h；硬件配置宏并入各自 .h；config.h 删除。

- [ ] **Step 1: 合并 max30102.h**

把 `crouse.h/max30102_config.h` 里**仅剩的硬件配置部分**（Task 1 已迁走算法参数）追加到 `crouse.h/max30102.h` 的 `#endif` 之前。此时 max30102_config.h 里应只剩：

```c
#define MAX30102_I2C_ADDR          (0x57 << 1)  /* 0xAE */
#define MAX30102_SPO2_CONFIG       0x47   /* ADC=4096, SR=100Hz, PW=411us */
#define MAX30102_LED1_CURRENT      0x50   /* IR LED 电流 ~16mA */
#define MAX30102_LED2_CURRENT      0x50   /* Red LED 电流 ~16mA */
#define MAX30102_FIFO_CONFIG       0x10   /* 滚动覆盖, 不平均 */
```

把这些（含行尾注释——寄存器值含义属传输 why，保留）复制进 max30102.h。max30102.h 顶部原有 `/* 通信: I2C1... */` 注释保留（传输 why）。

- [ ] **Step 2: 合并 mpu6050.h**

把 `crouse.h/mpu6050_config.h` 剩余硬件配置追加到 `crouse.h/mpu6050.h`：

```c
#define MPU6050_I2C_ADDR           (0x68 << 1)  /* 0xD0 */
#define MPU6050_ACCEL_RANGE        0x00   /* ±2g */
#define MPU6050_TEMP_OFFSET        36.53f /* °C 偏移 */
#define MPU6050_TEMP_SCALE         340.0f /* LSB/°C */
#define MPU6050_TEMP_FALLBACK      25.0f  /* 读取失败默认值 */
```

- [ ] **Step 3: 合并 ds18b20.h**

把 `crouse.h/ds18b20_config.h` 剩余硬件配置追加到 `crouse.h/ds18b20.h`。ds18b20_config.h 此时只剩引脚宏 + `#include "stm32f10x.h"`：

```c
#define DS18B20_PORT               GPIOA
#define DS18B20_PIN                GPIO_Pin_1
```

注意：ds18b20.h 需在顶部加 `#include "stm32f10x.h"`（原 config.h 里有，引脚宏依赖它）。ds18b20.h 原有 `/* 通信: 1-Wire... 用法: 先 StartConversion()... */` 注释保留（传输 why）。

- [ ] **Step 4: 合并 ds1302.h**

把 `crouse.h/ds1302_config.h` 剩余硬件配置追加到 `crouse.h/ds1302.h`：

```c
#define DS1302_CLK_PORT            GPIOB
#define DS1302_CLK_PIN             GPIOPin_1
#define DS1302_DAT_PORT            GPIOB
#define DS1302_DAT_PIN             GPIO_Pin_14
#define DS1302_CE_PORT             GPIOB
#define DS1302_CE_PIN              GPIO_Pin_13
```

注意：ds1302.h 需在顶部加 `#include "stm32f10x.h"`。原 `rtc_time_t` 结构体和函数声明保留。**复制时核对原 config.h 的确切宏名**（如 `GPIO_Pin_1` 不要误写成 `GPIOPin_1`——上面 DS1302_CLK_PIN 行是示意，实际以原文件为准）。

- [ ] **Step 5: 改各传感器 .c 的 include**

四个 .c 文件，把 `#include "xxx_config.h"` 删掉（硬件配置已在各自 .h 里，.c 已 include 自己的 .h）：

- `crouse.c/max30102.c`：删 `#include "max30102_config.h"`（第2行）。保留 `#include "max30102.h"`、`#include "i2c_hal.h"`（i2c 改名在 Task 4，本任务先不动）、`#include "systick.h"`、`#include "algorithms.h"`、`#include <string.h>`。
- `crouse.c/mpu6050.c`：删 `#include "mpu6050_config.h"`（第2行）。保留 `#include "mpu6050.h"`、`#include "i2c_hal.h"`、`#include "systick.h"`。
- `crouse.c/ds18b20.c`：删 `#include "ds18b20_config.h"`（第2行）。保留其余。
- `crouse.c/ds1302.c`：删 `#include "ds1302_config.h"`（第8行）。保留其余。

- [ ] **Step 6: 删除四个 config.h 文件**

Run:
```bash
rm crouse.h/max30102_config.h crouse.h/mpu6050_config.h crouse.h/ds18b20_config.h crouse.h/ds1302_config.h
```

- [ ] **Step 7: grep 验证**

Run: `grep -rn "_config.h" crouse.c/ crouse.h/`
Expected: 无输出。

Run: `ls crouse.h/*config.h 2>/dev/null`
Expected: 无输出（文件已删）。

Run: `grep -n "MAX30102_I2C_ADDR\|MPU6050_I2C_ADDR\|DS18B20_PORT\|DS1302_CLK_PORT" crouse.h/max30102.h crouse.h/mpu6050.h crouse.h/ds18b20.h crouse.h/ds1302.h`
Expected: 各自 .h 能 grep 到对应宏。

- [ ] **Step 8（可选）: Commit**

```bash
git add -A crouse.h/ crouse.c/
git commit -m "Refactor: 传感器三件套合并为 .c+.h, 删除 4 个 _config.h"
```

---

## Task 3: 字库合并为 font.h + 删 16x16 死字库

**Files:**
- Create: `crouse.h/font.h`
- Delete: `crouse.h/font5x7.h`、`crouse.h/font_cn_12x12.h`、`crouse.h/font_cn_16x16.h`
- Modify: `crouse.c/oled_ssd1306.c`（include 改名，但本任务只改 include；删 16x16 绘制函数在 Task 6）

**目标：** font5x7 + CN_* 枚举 + font_cn_12 三合一为 font.h；font_cn（16x16）死数据不收录。

- [ ] **Step 1: 创建 font.h**

新建 `crouse.h/font.h`，内容按顺序：

```c
#ifndef __FONT_H
#define __FONT_H

#include <stdint.h>

/* 5x7 ASCII 字库, ASCII 32..126 (95 glyphs), 每字 5 字节, 索引 = ascii - 32 */
static const uint8_t font5x7[95][5] = {
    /* ... 从 font5x7.h 原样复制全部 95 行字模 ... */
};

/* 12x12 中文字库索引枚举 (原 font_cn_16x16.h 定义, 12x12 复用) */
enum {
    CN_CHA = 0,   /* 查 */
    CN_KAN,       /* 看 */
    CN_XIN,       /* 心 */
    CN_LV,        /* 率 */
    CN_BU,        /* 步 */
    CN_SHU,       /* 数 */
    CN_GUI,       /* 归 */
    CN_LING,      /* 零 */
    CN_XUE,       /* 血 */
    CN_YANG,      /* 氧 */
    CN_TI,        /* 体 */
    CN_WEN,       /* 温 */
    /* ... 从 font_cn_16x16.h 复制完整枚举 (共 20 个) ... */
    CN_COUNT
};

/* 12x12 中文字模, 每字 24 字节, 索引与 CN_* 枚举一致 */
static const uint8_t font_cn_12[CN_COUNT][24] = {
    /* ... 从 font_cn_12x12.h 原样复制全部字模 ... */
};

#endif /* __FONT_H */
```

**执行要点：**
- `font5x7[95][5]` 数组：打开 `crouse.h/font5x7.h`，把 `static const uint8_t font5x7[95][5] = { ... };` 整块复制进 font.h（含全部 95 行字模数据）。
- `CN_*` 枚举：打开 `crouse.h/font_cn_16x16.h`，把 `enum { ... CN_COUNT };` 整块复制（含 20 个枚举值 + 行尾汉字注释——这些是数据说明，保留）。
- `font_cn_12[CN_COUNT][24]` 数组：打开 `crouse.h/font_cn_12x12.h`，把 `static const uint8_t font_cn_12[CN_COUNT][24] = { ... };` 整块复制。**去掉**原文件第 5 行 `#include "font_cn_16x16.h"`（已同文件）。
- **不要复制** `font_cn[CN_COUNT][32]`（16x16 数组）——死数据，丢弃。
- 注释：font5x7 顶部"5x7 pixel font..."属数据说明，压成一行保留；font_cn_12 顶部"12x12 中文字库..."同理保留 1 行。

- [ ] **Step 2: 改 oled_ssd1306.c 的字库 include**

修改 `crouse.c/oled_ssd1306.c` 第 7-9 行：

原：
```c
#include "font5x7.h"
#include "font_cn_16x16.h"
#include "font_cn_12x12.h"
```
改为：
```c
#include "font.h"
```

（本任务先不删 16x16 绘制函数，Task 6 统一处理死代码。include 改名后 oled_draw_cn 仍引用 font_cn[]——但 font.h 已无 font_cn 数组，**会导致 Task 6 前编译失败**。因此本任务必须和 Task 6 连续执行，或在本任务里临时保留 font_cn 数组。**推荐：本任务 Step 1 暂时也把 font_cn[CN_COUNT][32] 复制进 font.h，Task 6 删绘制函数时一并删该数组**——这样每个任务结束都能编译。

修正 Step 1：font.h 也复制 `font_cn[CN_COUNT][32]`（16x16 数组），放在 font_cn_12 之前。Task 6 再删它。

- [ ] **Step 3: 删除三个旧字库文件**

Run:
```bash
rm crouse.h/font5x7.h crouse.h/font_cn_12x12.h crouse.h/font_cn_16x16.h
```

- [ ] **Step 4: grep 验证**

Run: `grep -n "font5x7\|font_cn_12x12\|font_cn_16x16" crouse.c/oled_ssd1306.c`
Expected: 无输出（include 已改名）。

Run: `ls crouse.h/font*.h`
Expected: 只剩 `font.h`。

Run: `grep -nE "font5x7\[|font_cn_12\[|font_cn\[" crouse.h/font.h`
Expected: 三个数组都能 grep 到（含 font_cn，Task 6 删）。

- [ ] **Step 5（可选）: Commit**

```bash
git add -A crouse.h/ crouse.c/oled_ssd1306.c
git commit -m "Refactor: 字库三合一为 font.h"
```

---

## Task 4: i2c_hal / uart_hal 改名 + i2c 内联 lock/unlock + I2C_HAL_Init→I2C1_Init

**Files:**
- Rename: `crouse.c/i2c_hal.c` → `crouse.c/i2c.c`、`crouse.h/i2c_hal.h` → `crouse.h/i2c.h`
- Rename: `crouse.c/uart_hal.c` → `crouse.c/uart.c`、`crouse.h/uart_hal.h` → `crouse.h/uart.h`
- Modify: 重命名后的 i2c.c（内联 + 函数改名 + 注释清理）、i2c.h（函数改名 + 删 I2C2_ReadReg 声明）
- Modify: 调用方 include 改名：`crouse.c/main.c`、`crouse.c/max30102.c`、`crouse.c/mpu6050.c`、`crouse.c/oled_ssd1306.c`、`crouse.c/hc05.c`、`crouse.c/asr_pro.c`

**目标：** 去掉名不符实的 `_hal`；I2C_HAL_Init→I2C1_Init 与 I2C2_Init 对称；内联 i2c_lock/unlock。

- [ ] **Step 1: 重命名 i2c 文件**

Run:
```bash
git mv crouse.c/i2c_hal.c crouse.c/i2c.c
git mv crouse.h/i2c_hal.h crouse.h/i2c.h
```

（用 git mv 保留历史。若 git mv 因未跟踪失败，改用 `mv` + 后续 git add。）

- [ ] **Step 2: 重命名 uart 文件**

Run:
```bash
git mv crouse.c/uart_hal.c crouse.c/uart.c
git mv crouse.h/uart_hal.h crouse.h/uart.h
```

- [ ] **Step 3: 删除游离的 i2c.c/i2c.h（旧的、未加工程的）**

注意：Step 1 的 git mv 把 i2c_hal.c 改名成 i2c.c，但磁盘上原本就有一个游离的 i2c.c（未跟踪）。git mv 会覆盖或报错。**先确认**：

Run: `git status crouse.c/i2c.c crouse.c/i2c.h crouse.h/i2c.h`
Expected: 确认 i2c.c 现在是改名后的（来自 i2c_hal.c），游离旧 i2c.c 已被覆盖或需手动删。

若游离旧 `crouse.c/i2c.c`（未跟踪）还在（git status 显示 ??），删除它：
```bash
rm -f crouse.c/i2c.c.orig   # 若有备份
```
实际操作：git mv i2c_hal.c→i2c.c 时，若目标 i2c.c 已存在（游离文件），git mv 会拒绝。需先删游离 i2c.c：
```bash
rm -f crouse.c/i2c.c        # 删游离旧文件
git mv crouse.c/i2c_hal.c crouse.c/i2c.c   # 再改名
```
游离 `crouse.h/i2c.h` 同理先删再 mv。

- [ ] **Step 4: 改 i2c.h 内容**

`crouse.h/i2c.h`（原 i2c_hal.h）完整新内容：

```c
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
```

变化：守卫 `__I2C_HAL_H`→`__I2C_H`；`I2C_HAL_Init`→`I2C1_Init`；删 `I2C2_ReadReg` 声明（死代码，Task 6 删定义，此处先删声明）。

- [ ] **Step 5: 改 i2c.c 内容（include + 函数改名 + 内联 lock/unlock + 注释清理）**

`crouse.c/i2c.c`（原 i2c_hal.c）修改点：

(a) 第 8 行 `#include "i2c_hal.h"` → `#include "i2c.h"`

(b) 删第 20-22、64-66、132-134、167-169 行的 `/* ======== ... ======== */` 装饰框（4 处）。

(c) 内联 i2c_lock/unlock：删第 53-62 行的 `i2c_lock` 和 `i2c_unlock` 两个 static 函数。在 `i2c_xfer` 里（第 73 行）把 `if (i2c_lock(mtx) != 0) return -1;` 替换为：

```c
    if (!mtx) return -1;
    if (xSemaphoreTake(mtx, pdMS_TO_TICKS(100)) != pdTRUE) return -1;
```

把第 128 行 `i2c_unlock(mtx);` 替换为：

```c
    xSemaphoreGive(mtx);
```

(d) 第 136 行 `void I2C_HAL_Init(void)` → `void I2C1_Init(void)`

(e) 删第 194-197 行 `I2C2_ReadReg` 整个函数（死代码）。

(f) 保留的注释：第 1-6 行文件头（传输 why）、第 43-45 行总线错误 AF/BERR/ARLO 说明（传输 why）、第 81 行 `/* 读 SR2 清除 ADDR 标志 */`（传输 why）、第 141 行重映射原因（传输 why）。第 75/87/114 行的阶段标注 `/* ---- 第1阶段... ---- */` 等按 5.3 判定——属传输时序 why，**保留**。

- [ ] **Step 6: 改 uart.h 内容**

`crouse.h/uart.h`（原 uart_hal.h）只改守卫：

```c
#ifndef __UART_H
#define __UART_H
```
其余不变（UART1_Init/UART1_Send/UART2_Init 声明保留）。

- [ ] **Step 7: 改 uart.c 内容**

`crouse.c/uart.c`（原 uart_hal.c）第 1 行 `#include "uart_hal.h"` → `#include "uart.h"`。其余注释保留（每字节超时、RX 上拉都是传输 why）。

- [ ] **Step 8: 改所有调用方 include**

| 文件 | 改动 |
|---|---|
| crouse.c/main.c 第7行 | `#include "i2c_hal.h"` → `#include "i2c.h"` |
| crouse.c/main.c 第8行 | `#include "uart_hal.h"` → `#include "uart.h"` |
| crouse.c/main.c 第233行 | `I2C_HAL_Init();` → `I2C1_Init();` |
| crouse.c/max30102.c 第3行 | `#include "i2c_hal.h"` → `#include "i2c.h"` |
| crouse.c/mpu6050.c 第3行 | `#include "i2c_hal.h"` → `#include "i2c.h"` |
| crouse.c/oled_ssd1306.c 第2行 | `#include "i2c_hal.h"` → `#include "i2c.h"` |
| crouse.c/hc05.c 第2行 | `#include "uart_hal.h"` → `#include "uart.h"` |
| crouse.c/asr_pro.c 第2行 | `#include "uart_hal.h"` → `#include "uart.h"` |

- [ ] **Step 9: grep 验证**

Run: `grep -rn "i2c_hal\|uart_hal\|I2C_HAL_Init" crouse.c/ crouse.h/`
Expected: 无输出。

Run: `grep -n "I2C1_Init\|I2C2_Init" crouse.c/i2c.c crouse.h/i2c.h crouse.c/main.c`
Expected: i2c.c 定义 I2C1_Init/I2C2_Init，i2c.h 声明，main.c 调用 I2C1_Init。

Run: `grep -n "i2c_lock\|i2c_unlock" crouse.c/i2c.c`
Expected: 无输出（已内联）。

Run: `grep -n "I2C2_ReadReg" crouse.c/i2c.c crouse.h/i2c.h`
Expected: 无输出。

- [ ] **Step 10（可选）: Commit**

```bash
git add -A
git commit -m "Refactor: i2c_hal/uart_hal 改名去 _hal, I2C_HAL_Init→I2C1_Init, 内联 lock/unlock, 删 I2C2_ReadReg"
```

---

## Task 5: keys/iwdg/globals 并入 main.c

**Files:**
- Modify: `crouse.c/main.c`（吸收三个文件内容 + 删对应 include）
- Delete: `crouse.c/keys.c`、`crouse.c/iwdg.c`、`crouse.h/keys.h`、`crouse.h/iwdg.h`、`crouse.h/globals.h`

**目标：** 只被 main.c 用的小模块并入 main.c，消除 5 个碎片文件。

- [ ] **Step 1: 把 keys.c 内容并入 main.c**

在 `crouse.c/main.c` 中，删掉第 19 行 `#include "keys.h"`。把 `crouse.c/keys.c` 的 `Keys_Init`、`key_read`、`Key_Get` 三个函数复制到 main.c 合适位置（建议放在 `Get_Temperature` 之后、`vTaskSensors` 之前，约第 48 行后）。

复制时：
- 保留 keys.c 的 `#include "stm32f10x_gpio.h"`/`"stm32f10x_rcc.h"`/`"FreeRTOS.h"`/`"task.h"` 依赖——main.c 顶部已有这些 include，无需重复。
- 注释清理：keys.c 第 4-6 行板载按键说明（"PA0=WK_UP 翻页 高电平有效 用开漏输出+低电平模拟强下拉"）属 why（解释 GPIO 配置原因，传输/硬件踩坑），**保留**。`key_read` 上方"无阻塞消抖..."注释属算法 why，**保留**。

- [ ] **Step 2: 把 iwdg.c 内容并入 main.c**

删掉 main.c 第 20 行 `#include "iwdg.h"`。把 `crouse.c/iwdg.c` 的 `IWDG_Init`、`IWDG_Feed` 复制到 main.c（紧跟 keys 函数之后）。

依赖 `stm32f10x_iwdg.h`、`stm32f10x_rcc.h`——main.c 顶部已有 rcc，需添加 `#include "stm32f10x_iwdg.h"`（放到第 3 行 rcc 后）。

注释清理：iwdg.c 第 1-13 行时基推导大段注释——属"踩坑 why"（4s 选值理由），**保留**但可压缩。函数内第 1-5 步的 `/* 1. 使能写访问 */` 等步骤注释属 what，**删**（代码已自说明）。

- [ ] **Step 3: 处理 globals.h**

删掉 main.c 第 21 行 `#include "globals.h"`。globals.h 只有 extern 声明 + Get_Temperature 声明，定义本就在 main.c（第 34-47 行）。直接删 include 即可，无需搬移内容。

但需确认：`crouse.c/asr_pro.c`、`crouse.c/hc05.c`、`crouse.c/oled_ssd1306.c` 也 include 了 globals.h（用 g_page_advance 等）。这些文件需要看到 extern 声明。**方案**：把 globals.h 的 extern 声明搬到 algorithms.h 不合适（无关）。改为：在 main.c 顶部保留 extern 声明 + Get_Temperature 声明，但其他文件如何看到？

重新评估：globals.h 被 4 个文件 include（main/asr_pro/hc05/oled）。删 globals.h 后，这 3 个非 main 文件失去 extern 声明。**最简方案：保留 globals.h 不删**——它只有 15 行，是跨任务共享状态的契约，删了反而要在每个 .c 重复 extern。这违背"清爽"。

**改用方案 A**：globals.h 保留，不并入 main.c。从 Task 5 删除 globals.h 相关步骤。只在设计文档备注：globals.h 作为跨任务共享状态契约保留。

修正本任务：**只并入 keys.c 和 iwdg.c，不删 globals.h**。main.c 保留 `#include "globals.h"`。

- [ ] **Step 4: 删除 keys/iwdg 文件**

Run:
```bash
rm crouse.c/keys.c crouse.c/iwdg.c crouse.h/keys.h crouse.h/iwdg.h
```

- [ ] **Step 5: grep 验证**

Run: `grep -rn "keys.h\|iwdg.h" crouse.c/ crouse.h/`
Expected: 无输出。

Run: `grep -n "Keys_Init\|Key_Get\|IWDG_Init\|IWDG_Feed" crouse.c/main.c`
Expected: 四个函数都在 main.c 里定义。

Run: `ls crouse.c/keys.c crouse.c/iwdg.c crouse.h/keys.h crouse.h/iwdg.h 2>/dev/null`
Expected: 无输出（已删）。

Run: `grep -n "globals.h" crouse.c/main.c crouse.c/asr_pro.c crouse.c/hc05.c crouse.c/oled_ssd1306.c`
Expected: 4 个文件都仍 include globals.h（保留）。

- [ ] **Step 6（可选）: Commit**

```bash
git add -A
git commit -m "Refactor: keys.c/iwdg.c 并入 main.c, 删除 4 个碎片文件 (globals.h 保留为共享契约)"
```

---

## Task 6: 删死代码 + 安全内联/换库（oled/hc05/max30102）

**Files:**
- Modify: `crouse.c/oled_ssd1306.c`、`crouse.h/font.h`、`crouse.c/hc05.c`、`crouse.c/hc05.h`、`crouse.c/max30102.c`

**目标：** 删 5.1 节确定的死代码 + 5.4 节安全内联/换库。

- [ ] **Step 1: 删 oled 16x16 死代码（绘制函数 + font_cn 数组）**

在 `crouse.c/oled_ssd1306.c` 中：
- 删 `oled_draw_cn` 函数（原 189-206 行，16x16 单字绘制）
- 删 `oled_draw_cn_str` 函数（原 211-215 行，16x16 串绘制）
- 确认 `oled_draw_cn12` / `oled_draw_cn12_str`（12x12）**保留**（主页在用）

在 `crouse.h/font.h` 中：
- 删 `static const uint8_t font_cn[CN_COUNT][32] = { ... };` 数组（16x16 死字模，Task 3 暂留的）
- 保留 `font5x7`、`CN_*` 枚举、`font_cn_12`

- [ ] **Step 2: oled 手写 strlen 换标准库**

`crouse.c/oled_ssd1306.c` 已 `#include <string.h>`（第 14 行）。把 4 处手写求长：

```c
uint8_t len = 0;
while (s[len]) len++;
if (len == 0) return;
```
替换为：
```c
uint8_t len = (uint8_t)strlen(s);
if (len == 0) return;
```

4 处位置（用 grep 定位）：Run: `grep -n "while (s\[len\])" crouse.c/oled_ssd1306.c` 找到全部，逐处替换。

- [ ] **Step 3: oled 内联 write_data**

删 `crouse.c/oled_ssd1306.c` 中的 `write_data` static 函数（原 41-44 行）。找到 `OLED_Flush` 里调用 `write_data` 的地方，内联为直接调用 `I2C2_WriteReg(SSD1306_ADDR, DAT, ...)`。

先看 OLED_Flush 实现：Run: `grep -n "write_data\|OLED_Flush" crouse.c/oled_ssd1306.c` 定位。把 `write_data(buf, n)` 调用替换为 `I2C2_WriteReg(SSD1306_ADDR, DAT, buf, n)`（核对参数顺序以原 write_data 实现为准）。

- [ ] **Step 4: oled 删不实注释 + 冗余 what 注释**

- 删 `OLED_ShowTemperature` 上方"消除三处重复"不实描述
- 删 `OLED_ShowTime` 上方"消除两处重复"不实描述
- 删 `OLED_ShowActivity` 上方"统一带锁"冗余说明
- 删函数名已自说明的 what 注释：`oled_hline`、`oled_setpixel`、`oled_drawstr_at` 等上方的功能复述（保留含尺寸的如 `/* 10×14 px, 占 2 page */`）
- 删装饰框 `/* ==== 像素级绘图原语 ==== */`、`/* ==== 上层显示函数 ==== */` 框线（保留布局说明文字）
- framebuf 互斥锁大段注释（原 20-31 行）压缩到 4-6 行，保留踩坑核心（两 prio2 任务抢花屏）

- [ ] **Step 5: hc05 删 HC05_SendData + 内联 temp_out**

在 `crouse.c/hc05.c`：
- 删 `HC05_SendData` 函数（原 55-58 行，全项目零调用）
- 删 `crouse.h/hc05.h` 第 7 行 `void HC05_SendData(...);` 声明
- 内联 `temp_out`：找到 `float temp_out = valid ? temp : 0.0f;`（原 110 行），删该变量，在 snprintf 里直接用 `(double)(valid ? temp : 0.0f)`

- [ ] **Step 6: max30102 删永死截断分支**

`crouse.c/max30102.c` 原第 108-110 行：

```c
uint8_t raw[192];
int to_read = samples * 6;
if (to_read > (int)sizeof(raw)) to_read = (int)sizeof(raw);
```
删第三行 `if (...)` 截断（samples & 0x1F 已限 0~31，×6 ≤ 192，永不触发）。保留前两行。

- [ ] **Step 7: grep 验证**

Run: `grep -nE "oled_draw_cn\b|oled_draw_cn_str" crouse.c/oled_ssd1306.c`
Expected: 无输出（注意 `\b` 排除 oled_draw_cn12）。

Run: `grep -n "font_cn\[" crouse.h/font.h`
Expected: 无输出（16x16 数组已删）。

Run: `grep -n "font_cn_12\[" crouse.h/font.h`
Expected: 有输出（12x12 保留）。

Run: `grep -n "HC05_SendData" crouse.c/hc05.c crouse.h/hc05.h`
Expected: 无输出。

Run: `grep -n "I2C2_ReadReg" crouse.c/i2c.c crouse.h/i2c.h`
Expected: 无输出（Task 4 已删，复核）。

Run: `grep -n "write_data" crouse.c/oled_ssd1306.c`
Expected: 无输出（已内联）。

Run: `grep -n "while (s\[len\])" crouse.c/oled_ssd1306.c`
Expected: 无输出（已换 strlen）。

Run: `grep -n "temp_out" crouse.c/hc05.c`
Expected: 无输出。

Run: `grep -n "to_read > (int)sizeof" crouse.c/max30102.c`
Expected: 无输出。

- [ ] **Step 8（可选）: Commit**

```bash
git add -A
git commit -m "Refactor: 删死代码(16x16字库/HC05_SendData/I2C2_ReadReg/永死分支) + 内联write_data + 换strlen"
```

---

## Task 7: 注释清理（algorithms/main/传感器/传输层）

**Files:**
- Modify: `crouse.c/algorithms.c`、`crouse.c/main.c`、`crouse.c/max30102.c`、`crouse.c/mpu6050.c`、`crouse.c/ds18b20.c`、`crouse.c/ds1302.c`、`crouse.c/asr_pro.c`、`crouse.c/systick.c`、`crouse.c/oled_ssd1306.c`（剩余注释）、各 .h

**目标：** 按 5.3 判定三问（算法？传输？踩坑？）清理剩余注释。本任务靠人工判定，逐文件过。

**判定三问：** ①解释算法原因？②解释传输时序/协议？③不这么做会踩坑？三者皆否则删。拿不准时保守保留。

- [ ] **Step 1: algorithms.c 注释清理**

algorithms.c 多数注释是算法 why，**大量保留**。只删：
- 装饰框（如有）
- 重复的 what（如 `/* 收集间隔到局部数组 */` 第 129 行——代码自说明）
- 显而易见的行尾（如有）

保留（算法 why，必留）：
- 第 20-28 行 step_armed/step_hist 文件作用域说明
- 第 29-36 行步态连续性确认 why
- 第 55-63 行 ppg_bad_count/ppg_lock_count why
- 第 102-104 行环形缓冲替代 memmove why
- 第 110-116 行 peak_at 环形索引 why
- 第 143-144 行中值去极值 why
- 第 228-235 行信号质量门控 why
- 第 251-256 行锁定期 why
- 第 314-317 行 static 变量 why
- 第 335-337 行 shaking_energy 必须 float 踩坑
- 第 340-345 行滞回检测 why
- 第 358-365 行步态连续性确认 why
- 第 410-414 行临界区外读 why
- 第 435-439 行 Reset_StepCount 清空历史 why

- [ ] **Step 2: main.c 注释清理**

删：
- 第 25-33、38-47、49-57、107-112、154-156、166-171、181-183、197-206、222-224 行的 `===== 第N步 =====` 装饰框 + 任务职责描述块（`任务: 传感器采集... 轮询读取...`）。函数体自说明。
- main.c 第 64-66 行 IWDG 喂狗注释——属踩坑 why（解释为何放循环顶部覆盖两条出口），**保留**。
- main.c 第 69-71 行 MAX30102 传 NULL 注释——属算法/踩坑 why（避免栈分配 1KB），**保留**。
- main.c 第 77-79 行 mpu_ok 标志注释——属 why（避免 fallback 假数据当真实体温），**保留**。
- DS1302 自检/RTC 输出块的步骤注释（第 252-275 行）——`===== 第6步 =====` 框删，但 switch case 的字符串消息保留（那是数据不是注释）。

- [ ] **Step 3: 传感器 .c/.h 注释清理**

各文件删装饰框 + 冗余 what + 显而易见行尾 + 引脚/寄存器位复述。保留传输 why（命令字、时序、重映射、CRC、CH 位等）。

- max30102.c：删 3 处装饰框、`/* 0~31 (5-bit FIFO 深度) */`冗余、`/* 32×6=192 */`算术。保留 PART_ID 校验 why、FIFO 解码 why。
- mpu6050.c：删 2 处装饰框、enum 行"读到 0x42=8字节"重复。保留唤醒芯片 why、量程 why。
- ds18b20.c：删 3 处装饰框、返回码行尾注释（头文件已记载）。保留 0xCC/0x44 命令字、自检 why、CRC why。
- ds1302.c：删装饰框、SelfTest 逐行英文标签（精简为 3 段）。保留 CH 位 why、命令字、burst-read why、跳过星期 why。
- 各 .h：删装饰框，保留通信说明（传输 why）。

- [ ] **Step 4: asr_pro.c / systick.c 注释清理**

- asr_pro.c：删 `OLED_ShowTime(); /* 时间页: 大号时间显示 */` 行尾 what（第 45 行）。保留 OERR 清除 why、ASR 命令码固件约定 why。
- systick.c：保留 FreeRTOS tick + DWT fallback why（第 30-31 行，传输/系统 why）。基本无需改。

- [ ] **Step 5: grep 装饰框残留**

Run: `grep -rn "========" crouse.c/ crouse.h/`
Expected: 无输出（所有装饰框已删）。

Run: `grep -rn "===== 第" crouse.c/main.c`
Expected: 无输出。

- [ ] **Step 6（可选）: Commit**

```bash
git add -A
git commit -m "Refactor: 按「算法/传输/踩坑三类why」清理注释"
```

---

## Task 8: 删 FCARM 残留 + 同步 Keil 工程文件

**Files:**
- Delete: `fcarm_dummy.h`、`Auto_FcArm_Cmd.inp`、`crouse.h/Net_Config.h`
- Modify: `TASK1.uvprojx`、`TASK1.uvoptx`

**目标：** 清掉 FCARM 残留；Keil 工程文件引用与磁盘一致。

- [ ] **Step 1: 删 FCARM 残留**

Run:
```bash
rm fcarm_dummy.h Auto_FcArm_Cmd.inp crouse.h/Net_Config.h
```

- [ ] **Step 2: 同步 TASK1.uvprojx 文件引用**

打开 `TASK1.uvprojx`，在 `<File>` 项里做以下改动（XML 编辑）：

移除以下 `<FilePath>` 项：
- `.\crouse.c\i2c_hal.c`、`.\crouse.h\i2c_hal.h`
- `.\crouse.c\uart_hal.c`、`.\crouse.h\uart_hal.h`
- `.\crouse.c\keys.c`、`.\crouse.h\keys.h`
- `.\crouse.c\iwdg.c`、`.\crouse.h\iwdg.h`
- `.\crouse.h\max30102_config.h`、`.\crouse.h\mpu6050_config.h`、`.\crouse.h\ds18b20_config.h`、`.\crouse.h\ds1302_config.h`
- `.\crouse.h\font5x7.h`、`.\crouse.h\font_cn_12x12.h`、`.\crouse.h\font_cn_16x16.h`

新增/改名以下 `<FilePath>` 项：
- `.\crouse.c\i2c.c`、`.\crouse.h\i2c.h`
- `.\crouse.c\uart.c`、`.\crouse.h\uart.h`
- `.\crouse.h\font.h`

保留不变：`.\crouse.c\algorithms.c`、`.\crouse.h\algorithms.h`、max30102.c/.h、mpu6050.c/.h、ds18b20.c/.h、ds1302.c/.h、oled_ssd1306.c/.h、hc05.c/.h、asr_pro.c/.h、systick.c/.h、globals.h、FreeRTOSConfig.h、所有 Device/、freertos/、Core/、RTE/ 项。

**编辑方式：** 用 Edit 工具逐个替换 `<FilePath>.\crouse.c\i2c_hal.c</FilePath>` → `<FilePath>.\crouse.c\i2c.c</FilePath>` 等。对于纯删除项，删掉整个 `<File>...</File>` 块（含 `<FileName>` 和 `<FilePath>`）。

- [ ] **Step 3: 同步 TASK1.uvoptx**

`TASK1.uvoptx` 里通常也有对应的文件项（用于调试器记住文件状态）。做与 uvprojx 一致的改名/删除。uvoptx 里文件项格式类似 `<File><FileName>...</FileName><FilePath>...</FilePath></File>`。

Run: `grep -c "i2c_hal\|uart_hal\|_config.h\|keys.c\|iwdg.c\|font5x7\|font_cn_" TASK1.uvoptx`
Expected: 一个数字，表示需处理的项数。

逐项处理（同 Step 2 方法）。

- [ ] **Step 4: grep 验证工程文件**

Run: `grep -c "i2c_hal\|uart_hal\|_config\.h\|\\\\keys\.\|\\\\iwdg\.\|font5x7\|font_cn_12x12\|font_cn_16x16" TASK1.uvprojx`
Expected: 0

Run: `grep -c "i2c_hal\|uart_hal\|_config\.h\|\\\\keys\.\|\\\\iwdg\.\|font5x7\|font_cn_12x12\|font_cn_16x16" TASK1.uvoptx`
Expected: 0

Run: `grep -c "i2c\.c\|uart\.c\|font\.h" TASK1.uvprojx`
Expected: ≥3（i2c.c、uart.c、font.h 都在）

- [ ] **Step 5: 磁盘与工程一致性最终核对**

Run: `ls crouse.c/*.c crouse.h/*.h`
Expected: crouse.c 11 个 .c，crouse.h 14 个 .h，与设计第 3 节清单一致。

Run: `ls fcarm_dummy.h Auto_FcArm_Cmd.inp crouse.h/Net_Config.h 2>/dev/null`
Expected: 无输出（已删）。

- [ ] **Step 6（可选）: Commit**

```bash
git add -A
git commit -m "Refactor: 删 FCARM 残留 + 同步 Keil 工程文件引用"
```

---

## Task 9: 人工编译+烧录验证（用户在 Keil MDK 执行）

**Files:** 无代码改动——这是验证关卡。

**目标:** 确认重构后编译通过、功能与重构前一致。

- [ ] **Step 1: 用户在 Keil MDK 打开 TASK1.uvprojx**

确认左侧工程树无红色缺失文件、文件列表与磁盘一致。

- [ ] **Step 2: 用户 Rebuild 全量编译**

确认 0 error / 0 warning（与重构前基线一致）。若有错误，回到对应 Task 修复——常见：
- include 遗漏：grep 找 `i2c_hal`/`uart_hal`/`_config.h` 残留
- 宏未定义：确认算法参数都在 algorithms.h
- 函数未定义：确认 I2C1_Init 改名一致

- [ ] **Step 3: 用户烧录并跑通功能**

逐项验证与重构前一致：
- OLED 五页显示正常（主页时间+语音指南、心率、血氧、步数、体温）
- 按键 PA0 翻页
- 语音指令翻页（ASR 0x01-0x09）
- 蓝牙 JSON 上报 `{"hr":N,"spo2":N,"steps":N,"temp":N.NN}` 每 2s
- CAL A B / TIME 指令响应
- 体温/心率/血氧/计步读数合理
- DS1302 自检输出 OK、RTC 时间正确

- [ ] **Step 4: 通过后可提交（用户决定）**

重构完成。用户确认后可 commit + push（本次设计要求不推送，除非用户改主意）。

---

## 自审

**1. Spec coverage:**
- 第 3 节文件清单 → Task 1-8 逐项覆盖
- 第 4.1 算法参数迁移 → Task 1
- 第 4.2 include 改名 → Task 2/4/5
- 第 4.3 函数改名 I2C_HAL_Init→I2C1_Init → Task 4
- 第 4.4 字库合并+删16x16 → Task 3 + Task 6
- 第 4.5 main 吸收 keys/iwdg → Task 5（globals.h 改为保留，已在 Task 5 Step 3 说明并对应设计文档需同步修正）
- 第 5.1 死代码 → Task 6（I2C2_ReadReg 在 Task 4 删，其余在 Task 6）
- 第 5.2 不实注释 → Task 6 Step 4
- 第 5.3 注释清理 → Task 7
- 第 5.4 安全内联/换库 → Task 6
- 第 6 节 Keil 同步 → Task 8
- 第 9 节验证 → Task 9
- FCARM 残留 → Task 8

**缺口：** Task 5 决定保留 globals.h（与设计 3.2/4.5"删 globals.h"冲突）。需同步修正设计文档——globals.h 作为跨任务共享状态契约保留，文件数 24→14 改为 24→15，合计 41→26。这是实现中发现的更优方案，需回写设计文档。

**2. Placeholder scan:** 无 TBD/TODO。Task 3 Step 1 的字模数据用"从原文件复制"说明（无法在计划里贴 95 行字模），并给出了精确的复制来源和位置——这是必要的数据搬运，非占位符。

**3. Type consistency:** I2C1_Init 在 Task 4 定义/声明/调用一致；font.h 数组名 font5x7/font_cn/font_cn_12 与原文件一致，oled 引用名不变；HC05_SendData/I2C2_ReadReg 删除后无残留引用（Task 6/4 grep 确认）。

**修正动作：** 需回写设计文档 3.2 节和 4.5 节——globals.h 保留，文件数修正为 crouse.h 24→15、合计 41→26。
