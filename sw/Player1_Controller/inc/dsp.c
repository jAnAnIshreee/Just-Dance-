// ----------------------------------------------------------------------------
// 
// File name:     dsp.c
//
// ----------------------------------------------------------------------------
//
// Description:   This code is used for DSP processing of audio output data
//                
// Author:        Mark McDermott, Jon Valvano
// Orig gen date: Aug 1, 2020
// 
//
//
// ----------------------------------------------------------------------------


#include <stdint.h>
int32_t t0,t1,t2; // last 3 inputs/32
int32_t Rxx2;     // autocorrelation factor 
#define K 128     // how fast it responds
#define M 128
void NoiseReject_Init(void){
  t0 = t1 = t2 = 2048/32;
}
int32_t NoiseReject(int32_t x){
  t2 = t1; 
  t1 = t0; 
  t0 = x/32; 
  Rxx2 = ((K-1)*Rxx2 + t0*t2)/K;
  if(Rxx2 < -M) Rxx2 = -M; 
  if(Rxx2 >  M) Rxx2 =  M;
  return (Rxx2*x)/M; 
}

const int32_t ReW[32] ={
  1024, 946, 724, 392, 0, -392, -724, -946, -1024, -946, -724, -392, 0, 392, 724, 946,
  1024, 946, 724, 392, 0, -392, -724, -946, -1024, -946, -724, -392, 0, 392, 724, 946};
const int32_t ImW[32] ={
  0, -392, -724, -946, -1024, -946, -724, -392, 0, 392, 724, 946, 1024, 946, 724, 392,
  0, -392, -724, -946, -1024, -946, -724, -392, 0, 392, 724, 946, 1024, 946, 724, 392};
int32_t x[16];
int32_t rsum1,isum1;
int32_t rsum2,isum2;
void DFT_Init(void){
  rsum1=isum1=0;
  rsum2=isum2=0;
}
  // one data point
void DFT(uint32_t i, int32_t x){
  rsum1 += x*ReW[i];  
  isum1 += x*ImW[i];
  rsum2 += x*ReW[2*i];  
  isum2 += x*ImW[2*i];
}
int32_t Mag1(void){int32_t mag;
  rsum1 /= 1024;
  isum1 /= 1024;
  mag = rsum1*rsum1+isum1*isum1;
  rsum1=isum1=0;
  return mag;
}
int32_t Mag2(void){int32_t mag;
  rsum2 /= 1024;
  isum2 /= 1024;
  mag = rsum2*rsum2+isum2*isum2;
  rsum2=isum2=0;
  return mag;
}
int32_t aMag1(void){ // equiv freq = fs/16
  rsum1=isum1=0;
  for(int i=0;i<16;i++){
    rsum1 += x[i]*ReW[i];  
    isum1 += x[i]*ImW[i];
  }
  rsum1 /= 1024;
  isum1 /= 1024;
  return rsum1*rsum1+isum1*isum1;
}
int32_t aMag2(void){// equiv freq = 2*fs/16
  rsum2=isum2=0;
  for(int i=0;i<16;i++){
    rsum2 += x[i]*ReW[2*i];  
    isum2 += x[i]*ImW[2*i];
  }
  rsum2 /= 1024;
  isum2 /= 1024;
  return rsum2*rsum2+isum2*isum2;
}
int8_t x0,x1,x2; // last 3 inputs
int8_t Median(int8_t x){ 
int8_t result;
  x2 = x1;
  x1 = x0;
  x0 = x;
  if(x0>x1)
    if(x1>x2)     result = x1;   // x0>x1,x1>x2       x0>x1>x2
      else
        if(x0>x2) result = x2;   // x0>x1,x2>x1,x0>x2 x0>x2>x1
        else      result = x0;   // x0>x1,x2>x1,x2>x0 x2>x0>x1
  else 
    if(x2>x1)     result = x1;   // x1>x0,x2>x1       x2>x1>x0
      else
        if(x0>x2) result = x0;   // x1>x0,x1>x2,x0>x2 x1>x0>x2
        else      result = x2;   // x1>x0,x1>x2,x2>x0 x1>x2>x0
  return(result);
}
int8_t Median3(int8_t u1,int8_t u2,int8_t u3){ 
int8_t result;
  if(u1>u2)
    if(u2>u3)     result = u2;   // u1>u2,u2>u3       u1>u2>u3
      else
        if(u1>u3) result = u3;   // u1>u2,u3>u2,u1>u3 u1>u3>u2
        else      result = u1;   // u1>u2,u3>u2,u3>u1 u3>u1>u2
  else 
    if(u3>u2)     result = u2;   // u2>u1,u3>u2       u3>u2>u1
      else
        if(u1>u3) result = u1;   // u2>u1,u2>u3,u1>u3 u2>u1>u3
        else      result = u3;   // u2>u1,u2>u3,u3>u1 u2>u3>u1
  return(result);
}

