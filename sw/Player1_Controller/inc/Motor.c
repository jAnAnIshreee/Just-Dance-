/* Motor.c
 * Jonathan Valvano
 * RSLK v2.0.2
 * June 28, 2024
// Motor (not LM4F120)
// TM4C MSPM0
// PF2  PB4  Motor_PWML, ML+, IN3, PWM M1PWM6
// PF3  PB1  Motor_PWMR, MR+, IN1, PWM M1PWM7
// PA3  PB0  Motor_DIR_L,ML-, IN4, GPIO, 0 means forward, 1 means backward
// PA2  PB16 Motor_DIR_R,MR-, IN2, GPIO, 0 means forward, 1 means backward
 *
 */
#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"
#include "../inc/Motor.h"
#include "../inc/Clock.h"
    // write this

void Motor_Init(void){
}

void Motor_Forward(uint32_t dutyLeft, uint32_t dutyRight){

}

void Motor_Backward(uint32_t dutyLeft, uint32_t dutyRight){

}

void Motor_Right(uint32_t dutyLeft, uint32_t dutyRight){
  
}

void Motor_Left(uint32_t dutyLeft, uint32_t dutyRight){
 
}



