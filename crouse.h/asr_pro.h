#ifndef __ASR_PRO_H
#define __ASR_PRO_H

#include <stdint.h>

/* 语音指令码 — 由 ASR PRO 模块固件烧录决定, 此处必须与固件一致, 不可单方修改 */
#define ASR_CMD_HR        0x01
#define ASR_CMD_STEPS     0x02
#define ASR_CMD_STEPRST   0x03
#define ASR_CMD_SPO2      0x04
#define ASR_CMD_TEMP      0x05
#define ASR_CMD_TIME      0x06
#define ASR_CMD_MAIN      0x07
#define ASR_CMD_NEXT      0x08
#define ASR_CMD_ACTIVITY  0x09

void ASR_Init(uint32_t baud);
void ASR_ProcessUART(void);

#endif
