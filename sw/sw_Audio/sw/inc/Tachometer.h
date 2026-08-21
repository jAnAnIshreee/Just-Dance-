/*!
 * @defgroup RSLK
 * @brief RSLK tachometer interface
 <table>
<caption id="RSLKtach pins">RSLK RSLK motor pins</caption>
<tr><th> TM4C <th> MSPM0 <th> Description
<tr><td> PB7  <td> PB8   <td> ELA  T0CCP1
<tr><td> PB2  <td> PB12  <td> ERA  T3CCP0
<tr><td>      <td>       <td> ERB  GPIO input 
<tr><td>      <td>       <td> ELB  GPIO input 
</table>
 * @{*/
/**
 * @file      Tachometer.h
 * @brief     RSLK tachometer
 * @details   Using input capture to measure motor speed<br>

 * @version   RSLK v2.02
 * @author    Daniel Valvano and Jonathan Valvano
 * @copyright Copyright 2024 by Jonathan W. Valvano, valvano@mail.utexas.edu,
 * @warning   AS-IS
 * @note      For more information see  http://users.ece.utexas.edu/~valvano/
 * @date      June 30, 2024
 <table>
<caption id="RSLKtach2">RSLK Bumper switches</caption>
<tr><th> TM4C <th> MSPM0 <th> Description
<tr><td> PB7  <td> PB8   <td> ELA  T0CCP1
<tr><td> PB2  <td> PB12  <td> ERA  T3CCP0
<tr><td>      <td>       <td> ERB  GPIO input 
<tr><td>      <td>       <td> ELB  GPIO input 
</table>
  ******************************************************************************/

#ifndef __TACHOMETER_H__
#define __TACHOMETER_H__
#include <stdint.h>

/**
 * Check for stopped motors
 * Higher level software must run this every 10ms
 * @param none
 * @return none
 * @brief  Check for stopped motors
 * @note If the wheel is not spinning there will be no input capture interrupts
 */
void Tachometer_CheckForStopped(void); // run this every 10ms

/**
 * Initialize RSLK tachometers
 * @param none
 * @return none
 * @brief  Initialize motors
 * @note Uses input capture on ELA and ERA
 */
void Tachometer_Init(void);

/** 
 * Right motor speed in 0.1 rpm.
 * Does not spin, returns with last measurement
 * @param none
 * @return Rightrpm
 * @brief  Right motor speed
 * @note Uses input capture on ERA
 */ 
int32_t Tachometer_GetRightrpm(void);

/** 
 * Left motor speed in 0.1 rpm.
 * Does not spin, returns with last measurement
 * @param none
 * @return Leftrpm
 * @brief  Left motor speed
 * @note Uses input capture on ELA 
 */ 
int32_t Tachometer_GetLeftrpm(void);

#endif
