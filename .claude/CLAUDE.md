# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

智能健康手环 (Smart Health Band) — an STM32F103ZE-based wearable health monitor with FreeRTOS. It reads heart rate, SpO2, body temperature, step count, and real-time clock data from multiple sensors, displays results on an SSD1306 OLED, accepts voice commands via ASR PRO, and streams JSON health data over Bluetooth (HC-05).

## Build System

This is a **Keil MDK (uVision) 5** project. The project file is `TASK1.uvprojx`.

- **MCU**: STM32F103ZE (Cortex-M3, high-density), 512KB Flash, 64KB RAM. Startup file: `startup_stm32f10x_hd.s`. Clock: 72MHz (HSE 8MHz × PLL9).
- **Compiler**: ARMCLANG V6.24 (AC6)
- **Output**: `.\Objects\TASK1.hex` (HEX file created)
- **FreeRTOS**: Included in-tree under `freertos/` (not as a CMSIS pack)
- **StdPeriph Library**: Standard Peripheral Drivers in `Device/StdPeriph_Driver/`

Open `TASK1.uvprojx` in Keil MDK to build and flash. There are no CLI build scripts or Makefiles.

## Architecture

### Directory Layout

```
crouse.c/       ← Application source code (all user-written .c files)
crouse.h/       ← Application headers + FreeRTOSConfig.h + *_config.h parameter files
Device/         ← STM32F10x startup, system init, StdPeriph drivers
Core/           ← CMSIS-CORE headers and templates (not user code)
freertos/       ← FreeRTOS kernel source (include/, portable/, src/)
RTE/            ← Run-Time Environment (CMSIS RTOS wrapper)
DebugConfig/    ← Keil debugger configuration
Objects/        ← Build output (.o, .hex)
Listings/       ← Linker map/listings
docs/           ← Technical documentation source (markdown) and generated .docx
```

### Two I2C Buses (key design decision)

- **I2C1** (PB8=SCL, PB9=SDA, Remap): Shared bus for MAX30102 (PPG) and MPU6050 (IMU). Mutex-protected via `g_i2c_mutex`.
- **I2C2** (PB10=SCL, PB11=SDA): Dedicated bus for SSD1306 OLED only. Mutex-protected via `g_i2c2_mutex`.

**Note**: Splitting the OLED onto its own I2C bus avoids bandwidth contention with the 100Hz MAX30102 sensor. The README and `i2c_hal.c` are now consistent on this wiring:

| I2C Bus | SCL | SDA | Devices |
|---------|-----|-----|---------|
| I2C1    | PB8 | PB9 | MAX30102, MPU6050 |
| I2C2    | PB10| PB11| SSD1306 OLED |

### FreeRTOS Task Map

Four tasks run under the FreeRTOS scheduler (preemptive, 1ms tick):

| Task      | Stack | Priority | Period | Responsibility |
|-----------|-------|----------|--------|----------------|
| Sensors   | 512   | 3 (highest) | 200ms | Read MAX30102 FIFO, MPU6050 accel, DS18B20 temp (every 4th cycle ≈ 800ms) |
| Display   | 512   | 2       | 50ms  | Debounce key input, switch pages (5 pages), refresh OLED when active |
| Voice     | 384   | 2       | 100ms | Poll UART2 for ASR PRO single-byte commands |
| BT        | 384   | 2       | 20ms  | Poll UART1 for HC-05 AT/calibration commands, send JSON report every 2s |

Priority 3 for Sensors ensures PPG FIFO reads are not delayed (MAX30102 has only 32-sample FIFO at 100Hz → ~320ms before overflow).

### Unified Sensor API Pattern

Every sensor driver follows the same three-file, three-function convention:

| File | Purpose |
|------|---------|
| `xxx.c` | Implementation — register config, data read, error handling |
| `xxx.h` | Public API — only exposes `Init()` + `ReadData()` (+ `StartXxx()` for async) |
| `xxx_config.h` | All tunable parameters (hardware addresses, algorithm constants) in one place |

