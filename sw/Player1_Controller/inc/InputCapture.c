// InputCapture.c
// Runs on LM4F120/TM4C123
// Use Timer0A in edge time mode to request interrupts on the rising
// edge of PB0 (CCP0), and count the pulses.
// Daniel Valvano
// May 3, 2015

/* This example accompanies the book
   "Embedded Systems: Real Time Interfacing to Arm Cortex M Microcontrollers",
   ISBN: 978-1463590154, Jonathan Valvano, copyright (c) 2015
   Example 6.1, Program 6.1

 Copyright 2015 by Jonathan W. Valvano, valvano@mail.utexas.edu
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

// external signal connected to PB6 (T0CCP0) (trigger on rising edge)
#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"
#include "../inc/CortexM.h"

#define NVIC_EN0_INT19          0x00080000  // Interrupt 19 enable

#define TIMER_CFG_16_BIT        0x00000004  // 16-bit timer configuration,
                                            // function is controlled by bits
                                            // 1:0 of GPTMTAMR and GPTMTBMR
#define TIMER_TAMR_TACMR        0x00000004  // GPTM TimerA Capture Mode
#define TIMER_TAMR_TAMR_CAP     0x00000003  // Capture mode
#define TIMER_CTL_TAEN          0x00000001  // GPTM TimerA Enable
#define TIMER_CTL_TAEVENT_POS   0x00000000  // Positive edge
#define TIMER_IMR_CAEIM         0x00000004  // GPTM CaptureA Event Interrupt
                                            // Mask
#define TIMER_ICR_CAECINT       0x00000004  // GPTM CaptureA Event Interrupt
                                            // Clear
#define TIMER_TAILR_M           0xFFFFFFFF  // GPTM Timer A Interval Load
                                            // Register

void (*PeriodicTask)(void);   // user function

//------------TimerCapture_Init------------
// Initialize Timer0A in edge time mode to request interrupts on
// the rising edge of PB6 (CCP0).  The interrupt service routine
// acknowledges the interrupt and calls a user function.
// Input: task is a pointer to a user function
// Output: none
//
//void TimerCapture_Init(void(*task)(void)){
//  SYSCTL_RCGCTIMER_R |= 0x01;// activate timer0
//  SYSCTL_RCGCGPIO_R |= 0x02; // activate port B
//  while((SYSCTL_PRGPIO_R&0x0002) == 0){};// ready?

//  PeriodicTask = task;             // user function 
//  GPIO_PORTB_DIR_R &= ~0x40;       // make PB6 in
//  GPIO_PORTB_AFSEL_R |= 0x40;      // enable alt funct on PB6
//  GPIO_PORTB_DEN_R |= 0x40;        // enable digital I/O on PB6
//                                   // configure PB6 as T0CCP0
//  GPIO_PORTB_PCTL_R = (GPIO_PORTB_PCTL_R&0xF0FFFFFF)+0x07000000;
//  GPIO_PORTB_AMSEL_R &= ~0x40;     // disable analog functionality on PB6
//  TIMER0_CTL_R &= ~TIMER_CTL_TAEN; // disable timer0A during setup
//  TIMER0_CFG_R = TIMER_CFG_16_BIT; // configure for 16-bit timer mode
//                                   // configure for capture mode, default down-count settings
//  TIMER0_TAMR_R = (TIMER_TAMR_TACMR|TIMER_TAMR_TAMR_CAP);
//                                   // configure for rising edge event
//  TIMER0_CTL_R &= ~(TIMER_CTL_TAEVENT_POS|0xC);
//  TIMER0_TAILR_R = TIMER_TAILR_M;  // max start value
//  TIMER0_IMR_R |= TIMER_IMR_CAEIM; // enable capture match interrupt
//  TIMER0_ICR_R = TIMER_ICR_CAECINT;// clear timer0A capture match flag
//  TIMER0_CTL_R |= TIMER_CTL_TAEN;  // enable timer0A 16-b, +edge timing, interrupts
//                                   // Timer0A=priority 2
//  NVIC_PRI4_R = (NVIC_PRI4_R&0x00FFFFFF)|0x40000000; // top 3 bits
//  NVIC_EN0_R = NVIC_EN0_INT19;     // enable interrupt 19 in NVIC
//}

//void Timer0A_Handler(void){
//  TIMER0_ICR_R = TIMER_ICR_CAECINT;// acknowledge timer0A capture match
//  (*PeriodicTask)();               // execute user task
//}

void (*PeriodicTask0B)(void);   // user function

//------------TimerCapture0B_Init------------
// Initialize Timer0B in edge time mode to request interrupts on
// the rising edge of PB7 (T0CCP1).  The interrupt service routine
// acknowledges the interrupt and calls a user function.
// Prescale 80 (1us resolution)
// Input: task is a pointer to a user function
// Output: none
//
void TimerCapture0B_Init(void(*task)(void)){
  SYSCTL_RCGCTIMER_R        |= 0x01;                  // activate timer0
  SYSCTL_RCGCGPIO_R         |= 0x02;                  // activate port B
  while((SYSCTL_PRGPIO_R & 0x0002) == 0){};           // ready?

  PeriodicTask0B             = task;                  // user function 
  GPIO_PORTB_DIR_R          &= ~0x80;                 // make PB6 in
  GPIO_PORTB_AFSEL_R        |= 0x80;                  // enable alt funct on PB7
  GPIO_PORTB_DEN_R          |= 0x80;                  // enable digital I/O on PB7
                                                      // configure PB7 as T0CCP0
  GPIO_PORTB_PCTL_R          = (GPIO_PORTB_PCTL_R
                              & 0x0FFFFFFF)
                              + 0x70000000;
  GPIO_PORTB_AMSEL_R        &= ~0x80;                 // disable analog functionality on PB7
  
  TIMER0_CTL_R              &= ~TIMER_CTL_TBEN;       // disable timer0B during setup
  TIMER0_CFG_R               =  TIMER_CFG_16_BIT;     // configure for 16-bit timer mode
                                                      // configure for capture mode, default down-count settings
  TIMER0_TBMR_R              = (TIMER_TAMR_TACMR
                              | TIMER_TAMR_TAMR_CAP);
                                                      // configure for rising edge event
  TIMER0_TBPR_R              = 0xFF;                  // activate prescale, creating 24-bit
  TIMER0_CTL_R              &= ~(TIMER_CTL_TBEVENT_POS|0xC00);
  TIMER0_TBILR_R             = TIMER_TAILR_M;         // max start value
  TIMER0_IMR_R              |= TIMER_IMR_CBEIM;       // enable capture match interrupt
  TIMER0_ICR_R               = TIMER_ICR_CBECINT;     // clear timer0B capture match flag
  TIMER0_CTL_R              |= TIMER_CTL_TBEN;        // enable timer0B 16-b, +edge timing, interrupts
                                                      // Timer0B=priority 2
  NVIC_PRI5_R                = (NVIC_PRI5_R
                               & 0xFFFFFF00)
                               | 0x00000040;          // bits 7-5 bits
  NVIC_EN0_R                 = 1<<20;                 // enable interrupt 20 in NVIC
}

void Timer0B_Handler(void){
  TIMER0_ICR_R                 = TIMER_ICR_CBECINT;   // acknowledge timer0A capture match
  (*PeriodicTask0B)();                                // execute user task
}


//void (*PeriodicTask1A)(void);   // user function

//------------TimerCapture_Init2------------
// Initialize Timer1A in edge time mode to request interrupts on
// the rising edge of PF2 as T1CCP0.  The interrupt service routine
// acknowledges the interrupt and calls a user function.
// Input: task is a pointer to a user function
// Output: none
/*
void TimerCapture_Init2(void(*task)(void)){
  SYSCTL_RCGCTIMER_R |= 0x02;// activate timer1
  SYSCTL_RCGCGPIO_R |= 0x20; // activate port F
  while((SYSCTL_PRGPIO_R&0x0020) == 0){};// ready?

  PeriodicTask1A = task;             // user function 
  GPIO_PORTF_DIR_R &= ~0x04;       // make PF2 in
  GPIO_PORTF_AFSEL_R |= 0x04;      // enable alt funct on PF2
  GPIO_PORTF_DEN_R |= 0x04;        // enable digital I/O on PF2
                                   // configure PF2 as T1CCP0
  GPIO_PORTF_PCTL_R = (GPIO_PORTF_PCTL_R&0xFFFFF0FF)+0x00000700;
  GPIO_PORTF_AMSEL_R &= ~0x04;     // disable analog functionality on PF2
  TIMER1_CTL_R &= ~TIMER_CTL_TAEN; // disable timer1A during setup
  TIMER1_CFG_R = TIMER_CFG_16_BIT; // configure for 16-bit timer mode
                                   // configure for capture mode, default down-count settings
  TIMER1_TAMR_R = (TIMER_TAMR_TACMR|TIMER_TAMR_TAMR_CAP);
                                   // configure for rising edge event
  TIMER1_CTL_R &= ~(TIMER_CTL_TAEVENT_POS|0xC);
  TIMER1_TAILR_R = TIMER_TAILR_M;  // max start value

  TIMER1_IMR_R |= TIMER_IMR_CAEIM; // enable capture match interrupt
  TIMER1_ICR_R = TIMER_ICR_CAECINT;// clear timer1A capture match flag
  TIMER1_CTL_R |= TIMER_CTL_TAEN;  // enable timer1A 16-b, +edge timing, interrupts
                                   // Timer1A=priority 2
	NVIC_PRI5_R = (NVIC_PRI5_R&0xFFFF00FF)|0x00004000; // top 3 bits, 15:13
  NVIC_EN0_R = 1<<21;     // enable interrupt 21 in NVIC

}

void Timer1A_Handler(void){
  TIMER1_ICR_R = TIMER_ICR_CAECINT;           // acknowledge timer0A capture match
  (*PeriodicTask1A)();                        // execute user task
}
*/


