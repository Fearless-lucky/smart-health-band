#ifndef __DS1302_H
#define __DS1302_H

#include <stdint.h>

typedef struct {
    uint8_t sec, min, hour, day, month, year;
} rtc_time_t;

void DS1302_Init(void);
void DS1302_ReadTime(rtc_time_t *t);
void DS1302_WriteTime(const rtc_time_t *t);
int  DS1302_SelfTest(void);   /* 返回0=通过, -1=通信失败, -2=RAM回读错, -3=WP写入失败 */

#endif
