#ifndef __ERRORS_H
#define __ERRORS_H

/* 统一错误码 — 驱动层 Init/ReadData 及 I2C/UART 传输函数的返回值约定:
 *   0      成功
 *   负值   失败, 数值标识失败类别 (调用方通常只需判断 == 0 / != 0)
 *
 * 注意: ASR_GetPageRequest() 返回的 -1 是"队列空"哨兵, 表示无请求,
 *       不属于本错误码体系。 */
#define ERR_OK       0    /* 成功 */
#define ERR_IO      (-1)  /* 总线/外设通信失败: 无 ACK、总线错误、读写出错 */
#define ERR_TIMEOUT (-2)  /* 等待超时: 标志位超时、总线互斥锁超时、1-Wire 无响应 */
#define ERR_CRC     (-3)  /* 数据校验失败: DS18B20 scratchpad CRC-8 不匹配 */
#define ERR_PARAM   (-4)  /* 非法参数: 输出指针为 NULL */
#define ERR_NO_DATA (-5)  /* 无有效数据: FIFO 空 / 溢出后弃帧 */

#endif
