// Timer1A.c
// Runs on LM4F120/TM4C123
// Use TIMER1 in 32-bit periodic mode to request interrupts at a periodic rate
// Daniel Valvano
// August 4, 2024

/* This example accompanies the book
   "Embedded Systems: Real Time Interfacing to Arm Cortex M Microcontrollers",
   ISBN: 978-1463590154, Jonathan Valvano, copyright (c) 2024
  Program 7.5, example 7.6

 Copyright 2024 by Jonathan W. Valvano, valvano@mail.utexas.edu
    You may use, edit, run or distribute this file
    as long as the above copyright notice remains
 THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
 OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
 MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
 VALVANO SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL,
 OR CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
 For more information about my classes, my research, and my books, see
 http://users.ece.utexas.edu/~valvano/
 */
#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"
#include "../src_latest/music.h"


void Timer1A_Init(uint32_t period, uint32_t priority){
  SYSCTL_RCGCTIMER_R |= 0x02;   
  TIMER1_CTL_R = 0x00000000;    
  TIMER1_CFG_R = 0x00000000;    
  TIMER1_TAMR_R = 0x00000002;   
  TIMER1_TAILR_R = period-1;    
  TIMER1_TAPR_R = 0;            
  TIMER1_ICR_R = 0x00000001;    
  TIMER1_IMR_R = 0x00000001;    
  NVIC_PRI5_R = (NVIC_PRI5_R&0xFFFF00FF)|(priority<<13); 
  NVIC_EN0_R = 1<<21;           
  TIMER1_CTL_R = 0x00000001;    
}

void Timer1A_Handler(void){
int k = 9;
}