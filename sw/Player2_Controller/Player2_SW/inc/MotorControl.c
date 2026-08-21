// ----------------------------------------------------------------------
//
// MotorControl.c
// 
// ----------------------------------------------------------------------
// 
//  This code runs the Motor Controller 
//  Mark McDermott  June 21, 2018
//  Jonathan Valvano Jan 12, 2025
//
//
// Two DC motor with L293 driver
// Bump switches
//   TM4C MSPM0
//   PA5  PA27    Left, Bump 0,
//   PF0  PB15    Center Left, Bump 1,
//   PB3  PA28    Center Right, Bump 2
//   PC4  PA31    Right, Bump 3

// Motor
//   TM4C MSPM0
//   PF2  PB4  Motor_PWML, ML+, IN3, PWM M1PWM6
//   PF3  PB1  Motor_PWMR, MR+, IN1, PWM M1PWM7
//   PA3  PB0  Motor_DIR_L,ML-, IN4, GPIO 0 means forward, 1 means backward
//   PA2  PB16 Motor_DIR_R,MR-, IN2, GPIO 0 means forward, 1 means backward

// tachometer
//   TM4C MSPM0
//   PB7  PB8  ELA  T0CCP1
//   PB6  PB7  ERB  GPIO input 
//   PA4  PB6  ELB  GPIO input 
//   PB2  PB12 ERA  T3CCP0

// SSD1306 I2C OLED
//   TM4C MSPM0
//   PA6  PB2  SCL I2C clock
//   PA7  PB3  SDA I2C data

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../inc/tm4c123gh6pm.h"
#include "../inc/CortexM.h"
#include "../inc/Motor.h"
#include "../inc/Timer2A.h"
#include "../inc/MotorControl.h"
#include "../inc/Tachometer.h"
#include "../inc/UART0int.h"
    // write this

