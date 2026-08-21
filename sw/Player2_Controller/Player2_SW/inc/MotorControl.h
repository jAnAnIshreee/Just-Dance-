/*!
 * @defgroup RSLK
 * @brief RSLK PI Controller
 <table>
<caption id="RSLKtach pins4">RSLK RSLK motor pins</caption>
<tr><th> TM4C <th> MSPM0 <th> Description
<tr><td> PB7  <td> PB8   <td> ELA  T0CCP1
<tr><td> PB6  <td> PB7   <td> ERB  GPIO input 
<tr><td> PA4  <td> PB6   <td> ELB  GPIO input 
<tr><td> PB2  <td> PB12  <td> ERA  T3CCP0
</table>
 <table>
<caption id="RSLKmotor4">RSLK motor pins</caption>
<tr><th> TM4C <th> MSPM0  <th> Description
<tr><td> PF2  <td> PB4  <td> Motor_PWML, ML+, IN3, PWM M1PWM6
<tr><td> PF3  <td> PB1  <td> Motor_PWMR, MR+, IN1, PWM M1PWM7
<tr><td> PA3  <td> PB0  <td> Motor_DIR_L,ML-, IN4, GPIO, 0 means forward, 1 means backward
<tr><td> PA2  <td> PB16 <td> Motor_DIR_R,MR-, IN2, GPIO, 0 means forward, 1 means backward
</table>
 * @{*/
/**
 * @file      MotorControl.h
 * @brief     RSLK PI motor controller
 * @details   Using input capture input and PWM output to control motor speed<br>

 * @version   RSLK v2.02
 * @author    Daniel Valvano and Jonathan Valvano
 * @copyright Copyright 2024 by Jonathan W. Valvano, valvano@mail.utexas.edu,
 * @warning   AS-IS
 * @note      For more information see  http://users.ece.utexas.edu/~valvano/
 * @date      July 5, 2024
 <table>
<caption id="RSLKtach pins5">RSLK RSLK motor pins</caption>
<tr><th> TM4C <th> MSPM0 <th> Description
<tr><td> PB7  <td> PB8   <td> ELA  T0CCP1
<tr><td> PB6  <td> PB7   <td> ERB  GPIO input 
<tr><td> PA4  <td> PB6   <td> ELB  GPIO input 
<tr><td> PB2  <td> PB12  <td> ERA  T3CCP0
</table>
 <table>
<caption id="RSLKmotor5">RSLK motor pins</caption>
<tr><th> TM4C <th> MSPM0  <th> Description
<tr><td> PF2  <td> PB4  <td> Motor_PWML, ML+, IN3, PWM M1PWM6
<tr><td> PF3  <td> PB1  <td> Motor_PWMR, MR+, IN1, PWM M1PWM7
<tr><td> PA3  <td> PB0  <td> Motor_DIR_L,ML-, IN4, GPIO, 0 means forward, 1 means backward
<tr><td> PA2  <td> PB16 <td> Motor_DIR_R,MR-, IN2, GPIO, 0 means forward, 1 means backward
</table>
  ******************************************************************************/

#ifndef __MOTORCONTOL_H__
#define __MOTORCONTOL_H__
#include <stdio.h>
#include <stdint.h>
/**
 * Initialize RSLK Controller
 * @param none
 * @return none
 * @brief  Initialize tach input and motor outputs
 * @note period is 40000 us
 */
void MC_init(void);

/**
 * Set desired speed in 0.1 rpm.
 * Robot should move in a straight line
 * @param newSpeed
 * @return none
 * @brief  Set desired speed
 */
void MC_SetDesiredSpeed(uint32_t newSpeed);

/**
 * Get desired speed in 0.1 rpm.
 * @param newSpeed
 * @return desiredSpeed
 * @brief  Get desired speed
 */
uint32_t MC_GetDesiredSpeed(void);

/**
 * Set Kp1
 * @param kp1
 * @return none
 * @brief  Set numerator for proportial control
 */
void MC_SetKp1(int32_t kp1);

/**
 * Set Kp2
 * @param kp2
 * @return none
 * @brief  Set denominator for proportial control
 */
void MC_SetKp2(int32_t kp2);

/**
 * Set Ki1
 * @param ki1
 * @return none
 * @brief  Set numerator for integral control
 */
void MC_SetKi1(int32_t ki1);

/**
 * Set Ki2
 * @param ki2
 * @return none
 * @brief  Set denominator for integral control
 */
void MC_SetKi2(int32_t ki2);

/**
 * Get Kp1
 * @param none
 * @return Kp1
 * @brief  Get numerator for proportial control
 */
int32_t MC_GetKp1(void);

/**
 * Get Kp2
 * @param none
 * @return Kp2
 * @brief  Get denominator for proportial control
 */
int32_t MC_GetKp2(void);

/**
 * Get Ki1
 * @param none
 * @return Ki1
 * @brief  Get numerator for integral control
 */
int32_t MC_GetKi1(void);

/**
 * Get Ki2
 * @param none
 * @return Ki2
 * @brief  Get denominator for integral control
 */
int32_t MC_GetKi2(void);

/**
 * Get LeftE
 * @param none
 * @return LeftE
 * @brief  Get controller error on left
 */
int32_t MC_GetLeftE(void);

/**
 * Get RightE
 * @param none
 * @return RightE
 * @brief  Get controller error on right
 */
int32_t MC_GetRightE(void);

/**
 * Get LeftU
 * @param none
 * @return LeftU
 * @brief  Get controller actuator output on left
 */
int32_t MC_GetLeftU(void);

/**
 * Get RightU
 * @param none
 * @return RightU
 * @brief  Get controller actuator output on right
 */
int32_t MC_GetRightU(void);

/**
 * Get Time 
 * since last change in desired speed, 10 ms
 * @param none
 * @return Time
 * @brief  Get controller time
 * @note Controller runs at 100Hz
 */
 uint32_t MC_Time(void); 
 
#define DUMPSIZE 200
// get dump array pointers
// left and right speeds are dumped after each change in desired speed
void MC_DumpSpeed(uint32_t *leftspeed, uint32_t *rightspeed);
uint32_t *MC_DumpRight(void);
uint32_t *MC_DumpLeft(void);
#endif
