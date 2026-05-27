#include "hardware.h"
#include "algos.h"
#include "params.h"
#include <TM1637Display.h>
#include "sample.h"

// --------------------------------------------------
// TM1637 Display Setup
// --------------------------------------------------
#define DISPLAY_CLK A8   // SCK pin
#define DISPLAY_DIO 10   //  D10 (was A6) TX pin
#define DISPLAY_TIMEOUT_MS 500    // How long to show display on button press (adjust here!)
#define DISPLAY_HOLD_MS    2000   // How long to hold bank button to toggle display timeout mode (adjust here!)

// Create display object (CLK, DIO)
TM1637Display display(DISPLAY_CLK, DISPLAY_DIO);

// --------------------------------------------------
// Bank and Algorithm Organization
// --------------------------------------------------
// Bank 1: Wavetables (Generative Waveforms)
// Bank 2: Noisy Textures 
// Bank 3: Bitbend Wavetables
// Bank 4: Blips And Tones
// Bank 5: Logic Disorder 

enum Bank {
  BANK_WAVETABLES = 0,
  BANK_NOISY_TEXTURES,
  BANK_BITBEND,
  BANK_BLIPS,
  BANK_LOGIC,
  BANK_COUNT
};

// Algorithms per bank
enum WavetableAlgos {
  WT_GENERATIVE_1 = 0, // VARIANT 1: Sparse Glitchy | pot1: speed, pot2: silence
  WT_GENERATIVE_2,     // VARIANT 2: Dense Microglitch | pot1: speed, pot2: silence
  WT_GENERATIVE_3,     // VARIANT 3: Spacey Pulses | pot1: speed, pot2: silence
  WT_GENERATIVE_4,     // VARIANT 4: Random Jump Glitch | pot1: speed, pot2: silence
  WT_GENERATIVE_5,     // VARIANT 5: Wandering Window | pot1: speed, pot2: walk rate
  WT_GENERATIVE_6,     // VARIANT 6: Manual Window with Spray | pot1: window pos, pot2: window size
  WT_GENERATIVE_7,     // VARIANT 7: Noise or Saw Window | pot1: speed, pot2: walk rate
  WT_GENERATIVE_17,    // VARIANT 17: NEW buffer approach | pot1: TBD, pot2: TBD
  WT_GENERATIVE_18,    // VARIANT 18: BitBend Quad (XOR+HOLD+SET_0+SET_1) | pot1: speed, pot2: bit clock
  WT_ALGO_COUNT
};


enum NoisyTextureAlgos {
  NT_LATCHED_NOISE = 0, // pot1: clock prob, pot2: clock rate
  NT_DUST,              // pot1: lowpass cutoff, pot2: density
  NT_FMNOISE,           // pot1: base freq, pot2: noise S&H rate
  NT_NOISEGATES,        // pot1: base freq, pot2: random walk step
  NT_SAW_CLICKS,        // pot1: saw1 freq, pot2: saw2 freq
  NT_NOISE_NOR_NOISE,   // was NT_TWO_SAWS — pot1: S&H1 rate, pot2: S&H2 rate
  NT_DUST_BURST,        // pot1: burst speed, pot2: dust density
  NT_HIGHPASS_NOISE,    // pot1: AM depth (inverted), pot2: AM rate // Trainwreck
  NT_VINYL_CRACKLE, //NT_HIGH_NOISE_RHYTHM, // pot1: clock speed, pot2: clock division (1-7)
  NT_ALGO_COUNT
};

// BitBend Wavetables Bank
enum BitBendAlgos {
  BB_CHAOS = 0,        // BitBend Chaos (full generative) | pot1: speed, pot2: bit clock
  BB_SPARSE,           // BitBend Sparse (mode select) | pot1: speed, pot2: mode
  BB_DUAL,             // BitBend Dual (XOR+HOLD) | pot1: speed, pot2: bit clock
  BB_FREEZE,           // BitBend Freeze (SET_0+HOLD) | pot1: speed, pot2: bit clock
  BB_PING,             // BitBend Ping (XOR+SET_1) | pot1: speed, pot2: bit clock
  BB_MIRROR,           // BitBend Mirror (SET_0+SET_1) | pot1: speed, pot2: bit clock
  BB_TRIPLE,           // BitBend Triple (XOR+HOLD+SET_0) | pot1: speed, pot2: bit clock
  BB_SWEEP,            // BitBend Sweep (dual HOLD speeds) | pot1: speed, pot2: bit clock
  BB_TRIPLE_B,         // BitBend Triple B (SET_0+SET_1+HOLD) | pot1: speed, pot2: bit clock
  BB_ALGO_COUNT
};

