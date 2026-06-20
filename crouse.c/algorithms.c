#include "algorithms.h"
#include "max30102_config.h"
#include "mpu6050_config.h"
#include "ds18b20_config.h"
#include "systick.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define MAX_PEAKS        32

static volatile int hr      = 0;
static volatile int spo2    = 0;
static volatile int steps   = 0;
static float ema_hr          = 0.0f;
static float ema_spo2        = 0.0f;

static float spo2_cal_A = SPO2_CAL_A;
static float spo2_cal_B = SPO2_CAL_B;

static float   ppg_ir[PPG_BUF_LEN];
static float   ppg_red[PPG_BUF_LEN];
static uint16_t ppg_count      = 0;
static uint16_t ppg_write_idx  = 0;
static uint32_t ppg_total      = 0;

static float lp_state  = 0.0f;
static float hp_state  = 0.0f;
static float x_prev    = 0.0f;

static uint32_t peak_times[MAX_PEAKS];
static uint8_t  peak_count = 0;

static activity_t activity_state = ACTIVITY_UNKNOWN;

static float alpha_lp;
static float alpha_hp;

static float window_ir[PPG_WINDOW_SIZE];
static float filtered[PPG_WINDOW_SIZE];
static float win_red[PPG_WINDOW_SIZE];

void PPG_Init(void)
{
    float rc_lp = 1.0f / (2.0f * 3.1415926535f * PPG_LP_CUTOFF);
    float rc_hp = 1.0f / (2.0f * 3.1415926535f * PPG_HP_CUTOFF);
    float dt    = 1.0f / PPG_SAMPLE_RATE;
    alpha_lp = dt / (rc_lp + dt);
    alpha_hp = dt / (rc_hp + dt);
}

static float calc_mean(const float *v, uint16_t n)
{
    double s = 0.0;
    for (uint16_t i = 0; i < n; i++) s += v[i];
    return (float)(s / n);
}

static float calc_std(const float *v, uint16_t n, float mean)
{
    double s = 0.0;
    for (uint16_t i = 0; i < n; i++) {
        double d = v[i] - mean;
        s += d * d;
    }
    return sqrtf((float)(s / n));
}

static void add_peak(uint32_t abs_idx)
{
    if (peak_count < MAX_PEAKS) {
        peak_times[peak_count++] = abs_idx;
    } else {
        memmove(peak_times, peak_times + 1, (MAX_PEAKS - 1) * sizeof(uint32_t));
        peak_times[MAX_PEAKS - 1] = abs_idx;
    }
}

static int compute_hr(void)
{
    if (peak_count < 2) return 0;

    uint32_t n = (peak_count - 1 > PPG_PEAK_HISTORY_MAX)
                 ? PPG_PEAK_HISTORY_MAX : (peak_count - 1);
    uint32_t start = peak_count - 1 - n;
    double sum_period = 0.0;

    for (uint32_t k = start; k < start + n; k++) {
        sum_period += (double)(peak_times[k + 1] - peak_times[k]) / PPG_SAMPLE_RATE;
    }
    double avg_period = sum_period / (double)n;

    if (avg_period < PPG_PERIOD_MIN_S || avg_period > PPG_PERIOD_MAX_S) return 0;
    return (int)(60.0 / avg_period + 0.5);
}

void PPG_ProcessSamples(const int32_t *ir, const int32_t *red, uint16_t len)
{
    if (len == 0) return;

    for (uint16_t i = 0; i < len; i++) {
        ppg_ir[ppg_write_idx]  = (float)ir[i];
        ppg_red[ppg_write_idx] = (float)red[i];
        ppg_write_idx = (ppg_write_idx + 1) % PPG_BUF_LEN;
    }
    ppg_total += len;
    if (ppg_count < PPG_BUF_LEN) {
        ppg_count += len;
        if (ppg_count > PPG_BUF_LEN) ppg_count = PPG_BUF_LEN;
    }

    uint16_t N = (ppg_count < PPG_WINDOW_SIZE) ? ppg_count : PPG_WINDOW_SIZE;
    if (N < PPG_MIN_SAMPLES) return;

    uint16_t start_idx = (ppg_write_idx + PPG_BUF_LEN - N) % PPG_BUF_LEN;
    for (uint16_t j = 0; j < N; j++) {
        window_ir[j] = ppg_ir[(start_idx + j) % PPG_BUF_LEN];
    }

    float prev_lp = lp_state;
    float prev_hp = hp_state;
    float prev_x  = x_prev;

    for (uint16_t j = 0; j < N; j++) {
        float x   = window_ir[j];
        float yhp = alpha_hp * (prev_hp + x - prev_x);
        prev_hp   = yhp;
        float ylp = prev_lp + alpha_lp * (yhp - prev_lp);
        prev_lp   = ylp;
        prev_x    = x;
        filtered[j] = ylp;
    }
    lp_state = prev_lp;
    hp_state = prev_hp;
    x_prev   = prev_x;

    float mean = calc_mean(filtered, N);
    float stdv = calc_std(filtered, N, mean);
    float thresh = mean + PPG_PEAK_THRESH_RATIO * stdv;

    for (uint16_t i = 1; i < N - 1; i++) {
        if (filtered[i] > thresh &&
            filtered[i] > filtered[i - 1] &&
            filtered[i] > filtered[i + 1]) {

            uint32_t abs_idx = ppg_total - N + i;
            uint32_t min_dist = (uint32_t)(PPG_PEAK_MIN_DIST_S * PPG_SAMPLE_RATE);

            if (peak_count == 0 ||
                abs_idx > peak_times[(peak_count - 1) % MAX_PEAKS] + min_dist) {
                add_peak(abs_idx);
            }
        }
    }

    int new_hr = compute_hr();
    if (stdv < PPG_STD_ACTIVE_MIN) {
        hr = 0;
        ema_hr = 0.0f;
    } else if (new_hr > 0) {
        if (ema_hr == 0.0f) ema_hr = (float)new_hr;
        ema_hr = PPG_EMA_ALPHA * (float)new_hr + (1.0f - PPG_EMA_ALPHA) * ema_hr;
        hr = (int)(ema_hr + 0.5f);
    }

    for (uint16_t j = 0; j < N; j++) {
        win_red[j] = ppg_red[(start_idx + j) % PPG_BUF_LEN];
    }
    float mean_ir   = calc_mean(window_ir, N);
    float std_ir    = calc_std(window_ir, N, mean_ir);
    float mean_red  = calc_mean(win_red, N);
    float std_red   = calc_std(win_red, N, mean_red);

    if (stdv >= PPG_STD_ACTIVE_MIN && mean_ir > 0.0f
        && std_ir > 0.0f && mean_red > 0.0f) {
        float R = (std_red / mean_red) / (std_ir / mean_ir);
        int est = (int)(spo2_cal_A - spo2_cal_B * R + 0.5f);
        if (est < SPO2_CLAMP_MIN)  est = SPO2_CLAMP_MIN;
        if (est > SPO2_CLAMP_MAX)  est = SPO2_CLAMP_MAX;
        if (ema_spo2 == 0.0f) ema_spo2 = (float)est;
        ema_spo2 = PPG_EMA_ALPHA * (float)est + (1.0f - PPG_EMA_ALPHA) * ema_spo2;
        spo2 = (int)(ema_spo2 + 0.5f);
    } else {
        spo2 = 0;
        ema_spo2 = 0.0f;
    }
}

