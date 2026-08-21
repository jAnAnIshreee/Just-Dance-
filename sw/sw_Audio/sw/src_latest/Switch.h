// File **********Switch.h***********
// Lab 5
// Programs to interface with Switch buttons   
// Spring 2025

// 2-bit input, positive logic switches, positive logic software

#ifndef SWITCH_H
#define SWITCH_H

#include <stdint.h>

// ----- Function Prototypes -----
void PortC_Switches_Init(void);
uint8_t Buttons_Read(void);

#endif