enum BlipAndTonesAlgos {
  BLIP_RANDOM_TRIANGLE = 0, // pot1: trigger rate, pot2: env decay
  BLIP_HARMONIC_TRIS,       // pot1: LFO1 rate, pot2: LFO2 rate
  BLIP_FAST_TRIANGLE,       // pot1: frequency, pot2: env decay
  BLIP_PHRYGIAN_TRI,        // pot1: burst speed, pot2: root freq (SWAPPED!)
  BLIP_RING_MOD,            // pot1: osc1 freq, pot2: osc2 freq
  BLIP_NOISE_OR_SQUARE,     // pot1: noise S&H rate, pot2: square freq
  BLIP_MAJOR_TRIS,          // pot1: LFO1 rate, pot2: LFO2 rate
  BLIP_BERNOULLI_MINOR7,    // pot1: gate1 prob, pot2: gate2 prob (RENAMED - minor 7th chord!)
  BLIP_PENTATONIC_BLIPS,    // pot1: clock speed, pot2: env decay
  BLIP_ALGO_COUNT
};

enum LogicAlgos {
  LOGIC_THREE_CASCADE = 0, // pot1: freq1, pot2: freq2 (freq3 fixed)
  LOGIC_NOR_SQUARE,        // pot1: sq1 freq, pot2: sq2 freq
  LOGIC_TRI_OR_SAW,        // pot1: tri freq, pot2: saw freq
  LOGIC_TRI_NOR_TRI,       // pot1: tri1 freq, pot2: tri2 freq
  LOGIC_TRI_XOR_TRI,       // pot1: tri1 freq, pot2: tri2 freq
  LOGIC_SQUARE_XNOR,       // pot1: sq1 freq, pot2: sq2 freq
  LOGIC_SQUARE_NAND,       // pot1: sq1 freq, pot2: sq2 freq
LOGIC_TWO_SAWS,       // was LOGIC_NOISE_NOR_NOISE — pot1: saw1 freq, pot2: saw2 freq
  LOGIC_SQUARE_OR_SQUARE,  // pot1: sq1 freq, pot2: sq2 freq
  LOGIC_ALGO_COUNT
};

// Current selection
volatile Bank currentBank = BANK_WAVETABLES;
volatile uint8_t currentAlgo = 0;

// Algorithm counts per bank
const uint8_t algoCountPerBank[BANK_COUNT] = {
  WT_ALGO_COUNT,      // Bank 1: 9 generative waveforms (FULL!)
  NT_ALGO_COUNT,      // Bank 2: 9 noisy textures
  BB_ALGO_COUNT,      // Bank 3: BitBend (9 algos - COMPLETE!)
  BLIP_ALGO_COUNT,    // Bank 4: 9 blips and tones
  LOGIC_ALGO_COUNT    // Bank 5: 9 logic operations
};

// --------------------------------------------------
// Display State (non-blocking timeout)
// --------------------------------------------------
bool displayOn              = false;  // Is the display currently lit?
unsigned long displayOnTime = 0;      // When it was last turned on
bool displayTimeoutEnabled      = false;  // true  = auto-off after DISPLAY_TIMEOUT_MS
                                          // false = stays on, only updates on button press
bool displayShowingConfirmation = false;  // true = showing on/oFF confirmation, return to algo after timeout

