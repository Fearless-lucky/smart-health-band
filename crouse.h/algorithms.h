#ifndef __ALGORITHMS_H
#define __ALGORITHMS_H

#include <stdint.h>

void PPG_Init(void);
void PPG_ProcessSamples(const int32_t *ir, const int32_t *red, uint16_t len);

int  Get_HeartRate(void);
int  Get_SpO2(void);

typedef enum {
    ACTIVITY_UNKNOWN = 0,
    ACTIVITY_WALKING,
    ACTIVITY_RUNNING,
    ACTIVITY_SHAKING
} activity_t;

activity_t Get_ActivityState(void);

float Compensate_Temperature(float raw_temp, float ambient_temp);

void Step_ProcessAccel(int16_t ax, int16_t ay, int16_t az);
int  Get_StepCount(void);
void Reset_StepCount(void);

/* 算法参数 — 由各 *_config.h 迁移而来 (算法层不再依赖 *_config.h) */

/* (原 max30102_config.h) — PPG 算法参数, algorithms.c 使用 */
#define PPG_SAMPLE_RATE            100.0f /* Hz */
#define PPG_BUF_LEN                256    /* 环形缓冲区大小 */
#define PPG_WINDOW_SIZE            200    /* 分析窗口样本数 */
#define PPG_MIN_SAMPLES            50     /* 最小样本数才启动分析 */
#define PPG_LP_CUTOFF              5.0f   /* 低通截止频率 Hz */
#define PPG_HP_CUTOFF              0.5f   /* 高通截止频率 Hz */
#define PPG_EMA_ALPHA              0.15f  /* 心率/血氧指数移动平均平滑系数 */
#define PPG_PEAK_THRESH_RATIO      0.8f   /* 峰值检测阈值 = mean + ratio*std */
#define PPG_PEAK_MIN_DIST_S        0.4f   /* 峰间最短间隔 秒 */
#define PPG_PERIOD_MIN_S           0.3f   /* 最小心跳周期 秒 (<=200bpm) */
#define PPG_PERIOD_MAX_S           2.0f   /* 最大心跳周期 秒 (>=30bpm) */
#define PPG_PEAK_HISTORY_MAX       8      /* 心率计算用最近峰值数 */
#define PPG_STD_ACTIVE_MIN         0.2f   /* 标准差低于此值判定无信号 */
#define PPG_BAD_HOLD_MAX           3      /* 连续N个坏窗口才归零 (防单次抖动闪0) */
#define HR_MAX_DELTA               10     /* 心率单次更新最大变化 bpm (抗跳变) */
#define SPO2_MAX_DELTA             2      /* 血氧单次更新最大变化 % (抗跳变) */
#define PPG_IR_DC_MIN              5000.0f /* IR 通道 DC 均值门限 (无手指时仅环境光, 远低于此值) */
#define PPG_LOCK_COUNT             4      /* 连续N个好窗口才确认有效 (过滤环境微动) */

/* ---- SpO2 校准 ---- */
#define SPO2_CAL_A                 110.0f
#define SPO2_CAL_B                 25.0f
#define SPO2_CLAMP_MIN             50
#define SPO2_CLAMP_MAX             100

/* (原 mpu6050_config.h) — 计步算法参数, algorithms.c 使用 */
#define STEP_GRAVITY_EMA           0.98f  /* 重力估计指数移动平均系数 */
#define STEP_THRESH_EMA            0.995f /* 阈值自适应指数移动平均系数 */
#define STEP_INIT_THRESH           500.0f /* 初始阈值 (归一化后 LSB) */
#define STEP_SETTLE_COUNT          10     /* 前N次静默稳定期 (~2秒) */
#define STEP_MIN_INTERVAL_MS       300    /* 最小步间隔 ms (允许~200spm, 兼顾跑步) */
#define STEP_MIN_THRESH            200.0f /* 自适应阈值下限 (抗静止噪声误计) */
#define STEP_HISTORY_LEN           8      /* 步间隔历史缓冲区大小 */
#define STEP_VALID_WINDOW_MS       3000   /* 步态连续性窗口 ms (需窗口内≥2步才确认步态) */
#define STEP_CONFIRM_MIN           2      /* 窗口内最少步数才算连续步态 (过滤单次碰触) */

/* ---- 运动状态分类 ---- */
#define ACTIVITY_SHAKE_ENERGY      400    /* 晃动能量阈值 */
#define ACTIVITY_IDLE_MS           2000   /* 判定无动作的时间窗口 ms */
#define ACTIVITY_WALK_SPM_MIN      40.0f  /* 步行步频下限 steps/min */
#define ACTIVITY_RUN_SPM_MIN       120.0f /* 跑步步频下限 steps/min */

/* (原 ds18b20_config.h) — 温度补偿参数, algorithms.c 使用 */
#define TEMP_BASE_OFFSET           3.0f   /* 基础温差 skin→core °C (实测3.5偏高0.5, 回调至3.0) */
#define TEMP_AMBIENT_REF           25.0f  /* 参考环境温度 °C */
#define TEMP_AMBIENT_COEFF         0.05f  /* 环境温度修正系数 */
#define TEMP_OFFSET_MIN            1.5f   /* 最小补偿量 °C */
#define TEMP_OFFSET_MAX            4.0f   /* 最大补偿量 °C */

#endif
