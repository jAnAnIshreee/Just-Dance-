/*!
 * @defgroup RSLK
 * @brief RSLK human controller using joystick and switches
 <table>
<caption id="RSLKjoystick pins">Joystick pins</caption>
<tr><th> TM4C <th> MSPM0 <th> Description
<tr><td> xxx <td> xxx <td> X position (ADC)
<tr><td> yyy <td> yyy <td> Y position (ADC)
<tr><td> zzz <td> zzz  <td> Switch  GPIO input 

</table>
 * @{*/
/**
 * @file      JoyStick.h
 * @brief     RSLK human controller using joystick and switches
 * @details   Using two analog inputs and some switches to control RSLK<br>

 * @version   RSLK v2.02
 * @author    Your name
 * @copyright Lab solution, do not post,
 * @warning   AS-IS
 * @note      For more information see  http://users.ece.utexas.edu/~valvano/
 * @date      Jan 12, 2024

  ******************************************************************************/

#ifndef __JOYSTICK_H__
#define __JOYSTICK_H__
#include <stdint.h>


/**
 * Initialize joystick 
 * @param none
 * @return none
 * @brief  Initialize joystick and switches
 * @note Uses two analog inputs and some switches
 */
void JoyStick_Init(void);
// write this



#endif