**Function conventions:**
- `Init()` → returns `int`: 0 = success, negative = error code
- `ReadData()` → returns `int`: 0 = success, negative = error; data outputs via pointer parameters (pass `NULL` to skip unwanted fields)
- `StartXxx()` → only for async sensors (DS18B20) that need a conversion delay before `ReadData()`

**Why this pattern?** To change a parameter (e.g. LED current, EMA weight, threshold), you only open `xxx_config.h` — you don't need to read the driver implementation. To understand a sensor, you only read its `.h` file — the API surface is exactly two or three functions.

### Code Layering (4-tier architecture)

```
Application layer  (main.c, task functions)   ← "what to do"
Algorithm layer    (algorithms.c)              ← "how to calculate"
Driver layer       (max30102.c, mpu6050.c…)    ← "how to read"
Hardware abstraction (i2c_hal.c, uart_hal.c)   ← "how to transmit"
                          │
                  STM32 HAL / CMSIS / Registers ← hardware
```

Each layer only talks to the one below it. Changing the OLED screen only touches `oled_ssd1306.c` — the algorithm and application layers are unaffected.

### Signal Processing Pipeline (algorithms.c)

1. **PPG Processing**: 100Hz IR/Red samples → 0.5Hz HP filter + 5Hz LP filter (1st-order IIR) → dynamic threshold peak detection → heart rate from inter-peak intervals (EMA: α=0.15).
2. **SpO2**: `SpO2 = A - B × R`, where `R = (AC_red/DC_red) / (AC_ir/DC_ir)`. Calibratable via Bluetooth `CAL A B` command. Clamped to [50, 100].
3. **Step Counting**: Accelerometer magnitude minus gravity (EMA α=0.98) → adaptive threshold → min 400ms inter-step interval → activity classification (WALKING/RUNNING/SHAKING/UNKNOWN) based on cadence and idle time.
4. **Temperature Compensation**: `body_temp = skin_temp + offset`, where offset = 2.5°C − 0.05 × (ambient − 25°C), clamped to [1.0, 3.5]. Ambient reference comes from MPU6050's on-die temperature sensor.

### Globally Shared State

Defined in `crouse.h/globals.h` — these are `volatile` variables written by one task, read by others:

- `g_temperature` (float) — written by Sensors task
- `g_temp_valid` (int) — flag for valid temperature reading
- `g_page_advance` (int) — set by Voice/ASR task to trigger page switch in Display task

### HC-05 Bluetooth Protocol

- UART1 (PA9=TX, PA10=RX), default 9600 baud with auto-baud fallback chain (38400→115200→57600→19200)
- **Output**: JSON report `{"hr":72,"spo2":98,"steps":1234,"temp":36.50}` every 2 seconds
- **Input commands** (newline-terminated):
  - `CAL A B` — set SpO2 calibration coefficients
  - `TIME HH:MM:SS DD/MM/YY` — set RTC

### ASR PRO Voice Commands

UART2 (PA2=TX, PA3=RX) at 9600 baud. Single-byte command codes: 0x01–0x09 map to page navigation and step reset.

## Key Implementation Notes

