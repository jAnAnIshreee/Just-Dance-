// TLV5616.c
// Runs on TM4C123
// Use SSI1 to send a 16-bit code to the TLV5616 and return the reply.
// Daniel Valvano
// EE445L Fall 2015
//    Jonathan W. Valvano 9/22/15

/* This example accompanies the book
   "Embedded Systems: Real Time Interfacing to ARM Cortex M Microcontrollers",
   ISBN: 978-1463590154, Jonathan Valvano, copyright (c) 2014

 Copyright 2014 by Jonathan W. Valvano, valvano@mail.utexas.edu
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

// SSIClk (SCLK) connected to PD0
// SSIFss (FS)   connected to PD1
// SSITx (DIN)   connected to PD3

#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"

#define SSI_CR0_MODE2 0x004F 

#define SSI_SR_TNF   0x00000002  // Transmit FIFO Not Full
#define SSI_SR_RNE   0x00000004  // Receive FIFO Not Empty
#define SSI_SR_BSY   0x00000010  // Busy


void DAC_Init(uint16_t data){
  // Enable clocks
  SYSCTL_RCGCSSI_R |= 0x02;        
  SYSCTL_RCGCGPIO_R |= 0x08;       
  while((SYSCTL_PRGPIO_R & 0x08) == 0){};

  // PD0 = SSI1Clk
  // PD1 = SSI1Fss  ? this is your "frame select"
  // PD3 = SSI1Tx
  GPIO_PORTD_AFSEL_R |= 0x0B;
  GPIO_PORTD_DEN_R   |= 0x0B;
  GPIO_PORTD_AMSEL_R &= ~0x0B;

  GPIO_PORTD_PCTL_R =
    (GPIO_PORTD_PCTL_R & 0xFFFF0F00) |
    0x00002022;

  // Disable SSI during config
  SSI1_CR1_R = 0;

  // Clock divider (safe speed)
  SSI1_CPSR_R = 10;

  // ? SPI Mode 2, 16-bit frame ?
  SSI1_CR0_R = SSI_CR0_SPO | SSI_CR0_DSS_16;

  // Enable SSI
  SSI1_CR1_R |= SSI_CR1_SSE;
}

void DAC_Out(uint16_t code){
  while((SSI1_SR_R & SSI_SR_TNF) == 0){};

  // Send 16-bit frame
  SSI1_DR_R = 0x4000 | (code & 0x0FFF);

  // Wait for completion
  while(SSI1_SR_R & SSI_SR_BSY){};

  // Clear receive FIFO (since no MISO)
  uint32_t dump = SSI1_DR_R;
}

// --------------     DAC_OutNonBlocking   ------------------------------------
// Send data to TLV5616 12-bit DAC without checking for room in the FIFO
// inputs:  voltage output (0 to 4095)
// 
void DAC_Out_NB(uint16_t code){
    // Consider writing this (If it is what your heart desires)
    // Consider the following registers:
	  // SSI1_SR_R, SSI1_DR_R
}