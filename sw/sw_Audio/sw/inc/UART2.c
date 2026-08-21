//UART2.c
//Dung Nguyen
//Wally Guzman

#include <stdint.h>
#include "../inc/UART2.h"
#include "../inc/tm4c123gh6pm.h"
// PD7=U2Tx PD6=U2Rx
void UART2_Init(uint32_t baud){
  volatile int delay;
    
    // Enable UART2
  SYSCTL_RCGCUART_R |= 0x02;
  delay++; delay++;
    
    // Enable PORT D clock gating
  SYSCTL_RCGCGPIO_R |= 0x08;
  delay++; delay++;
  GPIO_PORTD_LOCK_R = 0x4C4F434B;   // 2) unlock GPIO Port D
  GPIO_PORTD_CR_R = 0xFF;           // allow changes to PD7
	
    // Enable UART2 
  GPIO_PORTD_AFSEL_R |= 0xC0; // PD7, PD6
  GPIO_PORTD_PCTL_R =(GPIO_PORTD_PCTL_R&0x00FFFFFF)|0x11000000;
  GPIO_PORTD_DEN_R   |= 0xC0; // PD7, PD6 
  UART2_CTL_R &= ~UART_CTL_UARTEN;                  // Clear UART2 enable bit during config
  UART2_CTL_R |= 0x20;
// UART2_IBRD_R = 43;       // IBRD = int(80,000,000 / (16 * 115,200)) = int(43.403)
// UART2_FBRD_R = 26;       // FBRD = round(0.4028 * 64 ) = 26	
  UART2_IBRD_R = 5000000/baud;   										// IBRD = int(80,000,000 / (16 * baud))
  UART2_FBRD_R = ((64*(5000000%baud))+baud/2)/baud; // FBRD = round(remainder * 64 + 0.5)
	
//	UART2_IBRD_R = 86; //57,600
//	UART2_FBRD_R = 52;
	
  UART2_LCRH_R = (UART_LCRH_WLEN_8|UART_LCRH_FEN);  // 8 bit word length, 1 stop, no parity, FIFOs enabled
  UART2_IFLS_R &= ~0x3F;                            // Clear TX and RX interrupt FIFO level fields
  UART2_IFLS_R += UART_IFLS_RX4_8;                  // RX FIFO interrupt threshold >= 1/2th full
  UART2_IM_R |= UART_IM_RXIM | UART_IM_RTIM;       	// Enable interupt on RX and RX transmission end
  UART2_CTL_R |= UART_CTL_UARTEN;                   // Set UART2 enable bit  

	
  NVIC_EN1_R = 1<<(33-32);// Enable Interrupts on #33
}

//--------UART2_OutChar--------
// Prints a character to UART2
// Inputs: character to transmit
// Outputs: none
void UART2_OutChar(char data){
  while((UART2_FR_R&UART_FR_TXFF) != 0);
  UART2_DR_R = data;
}

void UART2_OutString(char *pt){
  while(*pt){
    UART2_OutChar(*pt);
    pt++;
  }
}

//--------UART2_EnableRXInterrupt--------
// - Enables UART2 RX interrupt
// Inputs: none
// Outputs: none
void UART2_EnableRXInterrupt(void){
  NVIC_EN1_R = 1<<(33-32); // interrupt 33
}

//--------UART2_DisableRXInterrupt--------
// - Disables UART2 RX interrupt
// Inputs: none
// Outputs: none
void UART2_DisableRXInterrupt(void){
  NVIC_DIS1_R = 1<<(33-32); // interrupt 33    
}

//------------UART2_FinishOutput------------
// Wait for all transmission to finish
// Input: none
// Output: none
void UART2_FinishOutput(void){
  // Wait for entire tx message to be sent
  // UART Transmit FIFO Empty =1, when Tx done
  while((UART2_FR_R&UART_FR_TXFE) == 0);
  // wait until not busy
  while((UART2_FR_R&UART_FR_BUSY));
}