- **Timekeeping**: `Systick_GetTick()` in [systick.c](crouse.c/systick.c) returns FreeRTOS tick count when scheduler is running, falling back to `DWT->CYCCNT / (SystemCoreClock/1000)` during boot init. The DWT fallback wraps every ~59.6s (at 72MHz), acceptable since init takes <1s. Delay_us/Delay_ms use DWT cycle counting directly (busy-wait), independent of FreeRTOS ticks — safe to call before scheduler starts.
- **Stack overflow detection** (`configCHECK_FOR_STACK_OVERFLOW=1`): The hook outputs "STACK OVERFLOW: <taskname>" via UART1 at 115200 baud, then spins forever.
- **I2C error recovery**: On any I2C failure, the bus is reset via full deinit+reinit before returning error. All I2C functions time out after 50ms per operation. I2C lock timeout: 100ms via mutex.
- **DS18B20 two-step async pattern**: `StartConversion()` (0xCC 0x44) → wait 4×200ms=800ms (≥750ms 12-bit conversion time) → `ReadData()` (0xCC 0xBE + CRC8). The `ds18b20_tick` counter in `vTaskSensors` sequences this across 4 sensor cycles. On the read cycle, `continue` skips `vTaskDelay` so the next conversion starts immediately — the MAX30102 read in that bonus cycle finds 0 new samples (harmless).
- **DS18B20 CRC**: `DS18B20_ReadData` returns `-1` on CRC failure, `-2` on bus timeout. `main.c` checks `== 0` for success. The legacy `TEMP_CRC_INVALID` (-999.0f) macro has been removed — it was dead code.
- **DS1302 self-test**: On boot, `main()` runs `DS1302_SelfTest()` and outputs the result over UART1 (115200) for wiring verification. Also reads and outputs current RTC time.
- **UART1 bootstrap sequence**: `main()` initializes UART1 at 115200 for debug output, then `HC05_Init(9600)` reinitializes it at HC-05's baud rate.
- **OERR handling**: `HC05_Process()` and `ASR_ProcessUART()` explicitly clear UART Overrun Error flag on each poll cycle to prevent receive lockup.
- **MAX30102 FIFO read pattern**: Reads wr/rd pointers separately (two I2C transactions) → computes available samples via `(wr-rd) & 0x1F` → burst-reads register 0x07 for N×6 bytes → decodes 18-bit IR/Red values → feeds to PPG_ProcessSamples(). The two-pointer reads are not atomic, but this is safe: we read what was available at wr snapshot; new data arriving between reads stays for next cycle.
- **OLED dirty-page optimization**: Only pages with modified content are flushed to I2C2 (`dirty[8]` bitmask). Each flush writes page address + 128 bytes of framebuffer data.
- **FreeRTOS mutex init ordering**: Mutexes are created in `I2C_HAL_Init()`/`I2C2_Init()`, which are called in `main()` BEFORE `vTaskStartScheduler()`. This is safe — FreeRTOS mutex/semaphore APIs work before scheduler start.

## Known Concurrency Gotchas

### `g_temperature` / `g_temp_valid` atomic access (resolved)

All cross-task reads of body temperature go through `Get_Temperature(int *valid, float *temp)` ([main.c](crouse.c/main.c)), which wraps both reads in `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()`. This atomically snapshots the valid flag and the float value together, so the reader never sees a mismatched pair. Callers: `vTaskDisplay` (case 3), `HC05_Process`, `OLED_ShowMainPage`, `OLED_ShowTemperature`. The Sensors task is the sole writer (writes `g_temperature` + `g_temp_valid` back-to-back).

### OLED `framebuf` mutex (resolved)

`oled_ssd1306.c` guards each `OLED_Show*` sequence (Clear → DrawString → Flush) with `g_oled_mutex` (`xSemaphoreTake`/`Give`, 100ms timeout). This prevents `vTaskDisplay` and `vTaskVoice` (both prio 2, time-sliced) from interleaving framebuffer mutations and producing garbled output. Low-level `OLED_Clear`/`OLED_DrawChar`/`OLED_DrawString`/`OLED_Flush` are deliberately unlocked — they are only ever called from inside a locked `Show*` function. External callers must use the `Show*` wrappers, never assemble Clear+Draw+Flush by hand.

### IWDG watchdog (new)

`IWDG_Init()` is called in `main()` just before `vTaskStartScheduler()` (LSI 40kHz / 256 prescaler / reload 625 ≈ 4s timeout). `vTaskSensors` calls `IWDG_Feed()` at the top of every loop iteration, covering both the normal `vTaskDelay` path and the DS18B20 `continue` path. Any bus lockup or task hang that stops feeding for 4s triggers a full system reset — the desired recovery behavior for a wearable. `Assert_Handler` and `vApplicationStackOverflowHook` intentionally do NOT feed the watchdog, so fatal faults self-reset within 4s.

### `configASSERT` diagnostics (updated)

