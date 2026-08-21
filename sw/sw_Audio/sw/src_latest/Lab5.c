#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> 
#include "../inc/tm4c123gh6pm.h"
#include "../inc/PLL.h"
#include "../src_latest/music.h"
#include "Switch.h"
#include "../inc/TLV5616.h"
#include "../inc/SysTickInts.h"
#include "../src_latest/ff.h"
#include "diskio.h"
#include "../inc/UART.h"

#define LED (*((volatile uint32_t *)0x40025038))

// --- Manual UART5 Register Definitions (PE4/PE5) ---
#define UART5_DR_R              (*((volatile uint32_t *)0x40011000))
#define UART5_FR_R              (*((volatile uint32_t *)0x40011018))
#define UART5_IBRD_R            (*((volatile uint32_t *)0x40011024))
#define UART5_FBRD_R            (*((volatile uint32_t *)0x40011028))
#define UART5_LCRH_R            (*((volatile uint32_t *)0x4001102C))
#define UART5_CTL_R             (*((volatile uint32_t *)0x40011030))

#define UART_FR_RXFE            0x00000010  
#define UART_LCRH_WLEN_8        0x00000060  
#define UART_LCRH_FEN           0x00000010  
#define UART_CTL_UARTEN         0x00000001  

extern uint16_t buffer0[BSIZE];
extern uint16_t buffer1[BSIZE];
extern volatile int needRefill;
extern volatile uint8_t Music_Playing;

FATFS g_sFatFs;
FIL Handle;
UINT br;

char ESPBuffer[16];
uint32_t espIdx = 0;

void DisableInterrupts(void); 
void EnableInterrupts(void);  
void SSI0_Init(uint32_t CPSDVSR);

// =====================================================
// TIMER1 INIT: For Deterministic Offset Delays
// =====================================================
void Timer1_Init(void){
  SYSCTL_RCGCTIMER_R |= 0x02;   
  while((SYSCTL_PRTIMER_R&0x02)==0); 
  TIMER1_CTL_R = 0x00000000;    
  TIMER1_CFG_R = 0x00000000;    
  TIMER1_TAMR_R = 0x00000001;   
}

void Timer1_Wait(uint32_t delay_ms){
  if(delay_ms == 0) return;
  TIMER1_TAILR_R = 80000 * delay_ms - 1; 
  TIMER1_ICR_R = 0x00000001;    
  TIMER1_CTL_R |= 0x00000001;   
  while((TIMER1_RIS_R & 0x00000001) == 0){}; 
  TIMER1_ICR_R = 0x00000001;    
}

void UART5_Init(void){
  SYSCTL_RCGCUART_R |= 0x20;            
  SYSCTL_RCGCGPIO_R |= 0x10;            
  while((SYSCTL_PRGPIO_R&0x10) == 0){};
  UART5_CTL_R &= ~UART_CTL_UARTEN;      
  UART5_IBRD_R = 43;                    
  UART5_FBRD_R = 26;                    
  UART5_LCRH_R = (UART_LCRH_WLEN_8|UART_LCRH_FEN);
  UART5_CTL_R |= UART_CTL_UARTEN;       
  GPIO_PORTE_AFSEL_R |= 0x30;           
  GPIO_PORTE_DEN_R |= 0x30;             
  GPIO_PORTE_PCTL_R = (GPIO_PORTE_PCTL_R&0xFF00FFFF)+0x00110000;
  GPIO_PORTE_AMSEL_R &= ~0x30;          
}

void UART5_OutChar(char data){
    while((UART5_FR_R & 0x0020) != 0); // Wait until TX FIFO is not full
    UART5_DR_R = data;
}

// Used during PRELOAD to wait for 'R' from Python on UART0
int Wait_For_Char(char target) {
    uint32_t timeout = 0;
    while(timeout < 8000000) { 
        if((UART0_FR_R & UART_FR_RXFE) == 0) { 
            if((UART0_DR_R & 0xFF) == target) return 1; 
        }
        timeout++;
    }
    return 0; 
}

// Used during PLAY to wait for 'V' from Python OR 'S' from ESP
int Wait_For_Start(void) {
    uint32_t timeout = 0;
    while(timeout < 8000000) { 
        // 1. Check UART0 (Master TM4C listening for 'V' from Python)
        if((UART0_FR_R & UART_FR_RXFE) == 0) { 
            if((UART0_DR_R & 0xFF) == 'V') return 1; 
        }
        
        // 2. Check UART5 (Controller TM4C listening for 'S' from ESP)
        if((UART5_FR_R & UART_FR_RXFE) == 0) { 
            if((UART5_DR_R & 0xFF) == 'S') return 1; 
        }
        
        timeout++;
    }
    return 0; // Timeout reached
}

