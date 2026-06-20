#ifndef __ALGORITHMS_H
#define __ALGORITHMS_H

#include <stdint.h>

void PPG_Init(void);
void PPG_ProcessSamples(const int32_t *ir, const int32_t *red, uint16_t len);

int  Get_HeartRate(void);
int  Get_SpO2(void);

void Set_SpO2_Calibration(float A, float B);

typedef enum {
    ACTIVITY_UNKNOWN = 0,
    ACTIVITY_WALKING,
    ACTIVITY_RUNNING,
    ACTIVITY_SHAKING
} activity_t;

activity_t Get_ActivityState(void);

float Compensate_Temperature(float raw_temp, float ambient_temp);

void Step_ProcessAccel(int16_t ax, int16_t ay, int16_t az);
int  Get_StepCount(void);
void Reset_StepCount(void);

#endif