`FreeRTOSConfig.h` defines `configASSERT(x)` to call `Assert_Handler(__FILE__, __LINE__)` (defined in `main.c`), which prints `"ASSERT FAIL: <file>:<line>"` via UART1 (re-init to 115200 as a fallback) then enters an infinite loop. Combined with IWDG, an assertion failure logs its location and resets within 4s — far better than the previous silent `for(;;)`.

### `configUSE_TIMERS` disabled

Set to `0` in `FreeRTOSConfig.h`. The project uses no software timers (HC05 telemetry timing is done via `last_report_tick` polling). Disabling frees the Timer service task (256-word stack + TCB + command queue ≈ 1.2KB heap).

### `Get_HeartRate()` / `Get_SpO2()` / `Get_StepCount()` reads without lock

These return `static volatile int` values from [algorithms.c](crouse.c/algorithms.c). On Cortex-M3, aligned 32-bit `int` reads ARE atomic, so no torn reads. However, the reader might see a momentarily stale value between the Sensors task's write and the next scheduler tick. This is acceptable for sensor display/reporting.

### Algorithm static arrays in BSS

`filtered[200]`, `window_ir[200]`, `win_red[200]`, `ppg_ir[256]`, `ppg_red[256]` are all file-scope `static` in [algorithms.c](crouse.c/algorithms.c), consuming ~4.4KB of BSS. This is intentional to avoid stack allocation in the Sensors task (512 words = 2KB stack).

## Modifying the Code

- **Task stack sizes**: Adjust in [main.c](crouse.c/main.c) (3rd argument to `xTaskCreate`). Current: Sensors/Display 512, Voice/BT 384 **words** (×4 = bytes). When modifying the Sensors task, note that `MAX30102_ReadData()` uses ~448 bytes of stack (`raw[192]` + `ir_dec[32]` + `red_dec[32]`), consuming ~22% of the 2KB stack budget.
- **FreeRTOS configuration**: [FreeRTOSConfig.h](crouse.h/FreeRTOSConfig.h) — heap size (15KB), max priorities (5), tick rate (1000Hz), stack overflow detection enabled (method 1).
- **Sensor hardware parameters** (I2C addresses, register values, pin assignments): Edit the relevant `*_config.h` file in [crouse.h/](crouse.h/).
- **Algorithm tuning parameters** (filter cutoffs, EMA weights, thresholds, calibration constants): Also in `*_config.h` files — `max30102_config.h` for PPG, `mpu6050_config.h` for step counting, `ds18b20_config.h` for temperature compensation.
- **I2C bus assignments** and mutex logic: [i2c_hal.c](crouse.c/i2c_hal.c). The unified `i2c_xfer()` function handles both buses.

## Complete Pin Map

| Pin   | Function          | Peripheral      | Config File         |
|-------|-------------------|-----------------|---------------------|
| PA0   | Key (WK_UP)       | GPIO Out OD     | keys.c              |
| PA1   | DS18B20 DQ        | 1-Wire (GPIO)   | ds18b20_config.h    |
| PA2   | ASR PRO TX        | USART2          | uart_hal.c          |
| PA3   | ASR PRO RX        | USART2          | uart_hal.c          |
| PA9   | HC-05 TX          | USART1          | uart_hal.c          |
| PA10  | HC-05 RX          | USART1          | uart_hal.c          |
| PB1   | DS1302 CLK        | GPIO bit-bang   | ds1302_config.h     |
| PB8   | I2C1 SCL          | I2C1 (Remap)    | i2c_hal.c           |
| PB9   | I2C1 SDA          | I2C1 (Remap)    | i2c_hal.c           |
| PB10  | I2C2 SCL          | I2C2            | i2c_hal.c           |
| PB11  | I2C2 SDA          | I2C2            | i2c_hal.c           |
| PB13  | DS1302 CE         | GPIO bit-bang   | ds1302_config.h     |
| PB14  | DS1302 DAT        | GPIO bit-bang   | ds1302_config.h     |
