#ifndef __KEYS_H
#define __KEYS_H

#include <stdint.h>

void Keys_Init(void);
int  Key_Get(void);   /* PA0 (WK_UP) — 翻页, 高电平有效 */

#endif
