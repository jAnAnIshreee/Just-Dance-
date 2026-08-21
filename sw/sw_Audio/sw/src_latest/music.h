// ********** music.h **********
// Header for music playback using SysTick + DAC
// Spring 2025

#ifndef __MUSIC_H__
#define __MUSIC_H__

#include <stdint.h>

typedef struct {
  uint32_t period;
  uint32_t duration; // in ms
} Note_t;

// Constants used across files
#define REST 0
extern const uint16_t SineWave[32];
extern const Note_t AllOfMe_Melody[];
extern const Note_t AllOfMe_Harmony[];
extern const uint32_t ALLOFME_NOTES;

// Shared Global Variables
extern volatile uint8_t Music_Playing;
extern volatile uint8_t note_index;
extern volatile uint32_t note_counter;
extern volatile uint32_t Envelope_Melody;
extern volatile uint32_t Envelope_Harmony;

void Music_Init(void);
uint32_t NotePeriod_To_Ticks(uint32_t pwm_period);

#endif