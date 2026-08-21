#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"
#include "../inc/CortexM.h"
#include "../src_latest/music.h"
#include "../inc/TLV5616.h"

#define BSIZE 512
uint16_t buffer0[BSIZE];
uint16_t buffer1[BSIZE];
volatile uint32_t bufIdx = 0;
volatile int activeBuf = 0;   // Which buffer is currently playing
volatile int needRefill = -1; // Flag: -1 means no refill, 0 or 1 means refill that buffer

void SysTick_Init(uint32_t period){
  long sr = StartCritical();
  NVIC_ST_CTRL_R = 0;         
  NVIC_ST_RELOAD_R = period-1;
  NVIC_ST_CURRENT_R = 0;      
  NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R&0x00FFFFFF)|0x40000000; 
  NVIC_ST_CTRL_R = 0x07;      
  EndCritical(sr);
}

void SysTick_Handler(void){
if(!Music_Playing) return;

  uint16_t rawSample;

  // 1. Grab the raw signed 16-bit sample from the active buffer
  if(activeBuf == 0) rawSample = buffer0[bufIdx];
  else               rawSample = buffer1[bufIdx];

  // 2. Convert Signed 16-bit to Unsigned 12-bit
  // Add 32768 to remove the negative sign, then shift right 4 to fit 12-bit DAC
  uint16_t dacSample = (rawSample + 32768) >> 4;

  // 3. Output to your 12-bit DAC
  DAC_Out(dacSample);

  bufIdx++;

  // 4. Buffer switching logic
  if(bufIdx >= BSIZE){
    bufIdx = 0;
    needRefill = activeBuf;    
    activeBuf = 1 - activeBuf; 
  }
}