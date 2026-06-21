#ifndef __IWDG_H
#define __IWDG_H

#include <stdint.h>

/* 独立看门狗 (IWDG) — 基于 LSI (~40kHz), 与主时钟无关。
 *
 * 用途: vTaskSensors 每 200ms 喂一次狗; 任何总线锁死/软件跑飞
 * 导致 4 秒内未喂狗, IWDG 自动复位整片系统, 避免手环永久死机。
 *
 * 调度器启动前在 main() 调用 IWDG_Init(), 之后 vTaskSensors 周期末调用 IWDG_Feed()。 */

void IWDG_Init(void);
/* 启动独立看门狗 (LSI/256, 重载 625 ≈ 4s 超时) */

void IWDG_Feed(void);
/* 喂狗, 重载计数器 (需在 IWDG_Init 后调用) */

#endif
