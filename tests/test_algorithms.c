/* 算法层单元测试 — 在 PC 主机上直接编译运行, 不依赖任何硬件。
 *
 * 被测对象: crouse.c/algorithms.c (心率/血氧/计步/温度补偿)
 * 策略: 合成已知频率的 PPG 与加速度信号喂入算法, 验证输出收敛到预期值。
 *
 * 运行: make -C tests   (需要 gcc/cc 与 libm)
 */

#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "algorithms.h"
#include "systick.h"   /* 桩头文件 (tests/stub/) */

#define PI_F 3.14159265358979323846f

/* ---------------- 测试框架 ---------------- */

static int g_checks = 0, g_failures = 0;

#define CHECK(cond) do {                                  \
    g_checks++;                                           \
    if (!(cond)) {                                        \
        g_failures++;                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                     \
} while (0)

static int test_summary(void)
{
    printf("----------------------------------------\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) printf("ALL TESTS PASSED\n");
    return g_failures == 0 ? 0 : 1;
}

/* ---------------- Systick 桩 ----------------
 * 计步算法依赖真实时基, 测试中手动推进模拟毫秒计数。 */
static uint32_t g_sim_ms = 0;

uint32_t Systick_GetTick(void)
{
    return g_sim_ms;
}

/* ---------------- PPG 合成信号 ----------------
 * ir/red = dc + ac*sin(2*pi*f*t), 采样率 100Hz, 与固件一致。
 * block_len 模拟 MAX30102 每次 FIFO 读取的样本数 (20 = 200ms 周期)。 */

static uint32_t g_ppg_sample = 0;

static void feed_ppg(int blocks, int block_len,
                     float freq_hz,
                     int32_t ir_dc, int32_t ir_ac,
                     int32_t red_dc, int32_t red_ac)
{
    int32_t ir[32], red[32];
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < block_len; i++) {
            float t = (float)g_ppg_sample / PPG_SAMPLE_RATE;
            ir[i]  = ir_dc  + (int32_t)(ir_ac  * sinf(2.0f * PI_F * freq_hz * t));
            red[i] = red_dc + (int32_t)(red_ac * sinf(2.0f * PI_F * freq_hz * t));
            g_ppg_sample++;
        }
        PPG_ProcessSamples(ir, red, (uint16_t)block_len);
    }
}

/* ---------------- 测试用例 ---------------- */

/* 温度补偿: offset = clamp(1.5 - 0.05*(ambient-25), 1.5, 4.0) */
static void test_temperature(void)
{
    printf("[test] temperature compensation\n");

    /* 参考环境 25°C: offset = 1.5 (下限) */
    CHECK(fabsf(Compensate_Temperature(34.0f, 25.0f) - 35.5f) < 0.01f);

    /* 高温 35°C: 理论 1.0, 被下限 1.5 钳位 */
    CHECK(fabsf(Compensate_Temperature(34.0f, 35.0f) - 35.5f) < 0.01f);

    /* 低温 5°C: 1.5 + 1.0 = 2.5 */
    CHECK(fabsf(Compensate_Temperature(34.0f, 5.0f) - 36.5f) < 0.01f);

    /* 极低温 -15°C: 1.5 + 2.0 = 3.5, 未超上限 4.0 */
    CHECK(fabsf(Compensate_Temperature(34.0f, -15.0f) - 37.5f) < 0.01f);
}

/* 心率 + 血氧: 1.2Hz 脉搏波 (72 bpm) → hr 收敛, SpO2 落在合理区间 */
static void test_hr_spo2(void)
{
    printf("[test] heart rate + SpO2\n");

    /* 20 秒 1.2Hz 信号: IR AC=1500, Red AC=700 (同 DC=50000)
     * R = (700/50000)/(1500/50000) ≈ 0.467 → SpO2 ≈ 110-25*0.467 ≈ 98 */
    feed_ppg(100, 20, 1.2f, 50000, 1500, 50000, 700);

    CHECK(hr >= 65 && hr <= 80);          /* 期望 72 ± 8 */
    CHECK(spo2 >= 90 && spo2 <= 100);     /* 期望 ~98 */

    printf("  hr=%d spo2=%d (expect ~72 / ~98)\n", hr, spo2);
}

/* 信号丢失保持: 手指离开后输出归零。
 * 注意: 分析窗口 200 样本 (2s) 需先被平坦信号填满, 之后连续
 * PPG_BAD_HOLD_MAX 个坏窗口才归零, 故需喂入 > 2s + 余量的数据。 */
static void test_signal_lost(void)
{
    printf("[test] signal loss hold\n");

    /* 无手指: IR DC 远低于门限 5000, 且无脉搏交流分量 */
    feed_ppg(25, 20, 1.2f, 100, 0, 100, 0);

    CHECK(hr == 0);
    CHECK(spo2 == 0);
}

/* 计步 + 运动分类: 1.5Hz 步频 (90 spm) 走路 30 秒 */
static void test_steps_walking(void)
{
    printf("[test] step counting + activity\n");

    Reset_StepCount();

    /* 50Hz 加速度采样, 幅值 = 1g(16384 LSB) ± 4000·sin(2π·1.5t)
     * 每个正半周触发一次 armed→released, 步间隔 ~666ms (>300ms 门限) */
    for (int i = 0; i < 1500; i++) {
        float t = (float)g_sim_ms / 1000.0f;
        int32_t mag = 16384 + (int32_t)(4000.0f * sinf(2.0f * PI_F * 1.5f * t));
        Step_ProcessAccel((int16_t)mag, 0, 0);
        g_sim_ms += 20;
    }

    /* 期望 ~45 步, 容忍 ±1/3 (自适应阈值收敛期会少计几步) */
    CHECK(steps >= 30 && steps <= 60);
    CHECK(activity_state == ACTIVITY_WALKING);   /* 90 spm > 60 下限 */

    printf("  steps=%d activity=WALKING (expect ~45)\n", steps);
}

/* 静止判定: 停止运动 5 秒后回到 RESTING, 且不再计步 */
static void test_resting(void)
{
    printf("[test] resting detection\n");

    int steps_before = steps;
    for (int i = 0; i < 250; i++) {          /* 5s @ 50Hz, 恒定 1g */
        Step_ProcessAccel(16384, 0, 0);
        g_sim_ms += 20;
    }

    CHECK(activity_state == ACTIVITY_RESTING);
    CHECK(steps == steps_before);
}

/* 步数清零 */
static void test_step_reset(void)
{
    printf("[test] step reset\n");
    Reset_StepCount();
    CHECK(steps == 0);
}

int main(void)
{
    test_temperature();
    test_hr_spo2();
    test_signal_lost();
    test_steps_walking();
    test_resting();
    test_step_reset();
    return test_summary();
}
