
// ----------------------------------------------------------------
// File name:     esp8266.c
// Description:   This code is used to bridge the TM4C123 board 
//                and the MQTT Web Application via the ESP8266. 
//                It does not use the preferred FIFO interface.
//
// Author:        Mark McDermott
// Date:          June 21, 2023
// Updated        July 23, 2024
//
// -----------------------------------------------------------------

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "inc/tm4c123gh6pm.h"

#include "../inc/UART.h"
#include "../inc/UART2.h"


#define     DEBUG1                // First level of Debug
//#undef      DEBUG1                // Comment out to enable DEBUG1

#define     DEBUG2                // Second level of Debug
//#undef      DEBUG2                // Comment out to enable DEBUG2

#define     DEBUG3                // Third level of Debug
#undef      DEBUG3                // Comment out to enable DEBUG3


#define     UART2_FR_TXFF            0x00000020  // UART Transmit FIFO Full
#define     UART2_FR_RXFE            0x00000010  // UART Receive FIFO Empty
#define     UART2_LCRH_WLEN_8        0x00000060  // 8 bit word length
#define     UART2_LCRH_FEN           0x00000010  // UART Enable FIFOs
#define     UART2_CTL_UARTEN         0x00000001  // UART Enable


#define     PB4     (*((volatile uint32_t *)0x40005040))        // RDY from ESP 8266  
#define     PB5     (*((volatile uint32_t *)0x40005080))        // RST_B to ESP8266
  
#define     RDY     (*((volatile uint32_t *)0x40005040))        // RDY from ESP 8266

#define     LOW   0x0
#define     HIGH  0x1



// --------------------------------------------------------------------------------------------------
// --------  MQTT connection variables for UT Broker        -----------------------------------------
// --------------------------------------------------------------------------------------------------

//#define     UNITY
//#define     UTOFFICE
//#define     LAB445
//#define     UTIOT
#define       HOTSPOT

// +++++++++++++++++++++++++++++++++++++++++++++++++
// Unity (CIA) Broker  -- use this string to access broker
//    mcdermot,CIA,65$HemiCuda,192.168.0.111,1883,

#ifdef UNITY
    char    eid[32]             = "mcdermot";
    char    ssid[32]            = "CIA";        
    char    pass[32]            = "65$HemiCuda";  
    char    mqtt_broker[32]     = "192.168.0.111";           // MQTT Broker Unity 
    char    mqtt_port[8]        = "1883";
 #endif

// ++++++++++++++++++++++++++++++++++++++++++++++++++
// EER 5.826 Broker
//

#ifdef UTOFFICE

    char    eid[32]             = "mcdermot";                    // Your EID goes here
    char    ssid[32]            = "EE-IOT-Platform-02";          // WiFi Access Point in the 445L Lab
    char    pass[32]            = "g!TyA>hR2JTy";
//    char    mqtt_broker[32]     = "10.159.177.113";              // MQTT Broker in EER
    char    mqtt_broker[32]     = "10.147.38.102";              // MQTT Broker MAC in my office
    char    mqtt_port[8]        = "1883";
#endif

// +++++++++++++++++++++++++++++++++++++++++++++++++
// 445L Lab Access to EER 5.826 Broker
//

#ifdef LAB445
    char    eid[32]             = "mcdermot";                    // Your EID goes here
    char    ssid[32]            = "EE-IOT-Platform-02";          // WiFi Access Point in the 445L Lab
    char    pass[32]            = "g!TyA>hR2JTy";
    char    mqtt_broker[32]     = "10.159.177.113";              // MQTT Broker in EER
    char    mqtt_port[8]        = "1883";
#endif

// +++++++++++++++++++++++++++++++++++++++++++++++++
// Utexas_IOT Access to EER 5.826 Broker
//

#ifdef UTIOT
    char    eid[32]             = "mcdermot";                    // Your EID goes here
    char    ssid[32]            = "utexas-iot";                  // WiFi Access Point in 5.826
    char    pass[32]            = "81546122050364296780";
    char    mqtt_broker[32]     = "10.159.177.113";              // MQTT Broker in EER
    char    mqtt_port[8]        = "1883";
#endif

// +++++++++++++++++++++++++++++++++++++++++++++++++
// Phone Hot Spot
//

#ifdef HOTSPOT
    char    eid[32]             = "mcdermot";                     // Your EID goes here
    char    ssid[32]            = "zoo";                      // WiFi Access Point in 5.826
    char    pass[32]            = "bullybully";
    char    mqtt_broker[32]     = "172.20.10.3";              // MQTT Broker in EER
    char    mqtt_port[8]        = "1883";
