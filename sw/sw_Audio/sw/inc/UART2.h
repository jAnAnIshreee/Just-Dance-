//UART2.h
//Dung Nguyen
//Wally Guzman

#include <stdint.h> 
// PD7=U2Tx PD6=U2Rx

void UART2_Init(uint32_t baud);

//--------UART2_OutChar--------
// Prints a character to UART1
// Inputs: character to transmit
// Outputs: none
void UART2_OutChar(char data);
void UART2_OutString(char *pt);

//--------UART2_EnableRXInterrupt--------
// Enables UART2 RX interrupt
// Inputs: none
// Outputs: none
void UART2_EnableRXInterrupt(void);

//--------UART2_DisableRXInterrupt--------
// Disables UART2 RX interrupt
// Inputs: none
// Outputs: none
void UART2_DisableRXInterrupt(void);

//------------UART2_FinishOutput------------
// Wait for all transmission to finish
// Input: none
// Output: none
void UART2_FinishOutput(void);
