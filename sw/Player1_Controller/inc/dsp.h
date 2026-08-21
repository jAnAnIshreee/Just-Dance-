#ifndef __DSP_H__
#define __DSP_H__
#include <stdint.h>

void NoiseReject_Init(void);
int32_t NoiseReject(int32_t x);
void DFT_Init(void);
void DFT(uint32_t i, int32_t x);
int32_t Mag1(void);
int32_t Mag2(void);
int8_t Median(int8_t x);

#endif