// Transparently passes data from ESP (UART5) to Python (UART0)
void CheckForESPScore(void) {
    while((UART5_FR_R & UART_FR_RXFE) == 0) { 
        // Read character from ESP
        char c = (char)(UART5_DR_R & 0xFF);
        
        // Forward the exact character straight to Python
        while((UART0_FR_R & 0x0020) != 0); // Wait for UART0 TX to be ready
        UART0_DR_R = c;
    }
}

int main(void){
    PLL_Init(Bus80MHz);
    DisableInterrupts();
    
    UART_Init();       
    UART5_Init();      
    PortC_Switches_Init();    
    DAC_Init(0);   
    Timer1_Init();
    
    SYSCTL_RCGCGPIO_R |= 0x20;
    while((SYSCTL_PRGPIO_R&0x20)==0);
    GPIO_PORTF_DIR_R |= 0x0E;
    GPIO_PORTF_DEN_R |= 0x0E;
    LED = 0x04; 
    
    SSI0_Init(40);            
    EnableInterrupts(); 
    
    if(disk_initialize(0) != 0 || f_mount(&g_sFatFs, "", 0) != FR_OK){
        LED = 0x02; 
        while(1);
    }

    UART_OutString("TM4C System Ready.\n");
    LED = 0x00; 
    
    uint8_t lastButton = Buttons_Read(); 
    memset(&Handle, 0, sizeof(FIL));
    
    uint8_t systemReady = 0;
    char* fileName = 0;
    uint32_t reloadVal = 0;
    uint32_t songOffset = 0;
    
    while(1){
        if(Music_Playing) CheckForESPScore();

        uint8_t currentButton = Buttons_Read(); 
        
        // --- 1. SONG SELECTION (Preload) ---
        if((currentButton & 0x20) && !(lastButton & 0x20)){
            fileName = "gang3.raw"; reloadVal = 1667; songOffset = 0;
            
            // Send '0' to ESP (No newline needed for single chars)
            UART5_OutChar('0'); 
            
            UART_OutString("G\n"); // Notify Python
            if(Wait_For_Char('R')) systemReady = 1; 
        }
        else if((currentButton & 0x10) && !(lastButton & 0x10)){
            fileName = "party3.raw"; reloadVal = 1814; songOffset = 200;
            
            // Send '1' to ESP (No newline needed for single chars)
            UART5_OutChar('1'); 
            
            UART_OutString("P\n"); // Notify Python
            if(Wait_For_Char('R')) systemReady = 1; 
        }

   // --- 2. EXECUTE SYSTEM ---
        if((currentButton & 0x08) && !(lastButton & 0x08)){
            if(systemReady && fileName != 0){
                systemReady = 0; 
                NVIC_ST_CTRL_R = 0; Music_Playing = 0; LED = 0x04; 
                if(Handle.fs) f_close(&Handle);
                
                // >>> SEND 'B' TO ESP D (Controller 1) <<<
                UART5_OutChar('B'); 

                // Wait 1 second using the hardware timer
                Timer1_Wait(1000);

                // >>> SEND 'R' TO ESP D (Controller 2) <<<
                UART5_OutChar('R'); 

                if(f_open(&Handle, fileName, FA_READ) == FR_OK){
                    f_read(&Handle, buffer0, 1024, &br); 
                    f_read(&Handle, buffer1, 1024, &br); 
                    while((UART0_FR_R & UART_FR_RXFE) == 0) { volatile char dummy = UART0_DR_R; }
                    
                    UART_OutString("X\n"); // Tell Python to play
                    
                    // >>> Wait for Python ('V') OR ESP ('S') <<<
                    if(Wait_For_Start()){ 
                        Timer1_Wait(songOffset);
                        SysTick_Init(reloadVal); Music_Playing = 1; LED = 0x08; 
                    } else { LED = 0x02; f_close(&Handle); }
                } else { LED = 0x02; }
            }
        }
        lastButton = currentButton;

        // --- 3. REFILL BUFFER ---
        if(needRefill != -1){
            int toFill = needRefill; needRefill = -1; 
            if(toFill == 0) f_read(&Handle, buffer0, 1024, &br);
            else            f_read(&Handle, buffer1, 1024, &br);
            if(br < 1024) f_lseek(&Handle, 0); 
        }
    }
}