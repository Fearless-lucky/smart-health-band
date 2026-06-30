#include "algorithms.h"
#include "systick.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define SPO2_CAL_A_VAL  SPO2_CAL_A
#define SPO2_CAL_B_VAL  SPO2_CAL_B
#define MAX_PEAKS        32

//输出结果
volatile int hr      = 0;
volatile int spo2    = 0;
volatile int steps   = 0;
static float ema_hr          = 0.0f;
static float ema_spo2        = 0.0f;

//计步状态
static uint32_t step_last      = 0;
static uint32_t step_intervals[STEP_HISTORY_LEN];
static uint8_t  step_hist_idx  = 0;
static uint8_t  step_hist_cnt  = 0;
static uint8_t  step_armed     = 0;
static uint32_t step_pending_ts[STEP_CONFIRM_MIN];
static uint8_t  step_pending_cnt = 0;
static uint8_t  step_confirmed   = 0;

//PPG信号状态
static float   ppg_ir[PPG_BUF_LEN];
static float   ppg_red[PPG_BUF_LEN];
static uint16_t ppg_count      = 0;
static uint16_t ppg_write_idx  = 0;
static uint32_t ppg_total      = 0;

static float lp_state  = 0.0f;
static float hp_state  = 0.0f;
static float x_prev    = 0.0f;

static uint32_t peak_times[MAX_PEAKS];
static uint8_t  peak_head  = 0;
static uint8_t  peak_count = 0;
static uint8_t  ppg_bad_count = 0;
static uint8_t  ppg_lock_count = 0;

volatile activity_t activity_state = ACTIVITY_RESTING;

static const float alpha_lp = 1.0f / (1.0f + (float)PPG_SAMPLE_RATE / (2.0f * 3.1415926535f * PPG_LP_CUTOFF));
static const float alpha_hp = 1.0f / (1.0f + (float)PPG_SAMPLE_RATE / (2.0f * 3.1415926535f * PPG_HP_CUTOFF));
static float window_ir[PPG_WINDOW_SIZE];
static float filtered[PPG_WINDOW_SIZE];

static void add_peak(uint32_t abs_idx)
{
    peak_times[peak_head] = abs_idx;
    peak_head = (peak_head + 1) % MAX_PEAKS;
    if (peak_count < MAX_PEAKS) peak_count++;
}

static uint32_t peak_at(uint8_t i)
{
    uint8_t start = (peak_count == MAX_PEAKS) ? peak_head : 0;
    return peak_times[(start + i) % MAX_PEAKS];
}

static int ema_update(float *ema, int raw, int max_delta)
{
    if (*ema == 0.0f) { *ema = (float)raw; return (int)(*ema + 0.5f); }
    float target = (float)raw;
    if (target > *ema + (float)max_delta)      target = *ema + (float)max_delta;
    else if (target < *ema - (float)max_delta) target = *ema - (float)max_delta;
    *ema = PPG_EMA_ALPHA * target + (1.0f - PPG_EMA_ALPHA) * *ema;
    return (int)(*ema + 0.5f);
}

static float calc_std(double sum, double sum2, uint16_t n, float *mean_out)
{
    float mean = (float)(sum / n);
    float var  = (float)(sum2 / n) - mean * mean;
    if (var < 0.0f) var = 0.0f;
    *mean_out = mean;
    return sqrtf(var);
}

static int compute_hr(void)
{
    if (peak_count < 2) return 0;

    uint32_t n = (peak_count - 1 > PPG_PEAK_HISTORY_MAX)
                 ? PPG_PEAK_HISTORY_MAX : (peak_count - 1);
    uint32_t start = peak_count - 1 - n;

    uint32_t iv[PPG_PEAK_HISTORY_MAX];
    for (uint32_t k = 0; k < n; k++)
        iv[k] = peak_at(start + k + 1) - peak_at(start + k);

    for (uint32_t i = 1; i < n; i++) {
        uint32_t key = iv[i];
        int32_t  j = (int32_t)i - 1;
        while (j >= 0 && iv[j] > (uint32_t)key) { iv[j + 1] = iv[j]; j--; }
        iv[j + 1] = key;
    }
    uint32_t median = iv[n / 2];

    double sum_period = 0.0;
    uint32_t cnt = 0;
    for (uint32_t k = 0; k < n; k++) {
        if (iv[k] >= median - median / 5 && iv[k] <= median + median / 5) {
            sum_period += (double)iv[k] / PPG_SAMPLE_RATE;
            cnt++;
        }
    }
    if (cnt == 0) return 0;

    double avg_period = sum_period / (double)cnt;
    if (avg_period < PPG_PERIOD_MIN_S || avg_period > PPG_PERIOD_MAX_S) return 0;
    return (int)(60.0 / avg_period + 0.5);
}

