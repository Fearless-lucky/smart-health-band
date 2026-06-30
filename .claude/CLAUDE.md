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
crouse.h/       ← Application headers + FreeRTOSConfig.h
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

**Note**: Splitting the OLED onto its own I2C bus avoids bandwidth contention with the 100Hz MAX30102 sensor. The wiring is:

| I2C Bus | SCL | SDA | Devices |

| I2C Bus | SCL | SDA | Devices |
|---------|-----|-----|---------|
| I2C1    | PB8 | PB9 | MAX30102, MPU6050 |
| I2C2    | PB10| PB11| SSD1306 OLED |

### FreeRTOS Task Map

Four tasks run under the FreeRTOS scheduler (preemptive, 1ms tick):

| Task      | Stack | Priority | Period | Responsibility |
|-----------|-------|----------|--------|----------------|
| Sensors   | 512   | 3 (highest) | 200ms | Read MAX30102 FIFO, MPU6050 accel, DS18B20 temp (every 5th cycle ≈ 1000ms) |
| Display   | 512   | 2       | 50ms  | Debounce key input, switch pages (6 pages), refresh OLED when active |
| Voice     | 384   | 2       | 100ms | Poll UART2 for ASR PRO single-byte commands |
| BT        | 384   | 2       | 20ms  | Broadcast JSON health data every 2s over UART1 (HC-05) |

Priority 3 for Sensors ensures PPG FIFO reads are not delayed (MAX30102 has only 32-sample FIFO at 100Hz → ~320ms before overflow).

### Unified Sensor API Pattern

Every sensor driver has a `.c` (implementation) and `.h` (public API + hardware configuration). No separate `*_config.h` — tuning constants live in the header alongside the API:

| File | Purpose |
|------|---------|
| `xxx.c` | Implementation — register config, data read, error handling |
| `xxx.h` | Public API + all tunable parameters (I2C address, register values, pin assignments, calibration constants) |

**Function conventions:**
- `Init()` → returns `int`: 0 = success, negative = error code
- `ReadData()` → returns `int`: 0 = success, negative = error; data outputs via pointer parameters (pass `NULL` to skip unwanted fields)
- `StartXxx()` → only for async sensors (DS18B20) that need a conversion delay before `ReadData()`

**Algorithm tuning parameters** (filter cutoffs, EMA weights, thresholds, step counting constants, temperature compensation): defined in `algorithms.h` — the algorithm layer is self-contained.

**Why this pattern?** To change a sensor hardware parameter (e.g. LED current, I2C address), you open `xxx.h`. To change an algorithm parameter, you open `algorithms.h`. You don't need to read the driver implementation.

### Code Layering (4-tier architecture)

```
Application layer  (main.c, task functions)   ← "what to do"
Algorithm layer    (algorithms.c)              ← "how to calculate"
Driver layer       (max30102.c, mpu6050.c…)    ← "how to read"
Hardware abstraction (i2c.c, uart.c)   ← "how to transmit"
                          │
                  STM32 HAL / CMSIS / Registers ← hardware
```

Each layer only talks to the one below it. Changing the OLED screen only touches `oled_ssd1306.c` — the algorithm and application layers are unaffected.

### Signal Processing Pipeline (algorithms.c)

1. **PPG Processing**: 100Hz IR/Red samples → 0.5Hz HP filter + 5Hz LP filter (1st-order IIR) → dynamic threshold peak detection → heart rate from inter-peak intervals (EMA: α=0.15).
2. **SpO2**: `SpO2 = A - B × R`, where `R = (AC_red/DC_red) / (AC_ir/DC_ir)`. Calibration coefficients A, B are compile-time constants in `algorithms.h` (`SPO2_CAL_A`, `SPO2_CAL_B`). Clamped to [50, 100].
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
- **Input commands**: Bluetooth command processing (CAL, TIME) has been removed — HC-05 is broadcast-only. RTC time is set via voice ASR command or preset at build time.

### ASR PRO Voice Commands

UART2 (PA2=TX, PA3=RX) at 9600 baud. Single-byte command codes: 0x01–0x09 map to page navigation and step reset.

## Key Implementation Notes