// --------------------------------------------------
// Display Helper Function
// --------------------------------------------------
void updateDisplay() {
  // Show: B[bank] A[algo]
  // Internal counting: 0-indexed
  // Display counting: 1-indexed (more intuitive)

  uint8_t displayBank = currentBank + 1;  // 0→1, 1→2, etc.
  uint8_t displayAlgo = currentAlgo + 1;  // 0→1, 1→2, etc.

  // Segment pattern for 'b' (lowercase)
  uint8_t segB = SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;

  // Segment pattern for 'A'
  uint8_t segA = SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;

  uint8_t segments[4];
  segments[0] = segB;                              // 'b'
  segments[1] = display.encodeDigit(displayBank);  // Bank number
  segments[2] = segA;                              // 'A'
  segments[3] = display.encodeDigit(displayAlgo);  // Algo number

  display.setBrightness(0x02);
  display.setSegments(segments);

  // Non-blocking: record when display came on, loop() handles turn-off
  displayOn     = true;
  displayOnTime = millis();
}

// --------------------------------------------------
// Button State Tracking (Simple Debounce)
// --------------------------------------------------
// We track last stable state and debounce time for each button
bool lastBankButtonState = HIGH;
bool lastAlgoButtonState = HIGH;
unsigned long lastBankButtonTime = 0;
unsigned long lastAlgoButtonTime = 0;
const unsigned long buttonDebounceMs = 50;  // 50ms debounce - works with bare wires

// --------------------------------------------------
// Timer Setup
// --------------------------------------------------
void setupTimer()
{
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(TC5_GCLK_ID) |
                      GCLK_CLKCTRL_CLKEN |
                      GCLK_CLKCTRL_GEN_GCLK0;
  while (GCLK->STATUS.bit.SYNCBUSY);

  TC5->COUNT16.CTRLA.bit.ENABLE = 0;
  while (TC5->COUNT16.STATUS.bit.SYNCBUSY);

  TC5->COUNT16.CTRLA.reg =
      TC_CTRLA_MODE_COUNT16 |
      TC_CTRLA_PRESCALER_DIV1 |
      TC_CTRLA_WAVEGEN_MFRQ;

  TC5->COUNT16.CC[0].reg = (uint16_t)(48000000 / SAMPLE_RATE);
  while (TC5->COUNT16.STATUS.bit.SYNCBUSY);

  TC5->COUNT16.INTENSET.reg = TC_INTENSET_MC0;
  NVIC_EnableIRQ(TC5_IRQn);

  TC5->COUNT16.CTRLA.bit.ENABLE = 1;
  while (TC5->COUNT16.STATUS.bit.SYNCBUSY);
}

// --------------------------------------------------
// Setup
// --------------------------------------------------
void setup()
{
  analogWriteResolution(DAC_RESOLUTION);
  analogReadResolution(ADC_RESOLUTION);
  
  // Setup buttons with internal pullups (active LOW when pressed to ground)
  pinMode(BUTTON_BANK, INPUT_PULLUP);  // D7 - bank switching 
  pinMode(BUTTON_ALGO, INPUT_PULLUP);  // D6 - algorithm next 
  pinMode(BUTTON_PREV, INPUT_PULLUP);  // D9 - previous algorithm
  
  // NO interrupts - simple polling in loop() for maximum reliability

  delay(300);  // give USB time to enumerate
  
  // Initialize TM1637 display (CONSERVATIVE TEST)
  display.setBrightness(0x02);  // Low brightness (1-7, where 7=brightest)
  
  // Display "NOIS" on startup briefly
  uint8_t noiseText[] = {
    SEG_A | SEG_B | SEG_C | SEG_E | SEG_F,           // N
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,   // O
    SEG_B | SEG_C,                                    // I
    SEG_A | SEG_C | SEG_D | SEG_F | SEG_G            // S
  };
  display.setSegments(noiseText);
  
  delay(500);  // Show for half a second
  
  // Turn off display to stop refresh noise
  display.setBrightness(0x00);  // Brightness 0 = off
  uint8_t blank[] = {0, 0, 0, 0};
  display.setSegments(blank);
  
  // Generate initial waveform (Bank 1, Algo 0)
  generateWaveform1();

  setupTimer();

    // Show initial bank/algo on display
  updateDisplay();

}

