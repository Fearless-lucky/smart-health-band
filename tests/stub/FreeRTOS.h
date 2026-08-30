#ifndef FREERTOS_H_HOST_STUB
#define FREERTOS_H_HOST_STUB

/* 主机端单元测试桩: 单线程测试进程中临界区为空操作。
 * 仅用于编译算法层 (algorithms.c), 不含任何 FreeRTOS 真实实现。 */

#define taskENTER_CRITICAL() do {} while (0)
#define taskEXIT_CRITICAL()  do {} while (0)

#endif
