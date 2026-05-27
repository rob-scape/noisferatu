#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>

// --------------------------------------------------
// Pin Definitions
// --------------------------------------------------
#define DAC_PIN A0
#define PARAM1_PIN A1
#define PARAM2_PIN A2
#define BITCRUSH_PIN A3      // Global bitcrush control (1-10 bits)
#define SAMPLE_RATE_PIN A4   // Sample rate reduction (1x to 40x decimation, down to 400Hz)
#define VOLUME_PIN A5        // Master volume control (logarithmic)

/*
// Simple button controls for testing (replacing encoder)
#define BUTTON_PREV 7    // D7 - previous algorithm
#define BUTTON_ALGO 9    // D9 - next algorithm within bank
#define BUTTON_BANK 6   //  D6 - cycles through banks 
*/

// Buttons remapped
#define BUTTON_BANK 7   //  cycles through banks   // Btn1 BankUp 
#define BUTTON_PREV 9  //   previous algorithm    // Btn2 AlgoPrev
#define BUTTON_ALGO 6  //   next algorithm within bank //  Btn3 AlgoNext

// Old encoder pins (commented out for testing)
// #define ENCODER_CLK 9   // D9 - left/right detection
// #define ENCODER_DT 8    // D8 - left/right detection  
// #define ENCODER_SW 10   // D10 - click for bank switching

/*
// Bank select pins (for future SP3T switch implementation - currently using buttons)
#define BANK_A_PIN A3
#define BANK_B_PIN A4
#define BANK_C_PIN A5
*/


// --------------------------------------------------
// Audio Configuration
// --------------------------------------------------
#define SAMPLE_RATE 16000
#define DAC_RESOLUTION 10
#define ADC_RESOLUTION 12
#define DAC_CENTER 511  // 10-bit center point

// --------------------------------------------------
// Control Rate
// --------------------------------------------------
#define TOGGLE_SAMPLES (SAMPLE_RATE * 3)

// --------------------------------------------------
// Hardware State
// --------------------------------------------------
// Normalized pot values (0.0 to 1.0)
volatile float pot1_norm = 0.0f;
volatile float pot2_norm = 0.0f;

// Global bitcrush (1-10 bits, controlled by A3 pot)
volatile uint8_t globalBitcrush = 10;  // 10 = clean (no crush)

// Global sample rate reduction (controlled by A4 pot)
volatile uint8_t sampleRateDecimation = 1;  // 1 = full rate, 40 = heavily decimated

// Master volume (0-255, controlled by A5 pot with logarithmic curve)
volatile uint8_t masterVolume = 255;  // 255 = full volume, 0 = silent

// Logarithmic volume lookup table (256 entries, 0-255)
// Maps linear pot position to logarithmic volume curve
// Generated with: volume = (pot/255)^3 * 255 (cubic for smooth taper)
const uint8_t volumeLUT[256] PROGMEM = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,2,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,6,6,6,7,7,7,8,8,9,9,9,10,10,11,11,12,12,13,13,14,14,15,15,16,17,17,18,18,19,20,20,21,22,22,23,24,24,25,26,27,27,28,29,30,31,31,32,33,34,35,36,37,38,38,39,40,41,42,43,44,45,46,47,48,49,51,52,53,54,55,56,57,59,60,61,62,63,65,66,67,68,70,71,72,74,75,76,78,79,80,82,83,85,86,88,89,91,92,94,95,97,98,100,102,103,105,106,108,110,111,113,115,117,118,120,122,124,126,127,129,131,133,135,137,139,141,143,145,147,149,151,153,155,157,159,162,164,166,168,170,173,175,177,180,182,184,187,189,191,194,196,199,201,204,206,209,211,214,216,219,222,224,227,230,232,235,238,241,243,246,249,252,255
};

// Sample & hold state for sample rate reduction
volatile int16_t heldSample = 0;
volatile uint8_t decimationCounter = 0;

// Global gate (future use)
volatile bool gate = true;

#endif // HARDWARE_H
