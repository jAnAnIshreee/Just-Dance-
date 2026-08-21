// File **********Switch.c***********
// Spring 2025

#include <stdio.h>
#include <stdint.h>
#include "../inc/PLL.h"
#include "../inc/tm4c123gh6pm.h"
#include "Switch.h"
    
void PortC_Switches_Init(void){ //ALSO INITIALIZES PORT B
  // 1) Activate clock for Port B (bit 1 = 0x02) and Port C (bit 2 = 0x04) -> 0x06
  SYSCTL_RCGCGPIO_R |= 0x06;              
  
  // Allow time for clocks to stabilize on both ports
  while((SYSCTL_PRGPIO_R & 0x06) != 0x06){}; 

  // ============================================
  // Initialize Port C (PC4, PC5 for Song Selection)
  // ============================================
  GPIO_PORTC_PCTL_R &= ~0x00FF0000;  // clear PC4-5 [bits 23:16]
  GPIO_PORTC_AMSEL_R &= ~0x30;       // disable analog on PC4-5
  GPIO_PORTC_AFSEL_R &= ~0x30;       // disable alt func on PC4-5
  GPIO_PORTC_DIR_R &= ~0x30;         // <-- INPUTS (0x30 = 0011 0000)
  GPIO_PORTC_DEN_R |= 0x30;          // enable digital on PC4-5

  // ============================================
  // Initialize Port B (PB0 for Start Button)
  // ============================================
  GPIO_PORTB_PCTL_R &= ~0x0000000F;  // clear PB0 [bits 3:0]
  GPIO_PORTB_AMSEL_R &= ~0x01;       // disable analog on PB0
  GPIO_PORTB_AFSEL_R &= ~0x01;       // disable alt func on PB0
  GPIO_PORTB_DIR_R &= ~0x01;         // <-- INPUT (0x01 = 0000 0001)
  GPIO_PORTB_DEN_R |= 0x01;          // enable digital on PB0
}

// Function to read buttons
uint8_t Buttons_Read(void){
  // Read Port C (PC4 = 0x10, PC5 = 0x20)
  // (~ inverts so pressed = 1)
  uint8_t portC_val = (~GPIO_PORTC_DATA_R & 0x30);
  
  // Read Port B (PB0 = 0x01)
  // We shift it left by 3 so that PB0 (0x01) registers as 0x08.
  // This automatically matches Lab5.c's logic so you don't have to rewrite it!
  uint8_t portB_val = ((~GPIO_PORTB_DATA_R & 0x01) << 3);

  // Return the combined button states
  return (portC_val | portB_val);
}