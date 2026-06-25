#include "algorithms.h"
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

/* 步态历史 — 文件作用域, 供 Reset_StepCount 一并清零 (见 Step_ProcessAccel 注释) */
static uint32_t step_last      = 0;
static uint32_t step_intervals[STEP_HISTORY_LEN];
static uint8_t  step_hist_idx  = 0;
static uint8_t  step_hist_cnt  = 0;
/* 滞回峰值检测状态: 0=待上升越过阈值, 1=已越过待回落。提到文件作用域
 * 供 Reset_StepCount 重置, 否则清零步数时若正处于 armed 状态, 下一次
 * acc 跌回会立即误计一步 (step_last=0 → now-step_last 巨大 → 通过间隔检查)。 */
static uint8_t  step_armed     = 0;
/* 步态连续性确认: 记录最近 STEP_CONFIRM_MIN 步的绝对时间戳。
 * 只有在 STEP_VALID_WINDOW_MS 时间窗内已有 ≥ STEP_CONFIRM_MIN 步时,
 * 才将新检测到的步计入 steps。这能过滤"碰一下/拍一下"这类孤立单次扰动
 * (它们不会形成连续步态序列)。前 STEP_CONFIRM_MIN-1 步先存入暂存队列,
 * 达到阈值后才回补计入并转入正常计数模式。 */
static uint32_t step_pending_ts[STEP_CONFIRM_MIN];
static uint8_t  step_pending_cnt = 0;
static uint8_t  step_confirmed   = 0;   /* 1=已确认连续步态, 后续步直接计入 */

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
static uint8_t  peak_head  = 0;   /* 下一个写入位置 (环形缓冲) */
static uint8_t  peak_count = 0;   /* 已存峰值数 (≤ MAX_PEAKS) */

/* 连续低信号窗口计数。stdv 低于阈值时不立即归零, 累计到
 * PPG_BAD_HOLD_MAX 才判定"真正无信号", 避免单次运动伪影导致读数闪 0。 */
static uint8_t  ppg_bad_count = 0;

/* 连续"有效信号"窗口计数。需要连续 PPG_LOCK_COUNT 个窗口同时满足
 * (a) 交流分量 stdv 足够 (b) IR 直流分量足够 (有手指按压) 才确认信号有效,
 * 之后才输出心率/血氧。这能过滤环境微动/桌面震动产生的假"信号"——
 * 这类扰动虽能使 stdv 短时超阈值, 但无法持续多窗口稳定维持。 */
static uint8_t  ppg_lock_count = 0;

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
    /* 显式环形缓冲: peak_head 推进, peak_count 饱和在 MAX_PEAKS。
     * 替代旧的 memmove 整体平移方案——O(1) 写入, 且不依赖
     * "peak_count 永远停在 MAX_PEAKS" 这种隐式约定。 */
    peak_times[peak_head] = abs_idx;
    peak_head = (peak_head + 1) % MAX_PEAKS;
    if (peak_count < MAX_PEAKS) peak_count++;
}

/* 返回环形缓冲中第 i 个元素 (按写入时间顺序, i=0 最早, i=peak_count-1 最新)。
 * peak_count 已饱和时, 环形缓冲满, 需从 peak_head 处对齐。 */
static uint32_t peak_at(uint8_t i)
{
    /* 满缓冲时最早的元素就在 peak_head 处; 未满时 peak_head==count, 最早在 0 */
    uint8_t start = (peak_count == MAX_PEAKS) ? peak_head : 0;
    return peak_times[(start + i) % MAX_PEAKS];
}

