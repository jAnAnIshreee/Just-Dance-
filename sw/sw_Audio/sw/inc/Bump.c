// Bump.c
// RSLK Bumper switches
// negative logic internal pullup
// August 25, 2024
// Bump TM4C MSPM0   Position
// 0    PA5  PA27    Left
// 1    PF0  PB15    Center Left
// 2    PB3  PA28    Center Right
// 3    PC4  PA31    Right
#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"
#define PF0             (*((volatile uint32_t *)0x40025004))
#define PB3             (*((volatile uint32_t *)0x40005020))
#define PC4             (*((volatile uint32_t *)0x40006040))
#define PA5             (*((volatile uint32_t *)0x40004080))

void Bump_Init(void){
  SYSCTL_RCGCGPIO_R |= 0x00000027;  // activate clock for Port A,B,C,F
  while((SYSCTL_PRGPIO_R&0x27) != 0x27){};// allow time for clock to stabilize
  GPIO_PORTF_LOCK_R = 0x4C4F434B;   // unlock GPIO Port F
  GPIO_PORTF_CR_R = 0x1F;           // allow changes to PF0
  // only PF0 needs to be unlocked, other bits can't be locked
  GPIO_PORTA_DIR_R &= ~0x20;        // PA5 in
  GPIO_PORTA_PUR_R |= 0x20;         // enable pull-up on PA5 
  GPIO_PORTA_DEN_R |= 0x20;         // enable digital I/O on PA5
  GPIO_PORTF_DIR_R &= ~0x01;        // PF0 in
  GPIO_PORTF_PUR_R |= 0x01;         // enable pull-up on PF0 
  GPIO_PORTF_DEN_R |= 0x01;         // enable digital I/O on PF0
  GPIO_PORTB_DIR_R &= ~0x08;        // PB3 in
  GPIO_PORTB_PUR_R |= 0x08;         // enable pull-up on PB3 
  GPIO_PORTB_DEN_R |= 0x08;         // enable digital I/O on PB3
  GPIO_PORTC_DIR_R &= ~0x10;        // PC4 in
  GPIO_PORTC_PUR_R |= 0x10;         // enable pull-up on PC4 
  GPIO_PORTC_DEN_R |= 0x10;         // enable digital I/O on PC4
}
// bit 0 is left
// bit 1 is left center
// bit 2 is right center
// bit 3 is right
uint32_t Bump_In(void){
// PA5     Left, Bump 0,
// PF0     Center Left, Bump 1,
// PB3     Center Right, Bump 2
// PC4     Right, Bump 3
  return
    ((PA5>>5)|  // PA5 into bit 0
     (PF0<<1)|  // PF0 into bit 1
     (PB3>>1)|  // PB3 into bit 2
     (PC4>>1))  // PC4 into bit 3
     ^0x0F; // convert to positive logic
}
