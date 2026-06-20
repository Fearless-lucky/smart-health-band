#ifndef __KEYS_H
#define __KEYS_H

#include <stdint.h>

void Keys_Init(void);
int  Key_Get(void);   /* KEY0 (PE4) — 翻页, 低电平有效 */

#endif
