#ifndef __MAX30102_CONFIG_H
#define __MAX30102_CONFIG_H

/* ============================================================
 * MAX30102 硬件配置 — PPG 心率血氧传感器
 * 通信: I2C1, 7-bit 地址 0x57, 8-bit write 0xAE
 * 引脚: PB6=SCL, PB7=SDA (共享 I2C1 总线)
 * ============================================================ */

/* ---- I2C 地址 (8-bit write) ---- */
#define MAX30102_I2C_ADDR          (0x57 << 1)  /* 0xAE */

/* ---- 初始化寄存器配置 ---- */
#define MAX30102_SPO2_CONFIG       0x47   /* ADC=4096, SR=100Hz, PW=411us */
#define MAX30102_LED1_CURRENT      0x50   /* IR LED 电流 ~16mA */
#define MAX30102_LED2_CURRENT      0x50   /* Red LED 电流 ~16mA */
#define MAX30102_FIFO_CONFIG       0x10   /* 滚动覆盖, 不平均 */

/* ============================================================
 * PPG 算法参数 — algorithms.c 使用
 * ============================================================ */
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

/* ---- SpO2 校准 ---- */
#define SPO2_CAL_A                 110.0f
#define SPO2_CAL_B                 25.0f
#define SPO2_CLAMP_MIN             50
#define SPO2_CLAMP_MAX             100

#endif