//PPG信号处理
void PPG_ProcessSamples(const int32_t *ir, const int32_t *red, uint16_t len)
{
    if (len == 0) return;

    //写环形缓冲
    for (uint16_t i = 0; i < len; i++) {
        ppg_ir[ppg_write_idx]  = (float)ir[i];
        ppg_red[ppg_write_idx] = (float)red[i];
        ppg_write_idx = (ppg_write_idx + 1) % PPG_BUF_LEN;
    }
    ppg_total += len;
    ppg_count += len;
    if (ppg_count > PPG_BUF_LEN) ppg_count = PPG_BUF_LEN;

    //提取分析窗口
    uint16_t N = (ppg_count < PPG_WINDOW_SIZE) ? ppg_count : PPG_WINDOW_SIZE;
    if (N < PPG_MIN_SAMPLES) return;

    uint16_t start_idx = (ppg_write_idx + PPG_BUF_LEN - N) % PPG_BUF_LEN;
    if (start_idx + N <= PPG_BUF_LEN) {
        memcpy(window_ir, &ppg_ir[start_idx], N * sizeof(float));
    } else {
        uint16_t n1 = PPG_BUF_LEN - start_idx;
        memcpy(window_ir, &ppg_ir[start_idx], n1 * sizeof(float));
        memcpy(&window_ir[n1], ppg_ir, (N - n1) * sizeof(float));
    }

    //IIR滤波+统计累加
    float prev_lp = lp_state;
    float prev_hp = hp_state;
    float prev_x  = x_prev;
    double sum_f = 0.0, sum2_f = 0.0;
    double sum_ir = 0.0, sum2_ir = 0.0;
    double sum_red = 0.0, sum2_red = 0.0;

    for (uint16_t j = 0; j < N; j++) {
        float x   = window_ir[j];
        float xr  = ppg_red[(start_idx + j) % PPG_BUF_LEN];
        float yhp = alpha_hp * (prev_hp + x - prev_x);
        prev_hp   = yhp;
        float ylp = prev_lp + alpha_lp * (yhp - prev_lp);
        prev_lp   = ylp;
        prev_x    = x;
        filtered[j] = ylp;

        sum_f    += ylp; sum2_f   += (double)ylp * ylp;
        sum_ir   += x;   sum2_ir  += (double)x * x;
        sum_red  += xr;  sum2_red += (double)xr * xr;
    }
    lp_state = prev_lp;
    hp_state = prev_hp;
    x_prev   = prev_x;

    //计算均值方差阈值
    float mean;
    float stdv    = calc_std(sum_f, sum2_f, N, &mean);
    float thresh  = mean + PPG_PEAK_THRESH_RATIO * stdv;

    float mean_ir;
    float std_ir  = calc_std(sum_ir, sum2_ir, N, &mean_ir);

    float mean_red;
    float std_red = calc_std(sum_red, sum2_red, N, &mean_red);

    //峰值检测
    uint32_t min_dist = (uint32_t)(PPG_PEAK_MIN_DIST_S * PPG_SAMPLE_RATE);
    for (uint16_t i = 1; i < N - 1; i++) {
        if (filtered[i] > thresh &&
            filtered[i] > filtered[i - 1] &&
            filtered[i] > filtered[i + 1]) {

            uint32_t abs_idx = ppg_total - N + i;

            /* 用环绕减法求距上一峰的间隔, 抗 ppg_total uint32 回绕 (497天@100Hz) */
            if (peak_count == 0 ||
                (uint32_t)(abs_idx - peak_at(peak_count - 1)) > min_dist) {
                add_peak(abs_idx);
            }
        }
    }

    //信号质量门控
    int signal_good = (stdv >= PPG_STD_ACTIVE_MIN) && (mean_ir >= PPG_IR_DC_MIN);

    if (!signal_good) {
        ppg_lock_count = 0;
        if (ppg_bad_count < 255) ppg_bad_count++;
        if (ppg_bad_count >= PPG_BAD_HOLD_MAX) {
            hr = 0;
            ema_hr = 0.0f;
            spo2 = 0;
            ema_spo2 = 0.0f;
        }
        return;
    }
    ppg_bad_count = 0;

    if (ppg_lock_count < PPG_LOCK_COUNT) {
        if (++ppg_lock_count < PPG_LOCK_COUNT) return;
    }

    //心率计算
    int new_hr = compute_hr();
    if (new_hr > 0) hr = ema_update(&ema_hr, new_hr, HR_MAX_DELTA);

    //血氧计算
    if (std_ir > 0.0f && mean_red > 0.0f) {
        float R = (std_red / mean_red) / (std_ir / mean_ir);
        int est = (int)(SPO2_CAL_A_VAL - SPO2_CAL_B_VAL * R + 0.5f);
        if (est < SPO2_CLAMP_MIN)  est = SPO2_CLAMP_MIN;
        if (est > SPO2_CLAMP_MAX)  est = SPO2_CLAMP_MAX;
        spo2 = ema_update(&ema_spo2, est, SPO2_MAX_DELTA);
    }
}



