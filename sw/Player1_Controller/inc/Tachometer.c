// Tachmeter.c
// Runs on TM4C123 (not LF120)
// Jonathan Valvano
// Jan 6, 2025

// tachometer
//   TM4C MSPM0
//   PB7  PB8  ELA  T0CCP1
//   PB2  PB12 ERA  T3CCP0
//             ERB  GPIO input  (not used)
//             ELB  GPIO input  (not used)

#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"
#include "../inc/InputCapture.h"
    
// write this