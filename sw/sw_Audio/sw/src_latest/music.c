// File **********music.c***********
// Programs to play pre-programmed music and respond to switch inputs
// Spring 2025



#include <stdint.h>
#include "../inc/tm4c123gh6pm.h"
#include "../src_latest/music.h"
#include "../inc/TLV5616.h"
#include "../inc/Timer0A.h"
#include "../inc/Timer1A.h"
#include "../inc/SysTickInts.h"

const uint16_t SineWave[32] = {
  2048, 2447, 2831, 3185, 3495, 3750, 3939, 4056, 
  4095, 4056, 3939, 3750, 3495, 3185, 2831, 2447, 
  2048, 1648, 1264, 910, 600, 345, 156, 39, 
  0, 39, 156, 345, 600, 910, 1264, 1648
};

// ---- Note PWM periods (80 MHz clock) ----

#define NOTE_A3   36364   // 220 Hz
#define NOTE_B3   32388   // 247 Hz
#define NOTE_CS4  28860   // C#4 ~277 Hz
#define NOTE_C4   30578   // 261.63 Hz
#define NOTE_D4   27241   // 294 Hz
#define NOTE_E4   24270   // 330 Hz
#define NOTE_Fs4  21622   // F#4 ~370 Hz
#define NOTE_A4   18182   // 440 Hz

#define REST         0

#define NOTE_E2    101936
#define NOTE_G2     81632
#define NOTE_A2     72728
#define NOTE_B2     64776

#define NOTE_C3     61156
#define NOTE_D3     54482
#define NOTE_E3     48540
#define NOTE_Fs3    43244
#define NOTE_G3     40816
#define NOTE_Fs2    86488

// Harmony notes: Using some lower thirds/fifths for All of Me
const Note_t AllOfMe_Harmony[] = {
		// 'Cause all of me
		{NOTE_B2,200}, {NOTE_G3,1200}, {NOTE_E3,200}, {NOTE_A3,800}, {REST, 250},

		// Loves all of you
		{NOTE_B2,800}, {NOTE_Fs3,1200}, {NOTE_E3,200}, {NOTE_G3,800}, {REST, 250},

		
		// Love your curves and all your edge
		{NOTE_B2,200}, {NOTE_B2,600}, {NOTE_C3,400},
		{NOTE_C3,400}, {NOTE_C3,400}, {NOTE_B2,200},
		{NOTE_C3,200},  {NOTE_C3,600}, {REST,250},

	
		// All your perfect imperfections
		{NOTE_G3,200}, {NOTE_G3,600}, {NOTE_Fs3,400},
		{NOTE_Fs3,400}, {NOTE_Fs3,400}, {NOTE_E3,200},
		{NOTE_Fs3,200},  {NOTE_Fs3,600}, {REST,250},

		/*
		// All your perfect imperfections
		{NOTE_G3,200}, {NOTE_G3,600}, {NOTE_E3,400},
		{NOTE_E3,400}, {NOTE_E3,400}, {NOTE_D3,200},
		{NOTE_Fs3,200},  {NOTE_Fs3,600}, {REST,250},
		*/
		
		// Give your all to me
		{NOTE_B2,200}, {NOTE_B2,600}, {NOTE_G3,1200}, {NOTE_E3,200}, {NOTE_A3,800}, {REST, 250},

		
		// I'll give my all to you
		{NOTE_B2,200}, {NOTE_B2,200}, {NOTE_B2,600},
		{NOTE_Fs3,1200}, {NOTE_E3,200}, {NOTE_G3,800}, {REST, 250},

		
		// You're my end and my beginning
		{NOTE_B2,200}, {NOTE_B2,600}, {NOTE_C3,400},
		{NOTE_C3,400}, {NOTE_C3,400}, {NOTE_B2,200},
		{NOTE_C3,200},  {NOTE_C3,600}, {REST,250},

		
		// Even when I lose I'm winning
		{NOTE_G3,200}, {NOTE_G3,600}, {NOTE_Fs3,400},
		{NOTE_Fs3,400}, {NOTE_Fs3,400}, {NOTE_E3,200},
		{NOTE_Fs3,200},  {NOTE_Fs3,600}, {REST,250},

		
		// 'Cause I give you all of me
		{NOTE_B2,200}, {NOTE_B2,200}, {NOTE_C3,200}, {NOTE_D3,200},
		{NOTE_B3,800}, {NOTE_A3,800}, {NOTE_G3,800}, {NOTE_Fs3,600},
		{NOTE_D3,200}, {NOTE_D3,1800}, {NOTE_Fs3, 400}, {REST, 250},

		
		// And you give me all of you
		{NOTE_B2,200}, {NOTE_B2,200}, {NOTE_C3,200}, {NOTE_D3,200},
		{NOTE_B3,800}, {NOTE_A3,800}, {NOTE_G3,800}, {NOTE_Fs3,600},
		{NOTE_D3,200}, {NOTE_D3,1200}, {NOTE_B2, 200}, {NOTE_E3, 200}, {NOTE_D3, 400}, {REST, 500},

		{REST, 10000}
};