//温度算法
float Compensate_Temperature(float raw_temp, float ambient_temp)
{
    float delta = ambient_temp - TEMP_AMBIENT_REF;
    float offset = TEMP_BASE_OFFSET - TEMP_AMBIENT_COEFF * delta;
    if (offset < TEMP_OFFSET_MIN) offset = TEMP_OFFSET_MIN;
    if (offset > TEMP_OFFSET_MAX) offset = TEMP_OFFSET_MAX;
    return raw_temp + offset;
}




//计步算法
void Step_ProcessAccel(int16_t ax, int16_t ay, int16_t az)
{
    static float grav            = 0.0f;
    static float thr             = STEP_INIT_THRESH;
    static uint8_t settle_cnt    = 0;

    uint32_t now = Systick_GetTick();
    float mag = sqrtf((float)ax * ax + (float)ay * ay + (float)az * az);

    if (grav == 0.0f) grav = mag;
    grav = STEP_GRAVITY_EMA * grav + (1.0f - STEP_GRAVITY_EMA) * mag;
    float acc = mag - grav;

    thr = STEP_THRESH_EMA * thr + (1.0f - STEP_THRESH_EMA) * fabsf(acc);

    float eff_thr = (thr < STEP_MIN_THRESH) ? STEP_MIN_THRESH : thr;

    if (!step_armed) {
        if (acc > eff_thr) {
            step_armed = 1;
        }
    } else {
        if (acc < eff_thr * 0.5f) {
            step_armed = 0;
            if ((now - step_last) > (uint32_t)STEP_MIN_INTERVAL_MS) {
                uint32_t interval = now - step_last;

                if (step_confirmed) {
                    if ((now - step_last) > (uint32_t)STEP_VALID_WINDOW_MS) {
                        step_confirmed = 0;
                        step_pending_ts[0] = now;
                        step_pending_cnt = 1;
                    } else {
                        taskENTER_CRITICAL();
                        step_intervals[step_hist_idx] = interval;
                        step_hist_idx = (step_hist_idx + 1) % STEP_HISTORY_LEN;
                        if (step_hist_cnt < STEP_HISTORY_LEN) step_hist_cnt++;
                        steps++;
                        taskEXIT_CRITICAL();
                    }
                } else {
                    if (step_pending_cnt < STEP_CONFIRM_MIN)
                        step_pending_ts[step_pending_cnt++] = now;
                    if (step_pending_cnt >= STEP_CONFIRM_MIN) {
                        if ((step_pending_ts[step_pending_cnt - 1] - step_pending_ts[0])
                            <= (uint32_t)STEP_VALID_WINDOW_MS) {
                            taskENTER_CRITICAL();
                            for (uint8_t i = 0; i < step_pending_cnt; i++) {
                                step_intervals[step_hist_idx] = interval;
                                step_hist_idx = (step_hist_idx + 1) % STEP_HISTORY_LEN;
                                if (step_hist_cnt < STEP_HISTORY_LEN) step_hist_cnt++;
                                steps++;
                            }
                            taskEXIT_CRITICAL();
                            step_confirmed = 1;
                        }
                        step_pending_cnt = 0;
                    }
                }
                step_last = now;
            }
        }
    }

    uint32_t idle_time = now - step_last;
    if (idle_time > ACTIVITY_IDLE_MS) {
        activity_state = ACTIVITY_RESTING;
    } else if (step_hist_cnt >= 3) {
        uint32_t sum_int = 0;
        for (uint8_t i = 0; i < step_hist_cnt; i++) sum_int += step_intervals[i];
        float avg_interval = (float)sum_int / (float)step_hist_cnt;
        float spm = 60000.0f / avg_interval;

        if (spm > ACTIVITY_RUN_SPM_MIN)                activity_state = ACTIVITY_RUNNING;
        else if (spm > ACTIVITY_WALK_SPM_MIN)          activity_state = ACTIVITY_WALKING;
        else                                           activity_state = ACTIVITY_RESTING;
    }
}

void Reset_StepCount(void)
{
    taskENTER_CRITICAL();
    steps          = 0;
    step_last      = 0;
    step_hist_idx  = 0;
    step_hist_cnt  = 0;
    step_armed     = 0;
    step_pending_cnt = 0;
    step_confirmed   = 0;
    taskEXIT_CRITICAL();
}