// --------------------------------------------------
// Main Loop
// --------------------------------------------------
void loop()
{
  // Read pots
  uint16_t p1 = analogRead(PARAM1_PIN);
  uint16_t p2 = analogRead(PARAM2_PIN);
  uint16_t pBitcrush = analogRead(BITCRUSH_PIN);
  uint16_t pSampleRate = analogRead(SAMPLE_RATE_PIN);
  uint16_t pVolume = analogRead(VOLUME_PIN);

  pot1_norm = p1 * (1.0f / 4095.0f);  // XIAO SAMD21 is 12-bit (0-4095)
  pot2_norm = p2 * (1.0f / 4095.0f);
  
  // Map bitcrush pot to 1-10 bits (12-bit ADC: 0-4095)
  // CCW = 1 bit (ULTIMATE chaos - pure square wave), CW = 10 bits (clean)
  float bitcrush_norm = pBitcrush * (1.0f / 4095.0f);
  globalBitcrush = 1 + (uint8_t)(bitcrush_norm * 9.0f);  // 1 to 10
  
  // Map sample rate pot to 1-40x decimation (12-bit ADC: 0-4095) - INVERTED
  // CCW = 40 (400Hz - extreme lo-fi), CW = 1 (full 16kHz - clean)
  // Add dead zone: top 10% of pot travel = guaranteed no reduction
  float samplerate_norm = pSampleRate * (1.0f / 4095.0f);
  if (samplerate_norm > 0.9f) {
    sampleRateDecimation = 1;  // Guaranteed no reduction in top 10%
  } else {
    // Map 0-90% of pot to full 40-1 range
    float adjusted_norm = samplerate_norm / 0.9f;  // Rescale to 0-1
    sampleRateDecimation = 40 - (uint8_t)(adjusted_norm * 39.0f);  // 40 to 1
  }
  
  // Map volume pot with quadratic curve (efficient logarithmic approximation)
  // 12-bit ADC (0-4095), scale to 8-bit (0-255), then square for log curve
  uint16_t volTemp = pVolume >> 4;  // Scale to 0-255
  masterVolume = (volTemp * volTemp) >> 8;  // x² curve, scale back to 0-255

  // Update all algorithm parameters
  updateAllParams();
  
  // ========================================
  // BUTTON HANDLING - Simple and Robust
  // ========================================
  
  // --- BANK BUTTON ---
  // Short press : cycles through banks
  // Long press (DISPLAY_HOLD_MS): toggles display timeout mode on/off
  bool bankButtonNow = digitalRead(BUTTON_BANK);
  static bool bankLongPressFired = false;  // Prevent long-press firing more than once per hold

  if (bankButtonNow != lastBankButtonState) {
    // Button state changed - start debounce timer
    lastBankButtonTime = millis();
    lastBankButtonState = bankButtonNow;

    if (bankButtonNow == HIGH) {
      // Button just released
      if (!bankLongPressFired) {
        // Short press: released before long-press threshold → switch bank
        currentBank = (Bank)((currentBank + 1) % BANK_COUNT);

        // Reset to first algorithm in new bank
        currentAlgo = 0;

        // If switching to wavetables bank, generate first waveform
        if (currentBank == BANK_WAVETABLES) {
          generateWaveform1();
        }

        // Update display: show new bank/algo
        updateDisplay();
      }
      bankLongPressFired = false;  // Reset for next press
    }
  }

  // Long-press detection (fires once while button is held past threshold)
  if (bankButtonNow == LOW &&
      !bankLongPressFired &&
      (millis() - lastBankButtonTime) > DISPLAY_HOLD_MS) {

    bankLongPressFired = true;  // Only fire once per hold

    // Toggle display timeout mode
    displayTimeoutEnabled = !displayTimeoutEnabled;

   // Flash confirmation on display: "on" = always on, "oFF" = will turn off
    display.setBrightness(0x02);
    if (displayTimeoutEnabled) {
      // Timeout just turned ON → display will turn off → show "oFF"
      uint8_t segO = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;
      uint8_t segF = SEG_A | SEG_E | SEG_F | SEG_G;
      uint8_t msg[] = {segO, segF, segF, 0};
      display.setSegments(msg);
    } else {
      // Timeout just turned OFF → display stays on → show " on"
      uint8_t segO = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;
      uint8_t segN = SEG_A | SEG_B | SEG_C | SEG_E | SEG_F;
      uint8_t msg[] = {0, 0, segO, segN};
      display.setSegments(msg);
    }
    displayOn                  = true;
    displayOnTime              = millis();
    displayShowingConfirmation = true;  // Return to algo display after brief confirmation
  }
  
  // --- ALGORITHM BUTTON (D9) ---
  // Cycles through algorithms within current bank
  bool algoButtonNow = digitalRead(BUTTON_ALGO);
  
  if (algoButtonNow != lastAlgoButtonState) {
    // Button state changed - start debounce timer
    lastAlgoButtonTime = millis();
    lastAlgoButtonState = algoButtonNow;
  }
  
  if ((millis() - lastAlgoButtonTime) > buttonDebounceMs) {
    // Stable state for long enough
    if (algoButtonNow == LOW) {  // Button pressed (pulled to ground)
      // Move to next algorithm in current bank
      currentAlgo++;
      
      // Wrap around within bank (stay in SAME bank!)
      if (currentAlgo >= algoCountPerBank[currentBank]) {
        currentAlgo = 0;  // Back to first algo in this bank
      }
      
      // If in wavetables bank, regenerate appropriate waveform
      if (currentBank == BANK_WAVETABLES) {
        if (currentAlgo == WT_GENERATIVE_1) generateWaveform1();
        else if (currentAlgo == WT_GENERATIVE_2) { gw2BlipFreqSet = false; generateWaveform2(); }
        //else if (currentAlgo == WT_GENERATIVE_3) generateWaveform3();
        else if (currentAlgo == WT_GENERATIVE_4) generateWaveform4();
        else if (currentAlgo == WT_GENERATIVE_5) generateWaveform5();
        else if (currentAlgo == WT_GENERATIVE_6) generateWaveform6();
        else if (currentAlgo == WT_GENERATIVE_7) generateWaveform7();
        else if (currentAlgo == WT_GENERATIVE_17) generateWaveform17();
        else if (currentAlgo == WT_GENERATIVE_18) generateWaveform18();
      }
      // GW3 uses buffer-count regen instead of time-based
      if (currentBank == BANK_WAVETABLES && 
          currentAlgo == WT_GENERATIVE_3 && 
          gw3NeedsRegen) {
        gw3NeedsRegen = false;
        generateWaveform3();
      }
      
      // If in BitBend bank, regenerate appropriate waveform
      if (currentBank == BANK_BITBEND) {
        if (currentAlgo == BB_CHAOS) generateWaveform8();
        else if (currentAlgo == BB_SPARSE) generateWaveform9();
        else if (currentAlgo == BB_DUAL) generateWaveform10();
        else if (currentAlgo == BB_FREEZE) generateWaveform11();
        else if (currentAlgo == BB_PING) generateWaveform12();
        else if (currentAlgo == BB_MIRROR) generateWaveform13();
        else if (currentAlgo == BB_TRIPLE) generateWaveform14();
        else if (currentAlgo == BB_SWEEP) generateWaveform15();
        else if (currentAlgo == BB_TRIPLE_B) generateWaveform16();
      }
      
      // Update display: show new bank/algo
      updateDisplay();
      
      // Wait for button release to prevent multiple triggers
      while (digitalRead(BUTTON_ALGO) == LOW) {
        delay(10);
      }
      lastAlgoButtonTime = millis();
      lastAlgoButtonState = HIGH;
    }
  }
  
  // --------------------------------------------------
  // Previous Algorithm Button (D7)
  // --------------------------------------------------
  static bool lastPrevButtonState = HIGH;
  static unsigned long lastPrevButtonTime = 0;
  
  bool prevButtonNow = digitalRead(BUTTON_PREV);
  
  if (prevButtonNow != lastPrevButtonState) {
    // Button state changed - start debounce timer
    lastPrevButtonTime = millis();
    lastPrevButtonState = prevButtonNow;
  }
  
  if ((millis() - lastPrevButtonTime) > buttonDebounceMs) {
    // Stable state for long enough
    if (prevButtonNow == LOW) {  // Button pressed
      // Move to previous algorithm in current bank
      if (currentAlgo == 0) {
        // At first algo, wrap to last
        currentAlgo = algoCountPerBank[currentBank] - 1;
      } else {
        currentAlgo--;
      }
      
      // If in wavetables bank, regenerate appropriate waveform
      if (currentBank == BANK_WAVETABLES) {
        if (currentAlgo == WT_GENERATIVE_1) generateWaveform1();
        else if (currentAlgo == WT_GENERATIVE_2) { gw2BlipFreqSet = false; generateWaveform2(); }
        //else if (currentAlgo == WT_GENERATIVE_3) generateWaveform3();
        // GW3 uses buffer-count regen instead of time-based — let gw3NeedsRegen handle it
        else if (currentAlgo == WT_GENERATIVE_4) generateWaveform4();
        else if (currentAlgo == WT_GENERATIVE_5) generateWaveform5();
        else if (currentAlgo == WT_GENERATIVE_6) generateWaveform6();
        else if (currentAlgo == WT_GENERATIVE_7) generateWaveform7();
        else if (currentAlgo == WT_GENERATIVE_17) generateWaveform17();
        else if (currentAlgo == WT_GENERATIVE_18) generateWaveform18();
      }
      
      // If in BitBend bank, regenerate appropriate waveform
      if (currentBank == BANK_BITBEND) {
        if (currentAlgo == BB_CHAOS) generateWaveform8();
        else if (currentAlgo == BB_SPARSE) generateWaveform9();
        else if (currentAlgo == BB_DUAL) generateWaveform10();
        else if (currentAlgo == BB_FREEZE) generateWaveform11();
        else if (currentAlgo == BB_PING) generateWaveform12();
        else if (currentAlgo == BB_MIRROR) generateWaveform13();
        else if (currentAlgo == BB_TRIPLE) generateWaveform14();
        else if (currentAlgo == BB_SWEEP) generateWaveform15();
        else if (currentAlgo == BB_TRIPLE_B) generateWaveform16();
      }
      
      // Update display: show new bank/algo
      updateDisplay();
      
      // Wait for button release
      while (digitalRead(BUTTON_PREV) == LOW) {
        delay(10);
      }
      lastPrevButtonTime = millis();
      lastPrevButtonState = HIGH;
    }
  }
  
  // --------------------------------------------------
  // Non-blocking display timeout
  // --------------------------------------------------
  if (displayOn && (millis() - displayOnTime > DISPLAY_TIMEOUT_MS)) {
    if (displayShowingConfirmation) {
      // Confirmation shown long enough — return to current bank/algo
      displayShowingConfirmation = false;
      updateDisplay();  // Stays lit if timeout is off
    } else if (displayTimeoutEnabled) {
      // Normal timeout — blank the display
      displayOn = false;
      display.setBrightness(0x00);
      uint8_t blank[] = {0, 0, 0, 0};
      display.setSegments(blank);
    }
  }

  // Regenerate waveforms periodically (only for wavetables bank)
  static unsigned long lastRegenTime = 0;
  unsigned long regenInterval = 2000;  // Default 2 seconds
  
  /*
  // Adjust interval based on algorithm
  if (currentBank == BANK_WAVETABLES) {
    if (currentAlgo == WT_GENERATIVE_2) {
      regenInterval = 5000;  // Dense Microglitch: every 5 seconds
    //} else if (currentAlgo == WT_GENERATIVE_3) {
      //regenInterval = 3000;  // Spacey Pulses: every 3 seconds
    } else if (currentAlgo == WT_GENERATIVE_4) {
      regenInterval = 0xFFFFFFFF;  // Random Jump: never auto-regen (only on algo change)
    }
  }
    */


  // Adjust interval based on algorithm
  if (currentBank == BANK_WAVETABLES) {
    if      (currentAlgo == WT_GENERATIVE_2)  regenInterval = 5000;    // Dense Microglitch
    // GW3: play-count based — no interval needed
    else if (currentAlgo == WT_GENERATIVE_4)  regenInterval = 0xFFFFFFFF; // Random Jump: manual only
    else if (currentAlgo == WT_GENERATIVE_5)  regenInterval = 2000;    // Wandering Window
    else if (currentAlgo == WT_GENERATIVE_6)  regenInterval = 4000;    // Manual Window
    else if (currentAlgo == WT_GENERATIVE_7)  regenInterval = 3000;    // Noise or Saw Window
    else if (currentAlgo == WT_GENERATIVE_17) regenInterval = 2000;    // Buffer approach
    else if (currentAlgo == WT_GENERATIVE_18) regenInterval = 2000;    // BitBend Quad
    // GW1: default 2000 covers this
  }


  if (millis() - lastRegenTime > regenInterval) {
    lastRegenTime = millis();  // ALWAYS update timer!
    
    if (currentBank == BANK_WAVETABLES) {
      if (currentAlgo == WT_GENERATIVE_1) {
        generateWaveform1();
      } else if (currentAlgo == WT_GENERATIVE_2) {
        generateWaveform2();
      } else if (currentAlgo == WT_GENERATIVE_3) {
        generateWaveform3();
      } else if (currentAlgo == WT_GENERATIVE_4) {
        generateWaveform4();
      } else if (currentAlgo == WT_GENERATIVE_5) {
        generateWaveform5();
      } else if (currentAlgo == WT_GENERATIVE_6) {
        generateWaveform6();
      }
    }
  }
}

