// SysTickInts.h
// Runs on LM4F120/TM4C123
// Use the SysTick timer to request interrupts at a particular period.
// Daniel Valvano
// Jan 3, 2020

/* This example accompanies the book
   "Embedded Systems: Real Time Interfacing to Arm Cortex M Microcontrollers",
   ISBN: 978-1463590154, Jonathan Valvano, copyright (c) 2020

   Program 5.12, section 5.7

 Copyright 2020 by Jonathan W. Valvano, valvano@mail.utexas.edu
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

// SysTickInts.h
// Runs on LM4F120/TM4C123
// Use the SysTick timer to request interrupts at a particular period.
// This acts as the "Music Interpreter" for the Extra Credit Harmony.

#ifndef __SYSTICKINTS_H__
#define __SYSTICKINTS_H__

#include <stdint.h>
#define BSIZE 512

extern uint16_t buffer0[BSIZE];
extern uint16_t buffer1[BSIZE];
extern volatile uint32_t bufIdx;
extern volatile int activeBuf;
extern volatile int needRefill;
// **************SysTick_Init*********************
// Initialize SysTick periodic interrupts
// Input: interrupt period
//        Units of period are 12.5ns (assuming 80 MHz clock)
// Output: none
void SysTick_Init(uint32_t period);
 
void SysTick_Handler(void);
 

#endif // __SYSTICKINTS_H__