static int compute_hr(void)
{
    if (peak_count < 2) return 0;

    /* 取最近 (peak_count-1) 个峰间间隔, 上限 PPG_PEAK_HISTORY_MAX。
     * 通过 peak_at() 顺序访问环形缓冲, 不再依赖线性下标。 */
    uint32_t n = (peak_count - 1 > PPG_PEAK_HISTORY_MAX)
                 ? PPG_PEAK_HISTORY_MAX : (peak_count - 1);
    uint32_t start = peak_count - 1 - n;

    uint32_t iv[PPG_PEAK_HISTORY_MAX];
    for (uint32_t k = 0; k < n; k++)
        iv[k] = peak_at(start + k + 1) - peak_at(start + k);

    /* 插入排序求中值 (n ≤ 8, 开销可忽略) */
    for (uint32_t i = 1; i < n; i++) {
        uint32_t key = iv[i];
        int32_t  j = (int32_t)i - 1;
        while (j >= 0 && iv[j] > (uint32_t)key) { iv[j + 1] = iv[j]; j--; }
        iv[j + 1] = key;
    }
    uint32_t median = iv[n / 2];

    /* 仅取中值 ±20% 范围内的间隔求平均 — 剔除漏检峰 (间隔翻倍) 和
     * 误检峰 (间隔减半) 造成的离群间隔, 使 new_hr 稳定不跳变。 */
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

            /* 最近一个峰 = peak_at(peak_count-1), 经环形缓冲正确索引 */
            if (peak_count == 0 ||
                abs_idx > peak_at(peak_count - 1) + min_dist) {
                add_peak(abs_idx);
            }
        }
    }

    for (uint16_t j = 0; j < N; j++) {
        win_red[j] = ppg_red[(start_idx + j) % PPG_BUF_LEN];
    }
    float mean_ir   = calc_mean(window_ir, N);
    float std_ir    = calc_std(window_ir, N, mean_ir);
    float mean_red  = calc_mean(win_red, N);
    float std_red   = calc_std(win_red, N, mean_red);

    /* --- 信号质量门控 (HR 和 SpO2 共用) ---
     * 同时检查两个条件, 缺一不可:
     *   (a) 交流分量: stdv >= PPG_STD_ACTIVE_MIN (有脉搏波纹)
     *   (b) 直流分量: mean_ir >= PPG_IR_DC_MIN (有手指按压, IR 被组织吸收)
     * 仅 (a) 满足而 (b) 不满足时, 多为环境光/桌面震动产生的假纹波,
     * 不应输出读数。需要连续 PPG_LOCK_COUNT 个窗口同时满足才"锁定"输出,
     * 过滤短暂扰动。锁定后只要窗口仍有效就持续更新; 一旦累计坏窗口
     * 达 PPG_BAD_HOLD_MAX 则解锁并归零读数。 */
    int signal_good = (stdv >= PPG_STD_ACTIVE_MIN) && (mean_ir >= PPG_IR_DC_MIN);

    if (!signal_good) {
        ppg_lock_count = 0;          /* 坏窗口中断锁定进程 */
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

    /* 锁定期: 累积连续好窗口, 达 PPG_LOCK_COUNT 才首次输出读数。
     * 锁定后 ppg_lock_count 维持 >= PPG_LOCK_COUNT, 后续每次直接更新。 */
    if (ppg_lock_count < PPG_LOCK_COUNT) {
        ppg_lock_count++;
        if (ppg_lock_count < PPG_LOCK_COUNT) return;  /* 未达锁定, 不输出 */
    }

    /* --- HR 更新: compute_hr 已做中值去极值, 此处加变化率限幅防残余跳变 --- */
    int new_hr = compute_hr();
    if (new_hr > 0) {
        if (ema_hr == 0.0f) {
            ema_hr = (float)new_hr;
        } else {
            float target = (float)new_hr;
            if (target > ema_hr + (float)HR_MAX_DELTA)  target = ema_hr + (float)HR_MAX_DELTA;
            else if (target < ema_hr - (float)HR_MAX_DELTA) target = ema_hr - (float)HR_MAX_DELTA;
            ema_hr = PPG_EMA_ALPHA * target + (1.0f - PPG_EMA_ALPHA) * ema_hr;
        }
        hr = (int)(ema_hr + 0.5f);
    }

    /* --- SpO2 更新: EMA + 变化率限幅 --- */
    if (std_ir > 0.0f && mean_red > 0.0f) {
        float R = (std_red / mean_red) / (std_ir / mean_ir);
        int est = (int)(spo2_cal_A - spo2_cal_B * R + 0.5f);
        if (est < SPO2_CLAMP_MIN)  est = SPO2_CLAMP_MIN;
        if (est > SPO2_CLAMP_MAX)  est = SPO2_CLAMP_MAX;
        if (ema_spo2 == 0.0f) {
            ema_spo2 = (float)est;
        } else {
            float target = (float)est;
            if (target > ema_spo2 + (float)SPO2_MAX_DELTA)  target = ema_spo2 + (float)SPO2_MAX_DELTA;
            else if (target < ema_spo2 - (float)SPO2_MAX_DELTA) target = ema_spo2 - (float)SPO2_MAX_DELTA;
            ema_spo2 = PPG_EMA_ALPHA * target + (1.0f - PPG_EMA_ALPHA) * ema_spo2;
        }
        spo2 = (int)(ema_spo2 + 0.5f);
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
    /* grav/thr/shaking_energy/settle_cnt 是自适应基线, 清零反而需要重新稳定,
     * 故保留为函数内 static (Reset_StepCount 不触碰)。
     * last_step / step_hist_* / step_intervals 属于步态历史, 需要在归零步数时
     * 一并清空, 提到文件作用域供 Reset_StepCount 访问。 */
    static float grav            = 0.0f;
    static float thr             = STEP_INIT_THRESH;
    static float shaking_energy  = 0.0f;
    static uint8_t settle_cnt    = 0;

    uint32_t now = Systick_GetTick();
    float mag = sqrtf((float)ax * ax + (float)ay * ay + (float)az * az);

    if (grav == 0.0f) grav = mag;
    grav = STEP_GRAVITY_EMA * grav + (1.0f - STEP_GRAVITY_EMA) * mag;
    float acc = mag - grav;

    thr = STEP_THRESH_EMA * thr + (1.0f - STEP_THRESH_EMA) * fabsf(acc);

    /* 前 N 次静默，等待重力和阈值稳定 */
    if (settle_cnt < STEP_SETTLE_COUNT) { settle_cnt++; return; }

    /* 累积晃动能量 (EMA, 系数 0.9/0.1)。必须用 float: uint32_t 会把
     * 0.1*|acc|(<1) 每次截断为 0, 导致能量永远累积不起来,
     * ACTIVITY_SHAKING 状态永远无法触发。 */
    shaking_energy = 0.9f * shaking_energy + 0.1f * fabsf(acc);

    /* 带滞回的峰值检测 + 阈值下限。
     * - eff_thr 取自适应阈值与 STEP_MIN_THRESH 的较大值: 静止时自适应阈值
     *   趋向 0, 用下限兜底, 防止传感器噪声被误计为步。
     * - acc 越过 eff_thr → arm (标记疑似步态峰)
     * - acc 跌回 eff_thr/2 以下 → 确认完整峰 → 候选步
     * 相比 "acc>thr 即计步", 滞回能拒绝阈值附近的噪声抖动和宽峰重复计数。 */
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

                /* --- 步态连续性确认 ---
                 * 真实步行是连续序列 (步间隔 300ms~1.5s), 而碰一下/拍一下是
                 * 孤立事件。只有 STEP_VALID_WINDOW_MS 窗口内已有 ≥ STEP_CONFIRM_MIN
                 * 步时, 新步才计入 steps。
                 *   未确认 (step_confirmed==0): 候选步存入 step_pending_ts 暂存队列,
                 *     达到 STEP_CONFIRM_MIN 步且都在窗口内 → 确认 → 回补全部暂存步。
                 *   已确认 (step_confirmed==1): 直接计入, 并用窗口检查维持确认状态。
                 * step_last 无论如何都更新, 作为下个间隔的基准。 */
                if (step_confirmed) {
                    /* 已确认步态: 检查最近步是否仍在窗口内 (防止长时间停顿后误续) */
                    if ((now - step_last) > (uint32_t)STEP_VALID_WINDOW_MS) {
                        /* 超过窗口, 视为新序列开始, 回到未确认状态 */
                        step_confirmed = 0;
                        step_pending_cnt = 0;
                        if (step_pending_cnt < STEP_CONFIRM_MIN)
                            step_pending_ts[step_pending_cnt++] = now;
                    } else {
                        taskENTER_CRITICAL();
                        step_intervals[step_hist_idx] = interval;
                        step_hist_idx = (step_hist_idx + 1) % STEP_HISTORY_LEN;
                        if (step_hist_cnt < STEP_HISTORY_LEN) step_hist_cnt++;
                        steps++;
                        taskEXIT_CRITICAL();
                    }
                } else {
                    /* 未确认: 存入暂存队列, 先不计入 steps。
                     * 数组大小为 STEP_CONFIRM_MIN, 写入前检查防越界。 */
                    if (step_pending_cnt < STEP_CONFIRM_MIN)
                        step_pending_ts[step_pending_cnt++] = now;
                    if (step_pending_cnt >= STEP_CONFIRM_MIN) {
                        /* 检查暂存队列首尾是否都在窗口内 */
                        if ((step_pending_ts[step_pending_cnt - 1] - step_pending_ts[0])
                            <= (uint32_t)STEP_VALID_WINDOW_MS) {
                            /* 确认连续步态: 回补全部暂存步 (首步的 interval 用当前 interval) */
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

    /* 运动状态分类 — 读 step_hist_cnt / step_intervals 做浮点运算。
     * 这段读不进临界区: 本函数运行于 Sensors 任务 (prio 3, 最高用户优先级),
     * 没有用户任务能抢占它; 能抢占的只有 ISR, 而 ISR 不触碰这些变量。
     * 故读到的值要么是上面刚写入的, 要么是上一轮的, 不会撕裂。
     * 把浮点运算留在临界区外, 避免长时关中断影响 tick/响应。 */
    uint32_t idle_time = now - step_last;
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
    /* 归零步数时必须同步清空步态历史, 否则残留的 step_intervals 会立即
     * 把活动状态误判为 WALKING/RUNNING, step_last 也会污染 idle_time 判定。
     * step_armed / step_pending_* / step_confirmed 一并清零, 重置连续性
     * 确认状态机, 避免清零后立即继承旧步态判定。
     * grav/thr/shaking_energy/settle_cnt 是自适应基线, 不清零。 */
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