// PB2, Timer3
void (*PeriodicTask3A)(void);                 // user function

//------------TimerCapture3_Init------------
// Initialize Timer3A in edge time mode to request interrupts on
// the rising edge of PB2 (T3CCP0).  The interrupt service routine
// acknowledges the interrupt and calls a user function.
// Prescale 80 (1us resolution)
// Input: task is a pointer to a user function
// Output: none
//
void TimerCapture3_Init(void(*task)(void)){
  
  SYSCTL_RCGCTIMER_R        |= 0x08;                // activate timer3
  SYSCTL_RCGCGPIO_R         |= 0x02;                // activate port B
  while((SYSCTL_PRGPIO_R&0x02) == 0){};             // ready?

  PeriodicTask3A             = task;                // user function 
  GPIO_PORTB_DIR_R          &= ~0x04;               // make PB2 in
  GPIO_PORTB_AFSEL_R        |= 0x04;                // enable alt funct on PB2
  GPIO_PORTB_DEN_R          |= 0x04;                // enable digital I/O on PB2
                                                    // configure PB2 as T3CCP0
    
  GPIO_PORTB_PCTL_R          = (GPIO_PORTB_PCTL_R
                                & 0xFFFFF0FF)
                                + 0x00000700;
  GPIO_PORTB_AMSEL_R        &= ~0x04;               // disable analog functionality on PB2
  TIMER3_CTL_R              &= ~TIMER_CTL_TAEN;     // disable timer0A during setup
  TIMER3_CFG_R               = TIMER_CFG_16_BIT;    // configure for 16-bit timer mode
                                                    // configure for capture mode, default down-count settings
  TIMER3_TAMR_R              = (TIMER_TAMR_TACMR
                              | TIMER_TAMR_TAMR_CAP);
                                                    // configure for rising edge event
  TIMER3_TAPR_R              = 0xFF;                // activate prescale, creating 24-bit
  TIMER3_CTL_R              &= ~(TIMER_CTL_TAEVENT_POS|0xC);
  TIMER3_TAILR_R             = TIMER_TAILR_M;       // max start value
  TIMER3_IMR_R              |= TIMER_IMR_CAEIM;     // enable capture match interrupt
  TIMER3_ICR_R               = TIMER_ICR_CAECINT;   // clear timer3A capture match flag
  TIMER3_CTL_R              |= TIMER_CTL_TAEN;      // enable timer3A 16-b, +edge timing, interrupts
  NVIC_PRI8_R                = (NVIC_PRI8_R
                               & 0x00FFFFFF)
                               | (2<<29);           // Set priority 
                              
                                                    // vector number 51, interrupt number 35
  NVIC_EN1_R                 = 1<<(35-32);          // enable IRQ 35 in NVIC
}

void Timer3A_Handler(void){
  TIMER3_ICR_R               = TIMER_ICR_CAECINT;   // acknowledge timer0A capture match
  (*PeriodicTask3A)();                              // execute user task
}
