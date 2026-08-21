// Timer0A.c
// Runs on LM4F120/TM4C123
// Use Timer0A in periodic mode to request interrupts at a particular
// period.
// Daniel Valvano
// August 4, 2024

/* This example accompanies the book
   "Embedded Systems: Introduction to ARM Cortex M Microcontrollers"
   ISBN: 978-1469998749, Jonathan Valvano, copyright (c) 2024
   Volume 1, Program 9.8

  "Embedded Systems: Real Time Interfacing to ARM Cortex M Microcontrollers",
   ISBN: 978-1463590154, Jonathan Valvano, copyright (c) 2024
   Volume 2, Program 7.5, example 7.6

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
#include "../inc/TLV5616.h"



void Timer0A_Init(uint32_t period, uint32_t priority){
  SYSCTL_RCGCTIMER_R |= 0x01;      
  TIMER0_CTL_R &= ~0x00000001;     
  TIMER0_CFG_R = 0x00000000;       
  TIMER0_TAMR_R = 0x00000002;      
  TIMER0_TAILR_R = period-1;       
  TIMER0_TAPR_R = 0;               
  TIMER0_ICR_R = 0x00000001;       
  TIMER0_IMR_R |= 0x00000001;      
  NVIC_PRI4_R = (NVIC_PRI4_R&0x00FFFFFF)|(priority<<29); 
  NVIC_EN0_R = 1<<19;     
  TIMER0_CTL_R |= 0x00000001;      
}

void Timer0A_Handler(void){
int r = 7;
}