#endif

// -----------------------------------------------------------------------------

char    inchar;              

// --------------    DelayWait10ms     -------------------------------
// Subroutine to wait 10 msec
// Inputs: None
// Outputs: None
// Notes: 
//
void DelayWait10ms(uint32_t n){
	
	uint32_t volatile time;
  while(n){
    time = 727240*2/180;  
    while(time){
	  	time--;
    }
    n--;
  }
}

// ---------------------- Reset_8266 ----------------------------------
// Subroutine reset the WiFi chip
// Inputs: None
// Outputs: None
// Notes: 
//
void Reset_8266(void) 
{
  PB5 = 0x00;                   // Reset the 8266
  DelayWait10ms(500);           // Wait for 8266 to reset
  PB5 = 0x20;                   // Enable the 8266
  DelayWait10ms(500);           // Wait before setting up 8266
}

// --------------------   SetupWiFi -----------------------------------
//
// This routine sets up the Wifi connection between the TM4C and the
// hotspot. Enable the DEBUG flags if you want to watch the transactions.
// 

void SetupWiFi(void) 
{  
 
  #ifdef DEBUG1
    UART_OutString("\r\nIn WiFI_Setup routine\r\n");
    UART_OutString("Waiting for RDY flag from ESP\r\n");
    DelayWait10ms(500);
  #endif
  
 // Add in mini LED STUFF here 
 // ST7735_DrawString(0,0,"In WiFI_Setup routine", ST7735_Color565(255, 255, 255 ));
 // ST7735_DrawString(0,1,"Waiting for RDY flag ", ST7735_Color565(255, 255, 255 ));
  
//  while (!RDY) {      // Wait for ESP8266 indicate it is ready for programming data
//   
//    #ifdef DEBUG1
//      UART_OutString(".");
//    #endif
//    
//    DelayWait10ms(30);            // Slow down the printing of the period
//  
//  }

  UART2_OutString(eid);           // Student EID - Used for individualizing MQTT Topics
  UART2_OutChar(',');
	
  UART2_OutString(ssid);          // Send WiFi SSID to ESP8266
  UART2_OutChar(',');
  
  UART2_OutString(pass);          // Send WiFi Password to ESP8266
  UART2_OutChar(',');      
  
  UART2_OutString(mqtt_broker);   // Send IP address of MQTT Broker   
  UART2_OutChar(',');       
  
  UART2_OutString(mqtt_port);     // Send MQTT port number  
  UART2_OutChar(',');             // Extra comma needed for ESP8266 parser code
  
  UART2_OutChar('\n');            // Send NL to indicate EOT   

    #ifdef DEBUG2
      UART_OutChar('\n');
      UART_OutString("TM4C Sending: ");
      UART_OutString(eid);    
      UART_OutChar(',');
      UART_OutString(ssid);
      UART_OutChar(',');
      //UART_OutString(pass);
      //UART_OutChar(',');
      UART_OutString(mqtt_broker);
      UART_OutChar(',');
      UART_OutString(mqtt_port);
      UART_OutChar(',');
      UART_OutChar('\n');      
    #endif
    
    #ifdef DEBUG3
//  Change all this to mini LCD
//    ST7735_DrawString(0,3,ssid, ST7735_Color565(255, 0, 0 ));
//    ST7735_DrawString(0,4,pass, ST7735_Color565(255, 0, 0 ));
//    ST7735_DrawString(0,5,eid, ST7735_Color565(255, 0, 0 ));
//    ST7735_DrawString(0,6,mqtt_broker, ST7735_Color565(255, 0, 0 ));
//    ST7735_DrawString(0,7,mqtt_port, ST7735_Color565(255, 0, 0 ));
    #endif
  
  //
  // This while loop receives debug info from the 8266 and optionally 
  // sends it out the debug port. The loop exits once the RDY signal
  // is deasserted and the serial port has no more character to xmit
  //
  
//    while ((!RDY) | ((UART2_FR_R & UART2_FR_RXFE) ==0)) { 

//  
//    
//    if ((UART2_FR_R & UART2_FR_RXFE) ==0 ) 
//    {
//        inchar =(UART2_DR_R & 0xFF);      

//      #ifdef DEBUG1
//        UART_OutChar(inchar);                        // Output received character
//      #endif
//    
//    }
//    else if ((UART2_FR_R & UART2_FR_RXFE) !=0 ) { }     // Do nothing
//    
//  }

 
 
  DelayWait10ms(50);
  
  #ifdef DEBUG1
    UART_OutString("\r\nExiting WiFI_Setup routine\r\n");
  #endif
  
    
}


