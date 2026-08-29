#ifndef __ASR_PRO_H
#define __ASR_PRO_H

#include <stdint.h>

/* 语音指令码 — 由 ASR PRO 模块固件烧录决定, 此处必须与固件一致, 不可单方修改 */
#define ASR_CMD_HR        0x01   /* 1. 查看心率 */
#define ASR_CMD_STEPS     0x02   /* 2. 查看步数 */
#define ASR_CMD_STEPRST   0x03   /* 3. 归零步数 */
#define ASR_CMD_SPO2      0x04   /* 4. 查看血氧 */
#define ASR_CMD_TEMP      0x05   /* 5. 查看体温 */
#define ASR_CMD_ACTIVITY  0x06   /* 6. 查看状态 */

void ASR_Init(uint32_t baud);
void ASR_ProcessUART(void);

/* 取一条语音翻页请求 (非阻塞)。无请求返回 -1; 有请求返回 0~5 的目标页号。
 * 调用者: vTaskDisplay 每周期调用一次, 取到后由 Display 刷对应页。 */
int  ASR_GetPageRequest(void);

#endif