// --------------------------------------------------
// Audio ISR
// --------------------------------------------------
void TC5_Handler()
{
  if (TC5->COUNT16.INTFLAG.bit.MC0) {
    TC5->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;

    int16_t sample = 0;

    if (gate) {
      // Select algorithm based on current bank and algorithm index
      switch (currentBank) {
        
        // BANK 1: Wavetables (Generative Waveforms)
        case BANK_WAVETABLES:
          switch (currentAlgo) {
            case WT_GENERATIVE_1:
              sample = generativeWaveform1();
              break;
            case WT_GENERATIVE_2:
              sample = generativeWaveform2();
              break;
            case WT_GENERATIVE_3:
              sample = generativeWaveform3();
              break;
            case WT_GENERATIVE_4:
              sample = generativeWaveform4();
              break;
            case WT_GENERATIVE_5:
              sample = generativeWaveform5();
              break;
            case WT_GENERATIVE_6:
              sample = generativeWaveform6();
              break;
            case WT_GENERATIVE_7:
              sample = generativeWaveform7();
              break;
            case WT_GENERATIVE_17:
              sample = generativeWaveform17();
              break;
            case WT_GENERATIVE_18:
              sample = generativeWaveform18();
              break;
          }
          break;
        
        // BANK 2: Noisy Textures
        case BANK_NOISY_TEXTURES:
          switch (currentAlgo) {
            case NT_LATCHED_NOISE:
              sample = latchedNoise1();
              break;
            case NT_DUST:
              sample = dustNoise();
              break;
            case NT_FMNOISE:
              sample = FMnoise();
              break;
            case NT_NOISEGATES:
              sample = noisegates();
              break;
            case NT_SAW_CLICKS:
              sample = sawClicks();
              break;
            case NT_NOISE_NOR_NOISE:
              sample = noiseNorNoise();
              break;
            case NT_DUST_BURST:
              sample = dustBurst();
              break;
            case NT_HIGHPASS_NOISE:
              sample = highpassNoise();
              break;
              /*
            case NT_HIGH_NOISE_RHYTHM:
              sample = highNoiseRhythm();
              break;
              */
            case NT_VINYL_CRACKLE:
            sample = vinylCrackleAlgo();
            break;
          }
          break;
        
        // BANK 3: BitBend Wavetables
        case BANK_BITBEND:
          switch (currentAlgo) {
            case BB_CHAOS:
              sample = generativeWaveform8();
              break;
            case BB_SPARSE:
              sample = generativeWaveform9();
              break;
            case BB_DUAL:
              sample = generativeWaveform10();
              break;
            case BB_FREEZE:
              sample = generativeWaveform11();
              break;
            case BB_PING:
              sample = generativeWaveform12();
              break;
            case BB_MIRROR:
              sample = generativeWaveform13();
              break;
            case BB_TRIPLE:
              sample = generativeWaveform14();
              break;
            case BB_SWEEP:
              sample = generativeWaveform15();
              break;
            case BB_TRIPLE_B:
              sample = generativeWaveform16();
              break;
          }
          break;
        
        // BANK 4: Blips and Tones
        case BANK_BLIPS:
          switch (currentAlgo) {
            case BLIP_RANDOM_TRIANGLE:
              sample = randomTriangle();
              break;
            case BLIP_HARMONIC_TRIS:
              sample = harmonicTris();
              break;
            case BLIP_FAST_TRIANGLE:
              sample = fastTriangle();
              break;
            case BLIP_PHRYGIAN_TRI:
              sample = phrygianTri();
              break;
            case BLIP_RING_MOD:
              sample = ringMod();
              break;
            case BLIP_NOISE_OR_SQUARE:
              sample = noiseOrSquare();
              break;
            case BLIP_MAJOR_TRIS:
              sample = majorTris();
              break;
            case BLIP_BERNOULLI_MINOR7:
              sample = bernoulliTris();
              break;
            case BLIP_PENTATONIC_BLIPS:
              sample = pentatonicBlips();
              break;
          }
          break;
        
        // BANK 5: Logic Operations
        case BANK_LOGIC:
          switch (currentAlgo) {
            case LOGIC_THREE_CASCADE:
              sample = threeCascadedSquares();
              break;
            case LOGIC_NOR_SQUARE:
              sample = norSquare();
              break;
            case LOGIC_TRI_OR_SAW:
              sample = triOrSaw();
              break;
            case LOGIC_TRI_NOR_TRI:
              sample = triNorTri();
              break;
            case LOGIC_TRI_XOR_TRI:
              sample = triXorTri();
              break;
            case LOGIC_SQUARE_XNOR:
              sample = squareXnorSquare();
              break;
            case LOGIC_SQUARE_NAND:
              sample = squareNandSquare();
              break;
            case LOGIC_TWO_SAWS:
              sample = twoSaws();
              break;
            case LOGIC_SQUARE_OR_SQUARE:
              sample = squareOrSquare();
              break;
          }
          break;
      }
    } else {
      sample = 0; // gate off
    }

    // SAMPLE RATE REDUCTION (controlled by A4 pot)
    // Super cheap: just skip samples and hold
    decimationCounter++;
    if (decimationCounter >= sampleRateDecimation) {
      decimationCounter = 0;
      heldSample = sample;  // Update held sample
    }
    // else: use previous held sample (creates stepped/lo-fi effect)
    
    // GLOBAL BITCRUSH (controlled by A3 pot)
    int16_t crushMask = ~((1 << (10 - globalBitcrush)) - 1);
    int16_t crushed = heldSample & crushMask;

    // DITHER OPTIONS - uncomment your preferred version
    
    // Option 1: +0 or +1 dither (clean, minimal artifacts)
    //int16_t dithered = crushed + (rand12() & 1);  // +0 or +1 dither
    
    // Option 2: Symmetric ±1 dither (current favorite - good balance)
    int16_t dithered = crushed + ((rand12() & 1) - (rand12() & 1));
    
    // Option 3: No dithering (original, more whine)
    //int16_t dithered = crushed;  // No dithering
    
    // MASTER VOLUME (super efficient: one multiply + one shift)
    // Multiply signed sample by unsigned volume (0-255), then divide by 256
    int32_t volTemp = ((int32_t)dithered * masterVolume) >> 8;
    
    // Clamp to 10-bit DAC range (±512)
    if (volTemp > 511) volTemp = 511;
    if (volTemp < -512) volTemp = -512;
    int16_t volumed = (int16_t)volTemp;
    
    analogWrite(DAC_PIN, DAC_CENTER + volumed);
    //analogWrite(DAC_PIN, DAC_CENTER + dithered); // bypass volume
    //analogWrite(DAC_PIN, DAC_CENTER + sample); // original clean signal
  }
}