const Note_t AllOfMe_Melody[] = {
		// 'Cause all of me
		{NOTE_B2,200}, {NOTE_D3,1200}, {NOTE_B2,200}, {NOTE_E3,800}, {REST, 250},

		// Loves all of you
		{NOTE_B2,800}, {NOTE_A2,1200}, {NOTE_G2,200}, {NOTE_B2,800}, {REST, 250},

		
		// Love your curves and all your edge
		{NOTE_B2,200}, {NOTE_B2,600}, {NOTE_A2,400},
		{NOTE_A2,400}, {NOTE_A2,400}, {NOTE_G2,200},
		{NOTE_A2,200},  {NOTE_A2,600}, {REST,250},


		// All your perfect imperfections
		{NOTE_B2,200}, {NOTE_B2,600}, {NOTE_A2,400}, 
		{NOTE_A2,400},  {NOTE_A2,400}, {NOTE_G2,200},
		{NOTE_A2,200}, {NOTE_A2,600}, {REST, 250},

		
		// Give your all to me
		{NOTE_B2,200}, {NOTE_B2,600}, {NOTE_D3,1200}, {NOTE_B2,200}, {NOTE_E3,800}, {REST, 250},

		// I'll give my all to you
		{NOTE_B2,200}, {NOTE_E3,200}, {NOTE_B2,600},
		{NOTE_A2,1200}, {NOTE_G2,200}, {NOTE_B2,800}, {REST, 250},

		// You're my end and my beginning
		{NOTE_B2,200}, {NOTE_B2,600}, {NOTE_A2,400},
		{NOTE_A2,400}, {NOTE_A2,400}, {NOTE_G2,200},
		{NOTE_A2,200},  {NOTE_A2,600}, {REST, 250},

		// Even when I lose I'm winning
		{NOTE_B2,200}, {NOTE_B2,600}, {NOTE_A2,400},
		{NOTE_A2,400}, {NOTE_A2,400}, {NOTE_G2,200},
		{NOTE_A2,200},  {NOTE_A2,600}, {REST, 250},


		// 'Cause I give you all of me
		{NOTE_B2,200}, {NOTE_B2,200}, {NOTE_C3,200}, {NOTE_D3,200},
		{NOTE_G3,800}, {NOTE_Fs3,800}, {NOTE_E3,800}, {NOTE_D3,600},
		{NOTE_B2,200}, {NOTE_B2,1800}, {NOTE_A2, 400}, {REST, 250},

		
		// And you give me all of you
		{NOTE_B2,200}, {NOTE_B2,200}, {NOTE_C3,200}, {NOTE_D3,200},
		{NOTE_G3,800}, {NOTE_Fs3,800}, {NOTE_E3,800}, {NOTE_D3,600},
		{NOTE_B2,200}, {NOTE_B2,1200}, {NOTE_G2, 200}, {NOTE_B2, 200}, {NOTE_A2, 400}, {REST, 500},


		{REST, 10000}
		
};

const uint32_t ALLOFME_NOTES = sizeof(AllOfMe_Melody)/sizeof(Note_t);

volatile uint8_t note_index = 0;
volatile uint32_t note_counter = 0;
volatile uint8_t Music_Playing = 0;

uint32_t NotePeriod_To_Ticks(uint32_t pwm_period){
  if(pwm_period == 0) return 80000000/100; // Slow idle for rests
  return pwm_period / 32;
}

void Music_Init(void){
  DAC_Init(0);
  Music_Playing = 0;
  DAC_Out(2048);
  
  // Interpreter runs every 1ms (80MHz/1000)
  SysTick_Init(80000); 
  
  // Initialize Timers with 2 arguments (Period, Priority) to match your headers
  Timer0A_Init(NotePeriod_To_Ticks(AllOfMe_Melody[0].period), 2);
  Timer1A_Init(NotePeriod_To_Ticks(AllOfMe_Harmony[0].period), 2);
}