void Set_SpO2_Calibration(float A, float B)
{
    taskENTER_CRITICAL();
    spo2_cal_A = A;
    spo2_cal_B = B;
    taskEXIT_CRITICAL();
}

int   Get_HeartRate(void)  { return hr; }
int   Get_SpO2(void)       { return spo2; }

activity_t Get_ActivityState(void) { return activity_state; }

float Compensate_Temperature(float raw_temp, float ambient_temp)
{
    float delta = ambient_temp - TEMP_AMBIENT_REF;
    float offset = TEMP_BASE_OFFSET - TEMP_AMBIENT_COEFF * delta;
    if (offset < TEMP_OFFSET_MIN) offset = TEMP_OFFSET_MIN;
    if (offset > TEMP_OFFSET_MAX) offset = TEMP_OFFSET_MAX;
    return raw_temp + offset;
}

void Step_ProcessAccel(int16_t ax, int16_t ay, int16_t az)
{
    static float grav            = 0.0f;
    static float thr             = STEP_INIT_THRESH;
    static uint32_t last_step    = 0;
    static uint32_t step_intervals[STEP_HISTORY_LEN];
    static uint8_t  step_hist_idx = 0;
    static uint8_t  step_hist_cnt = 0;
    static uint32_t shaking_energy = 0;

    uint32_t now = Systick_GetTick();
    float mag = sqrtf((float)ax * ax + (float)ay * ay + (float)az * az);

    if (grav == 0.0f) grav = mag;
    grav = STEP_GRAVITY_EMA * grav + (1.0f - STEP_GRAVITY_EMA) * mag;
    float acc = mag - grav;

    thr = STEP_THRESH_EMA * thr + (1.0f - STEP_THRESH_EMA) * fabsf(acc);

    /* 前 N 次静默，等待重力和阈值稳定 */
    static uint8_t settle_cnt = 0;
    if (settle_cnt < STEP_SETTLE_COUNT) { settle_cnt++; return; }

    /* 累积晃动能量 */
    shaking_energy = (uint32_t)(0.9f * (float)shaking_energy + 0.1f * fabsf(acc));

    if (acc > thr && (now - last_step) > (uint32_t)STEP_MIN_INTERVAL_MS) {
        uint32_t interval = now - last_step;

        step_intervals[step_hist_idx] = interval;
        step_hist_idx = (step_hist_idx + 1) % STEP_HISTORY_LEN;
        if (step_hist_cnt < STEP_HISTORY_LEN) step_hist_cnt++;

        taskENTER_CRITICAL();
        steps++;
        taskEXIT_CRITICAL();
        last_step = now;
    }

    /* 运动状态分类 */
    uint32_t idle_time = now - last_step;
    if (idle_time > ACTIVITY_IDLE_MS) {
        if (shaking_energy > ACTIVITY_SHAKE_ENERGY)   activity_state = ACTIVITY_SHAKING;
        else                                           activity_state = ACTIVITY_UNKNOWN;
    } else if (step_hist_cnt >= 3) {
        uint32_t sum_int = 0;
        for (uint8_t i = 0; i < step_hist_cnt; i++) sum_int += step_intervals[i];
        float avg_interval = (float)sum_int / (float)step_hist_cnt;
        float spm = 60000.0f / avg_interval;

        if (spm > ACTIVITY_RUN_SPM_MIN)                activity_state = ACTIVITY_RUNNING;
        else if (spm > ACTIVITY_WALK_SPM_MIN)          activity_state = ACTIVITY_WALKING;
        else                                           activity_state = ACTIVITY_UNKNOWN;
    }
}

int  Get_StepCount(void)  { return steps; }

void Reset_StepCount(void)
{
    taskENTER_CRITICAL();
    steps = 0;
    taskEXIT_CRITICAL();
}