- **Timekeeping**: `Systick_GetTick()` in [systick.c](crouse.c/systick.c) returns FreeRTOS tick count when scheduler is running, falling back to `DWT->CYCCNT / (SystemCoreClock/1000)` during boot init. The DWT fallback wraps every ~59.6s (at 72MHz), acceptable since init takes <1s. Delay_us/Delay_ms use DWT cycle counting directly (busy-wait), independent of FreeRTOS ticks — safe to call before scheduler starts.
- **Stack overflow detection** (`configCHECK_FOR_STACK_OVERFLOW=1`): The hook outputs "STACK OVERFLOW: <taskname>" via UART1 at 115200 baud, then spins forever.
- **I2C error recovery**: On any I2C failure, the bus is reset via full deinit+reinit before returning error. All I2C functions time out after 50ms per operation. I2C lock timeout: 100ms via mutex.
- **DS18B20 two-step async pattern**: `StartConversion()` (0xCC 0x44) → wait 5×200ms=1000ms (≥750ms 12-bit conversion time) → `ReadData()` reads 9-byte scratchpad + validates CRC-8 (poly 0x31). The `ds18b20_tick` counter in `vTaskSensors` sequences this: tick=0 starts conversion, ticks 1–4 wait, tick≥5 reads. After reading, tick resets to 0 and `continue` skips `vTaskDelay` so the next conversion starts immediately — the MAX30102 read in that bonus cycle finds 0 new samples (harmless).
- **DS18B20 CRC**: `DS18B20_ReadData` reads all 9 scratchpad bytes and validates CRC-8. Returns `-1` on CRC failure, `-2` on bus timeout or NULL argument. `main.c` checks `== 0` for success.
- **DS1302 init**: On boot, `main()` calls `DS1302_Init()` which includes a built-in self-check (clears CH bit to start oscillator). The current time is read and output over UART1 (115200) for verification.
- **UART1 bootstrap sequence**: `main()` initializes UART1 at 115200 for debug output, then `HC05_Init(9600)` reinitializes it at HC-05's baud rate.
- **OERR handling**: `ASR_ProcessUART()` clears UART Overrun Error flag on each poll cycle to prevent receive lockup. `HC05_Process()` is broadcast-only and does not read RX, so OERR clearing there is unnecessary.
- **MAX30102 FIFO read pattern**: Reads wr/rd pointers separately (two I2C transactions) → computes available samples via `(wr-rd) & 0x1F` → if OVR bit set between wr and rd, the frame is discarded (data integrity loss) → otherwise burst-reads register 0x07 for N×6 bytes → decodes 18-bit IR/Red values → feeds to PPG_ProcessSamples(). The two-pointer reads are not atomic, but this is safe: we read what was available at wr snapshot; new data arriving between reads stays for next cycle.
- **OLED dirty-page optimization**: Only pages with modified content are flushed to I2C2 (`dirty[8]` bitmask). Each flush writes page address + 128 bytes of framebuffer data. `dirty[page]` is only cleared on successful I2C2 write — if the transfer fails, the page stays dirty and will be retransmitted next cycle.
- **FreeRTOS mutex init ordering**: Mutexes are created in `I2C1_Init()`/`I2C2_Init()`, which are called in `main()` BEFORE `vTaskStartScheduler()`. This is safe — FreeRTOS mutex/semaphore APIs work before scheduler start.

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

### `hr` / `spo2` / `steps` / `activity_state` global access (no getter functions)

These are `volatile` globals defined in [algorithms.c](crouse.c/algorithms.c), declared `extern` in [algorithms.h](crouse.h/algorithms.h). On Cortex-M3, aligned 32-bit `int` reads ARE atomic, so no torn reads. Readers in [main.c](crouse.c/main.c), [hc05.c](crouse.c/hc05.c), and [asr_pro.c](crouse.c/asr_pro.c) access them directly — the former `Get_HeartRate()`/`Get_SpO2()`/`Get_StepCount()`/`Get_ActivityState()` getter functions have been removed. However, the reader might see a momentarily stale value between the Sensors task's write and the next scheduler tick. This is acceptable for sensor display/reporting.

### Algorithm static arrays in BSS

`filtered[200]`, `window_ir[200]`, `ppg_ir[256]`, `ppg_red[256]` are all file-scope `static` in [algorithms.c](crouse.c/algorithms.c), consuming ~3.6KB of BSS (the legacy `win_red[200]` has been removed). This is intentional to avoid stack allocation in the Sensors task (512 words = 2KB stack).

## Modifying the Code

- **Task stack sizes**: Adjust in [main.c](crouse.c/main.c) (3rd argument to `xTaskCreate`). Current: Sensors/Display 512, Voice/BT 384 **words** (×4 = bytes). When modifying the Sensors task, note that `MAX30102_ReadData()` uses ~448 bytes of stack (`raw[192]` + `ir_dec[32]` + `red_dec[32]`), consuming ~22% of the 2KB stack budget.
- **FreeRTOS configuration**: [FreeRTOSConfig.h](crouse.h/FreeRTOSConfig.h) — heap size (15KB), max priorities (5), tick rate (1000Hz), stack overflow detection enabled (method 1).
- **Sensor hardware parameters** (I2C addresses, register values, pin assignments): Edit the sensor's `.h` file in [crouse.h/](crouse.h/) — e.g. `max30102.h` for MAX30102, `mpu6050.h` for MPU6050, `ds18b20.h` for DS18B20, `ds1302.h` for DS1302.
- **Algorithm tuning parameters** (filter cutoffs, EMA weights, thresholds, calibration constants): Edit [algorithms.h](crouse.h/algorithms.h) — the `PPG_*`, `STEP_*`, `TEMP_*`, `SPO2_*` macros.
- **I2C bus assignments** and mutex logic: [i2c.c](crouse.c/i2c.c). The unified `i2c_xfer()` function handles both buses.

## Complete Pin Map

| Pin   | Function          | Peripheral      | Config File         |
|-------|-------------------|-----------------|---------------------|
| PA0   | Key (WK_UP)       | GPIO Out OD     | main.c              |
| PA1   | DS18B20 DQ        | 1-Wire (GPIO)   | ds18b20.h           |
| PA2   | ASR PRO TX        | USART2          | uart.c              |
| PA3   | ASR PRO RX        | USART2          | uart.c              |
| PA9   | HC-05 TX          | USART1          | uart.c              |
| PA10  | HC-05 RX          | USART1          | uart.c              |
| PB1   | DS1302 CLK        | GPIO bit-bang   | ds1302.h            |
| PB8   | I2C1 SCL          | I2C1 (Remap)    | i2c.c               |
| PB9   | I2C1 SDA          | I2C1 (Remap)    | i2c.c               |
| PB10  | I2C2 SCL          | I2C2            | i2c.c               |
| PB11  | I2C2 SDA          | I2C2            | i2c.c               |
| PB13  | DS1302 CE         | GPIO bit-bang   | ds1302.h            |
| PB14  | DS1302 DAT        | GPIO bit-bang   | ds1302.h            |
