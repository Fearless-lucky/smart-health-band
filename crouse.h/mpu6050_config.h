#ifndef __MPU6050_CONFIG_H
#define __MPU6050_CONFIG_H

/* ============================================================
 * MPU6050 硬件配置 — 六轴加速度+陀螺仪传感器
 * 通信: I2C1, 7-bit 地址 0x68, 8-bit write 0xD0
 * 引脚: PB8=SCL, PB9=SDA (I2C1 Remap, 与 MAX30102 共享)
 * ============================================================ */

/* ---- I2C 地址 (8-bit write) ---- */
#define MPU6050_I2C_ADDR           (0x68 << 1)  /* 0xD0 */

/* ---- 加速度量程 ---- */
#define MPU6050_ACCEL_RANGE        0x00   /* ±2g */

/* ---- 芯片温度计算 ---- */
#define MPU6050_TEMP_OFFSET        36.53f /* °C 偏移 */
#define MPU6050_TEMP_SCALE         340.0f /* LSB/°C */
#define MPU6050_TEMP_FALLBACK      25.0f  /* 读取失败默认值 */

/* ============================================================
 * 计步算法参数 — algorithms.c 使用
 * ============================================================ */
#define STEP_GRAVITY_EMA           0.98f  /* 重力估计指数移动平均系数 */
#define STEP_THRESH_EMA            0.995f /* 阈值自适应指数移动平均系数 */
#define STEP_INIT_THRESH           500.0f /* 初始阈值 (归一化后 LSB) */
#define STEP_SETTLE_COUNT          10     /* 前N次静默稳定期 (~2秒) */
#define STEP_MIN_INTERVAL_MS       400    /* 最小步间隔 ms */
#define STEP_HISTORY_LEN           8      /* 步间隔历史缓冲区大小 */

/* ---- 运动状态分类 ---- */
#define ACTIVITY_SHAKE_ENERGY      400    /* 晃动能量阈值 */
#define ACTIVITY_IDLE_MS           2000   /* 判定无动作的时间窗口 ms */
#define ACTIVITY_WALK_SPM_MIN      40.0f  /* 步行步频下限 steps/min */
#define ACTIVITY_RUN_SPM_MIN       120.0f /* 跑步步频下限 steps/min */

#endif
