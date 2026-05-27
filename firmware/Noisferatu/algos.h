#ifndef ALGOS_H
#define ALGOS_H

#include <Arduino.h>
#include "hardware.h"

// --------------------------------------------------
// PRNG State (shared across algorithms)
// --------------------------------------------------
uint32_t rng = 0x12345678;

// --------------------------------------------------
// Shared Noise Functions
// --------------------------------------------------
inline int16_t noise1()
{
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return (int16_t)(rng & 0x3FF) - 512; // signed 10-bit
}

inline uint16_t rand12()
{
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng & 0x0FFF; // 0–4095
}

// --------------------------------------------------
// Global Triangle Oscillator (reusable building block)
// --------------------------------------------------
// Simple triangle wave generator - can be called from any algorithm
// Input: phase (0.0 to 1.0), Output: -512 to +511 (10-bit signed)
inline int16_t triangle(float phase)
{
  // Convert phase to triangle: 0->1->0, then scale to -1 to +1
  float tri = (phase < 0.5f) ? (phase * 2.0f) : (2.0f - phase * 2.0f);
  tri = tri * 2.0f - 1.0f;  // -1 to +1
  
  return (int16_t)(tri * 511.0f);  // -512 to +511
}

// --------------------------------------------------
// Global Square Wave Oscillator (reusable building block)
// --------------------------------------------------
// Simple square wave generator - can be called from any algorithm
// Input: phase (0.0 to 1.0), Output: -512 or +511 (10-bit signed)
inline int16_t square(float phase)
{
  // Square: high when phase < 0.5, low when phase >= 0.5
  return (phase < 0.5f) ? 511 : -512;
}

// --------------------------------------------------
// Global Square Wave with PWM (reusable building block)
// --------------------------------------------------
// Square wave with variable pulse width
// Input: phase (0.0 to 1.0), duty (0.0 to 1.0), Output: -512 or +511
inline int16_t squarePWM(float phase, float duty)
{
  // High when phase < duty, low otherwise
  return (phase < duty) ? 511 : -512;
}

// --------------------------------------------------
// Algorithm: Latched Noise
// --------------------------------------------------
volatile uint32_t holdCounter = 0;
volatile uint32_t holdPeriodSamples = SAMPLE_RATE;
volatile int16_t latchedValue = 0;
volatile uint16_t latchProbThreshold = 4095;

inline int16_t latchedNoise1()
{
  holdCounter++;

  if (holdCounter >= holdPeriodSamples) {
    holdCounter = 0;

    uint16_t r = rand12();

    if (r < latchProbThreshold) {
      latchedValue = noise1();
    }
    // else: hold previous value
  }

  return latchedValue;
}

// --------------------------------------------------
// Algorithm: Dust
// --------------------------------------------------
volatile float dustProb = 0.0f;
volatile float dustFilterAlpha = 1.0f;     // Filter coefficient (pot1 controlled)
volatile float dustFilterState = 0.0f;     // Filter state

inline int16_t dustNoise()
{
  // uniform random 0..1
  float r = random(0, 65536) * (1.0f / 65536.0f);

  int16_t rawClick;
  if (r < dustProb) {
    rawClick = noise1();   // click
  } else {
    rawClick = 0;          // silence
  }
  
  // 1-pole lowpass filter
  // output = output + alpha * (input - output)
  dustFilterState += dustFilterAlpha * ((float)rawClick - dustFilterState);
  
  return (int16_t)dustFilterState;
}

// --------------------------------------------------
// Algorithm: FMnoise (rewritten - inharmonic dual oscillators)
// two clocks at inharmonic ratio, trigger on coincidence
// --------------------------------------------------
volatile uint32_t fmPhase1 = 0;
volatile uint32_t fmPhase2 = 0;
volatile uint32_t fmPhaseInc1 = 0;   // pot1: base frequency osc1
volatile uint32_t fmPhaseInc2 = 0;   // pot1: base frequency osc2 (inharmonic ratio)

// Coincidence clock state
volatile uint32_t fmClock1Phase = 0;
volatile uint32_t fmClock2Phase = 0;
volatile uint32_t fmClockPhaseInc = 0;   // pot2: clock speed
volatile uint32_t fmClockPhaseInc2 = 0;  // move here from params.h
volatile uint8_t fmCurrentRatioIdx = 0;  // current ratio index

inline int16_t FMnoise()
{
  // Advance audio oscillators
  fmPhase1 += fmPhaseInc1;
  fmPhase2 += fmPhaseInc2;

  // Advance coincidence clocks
  static uint32_t lastClock1State = 0;
  static uint32_t lastClock2State = 0;

  fmClock1Phase += fmClockPhaseInc;
  fmClock2Phase += fmClockPhaseInc2;  // set in params: inc * FM_CLOCK_RATIO

  uint32_t clock1State = fmClock1Phase & 0x80000000;
  uint32_t clock2State = fmClock2Phase & 0x80000000;

// Trigger when clock1 rises AND clock2 is currently high
  bool clock1Rise = clock1State && !lastClock1State;
  bool clock2High = clock2State;

  if (clock1Rise && clock2High) {
    // Pick random ratio from table
    fmCurrentRatioIdx = rand12() % 8;
  }

  lastClock1State = clock1State;
  lastClock2State = clock2State;

  // Extract triangles
  uint16_t p1 = fmPhase1 >> 22;
  int16_t tri1 = (p1 < 512) ? ((p1 << 1) - 512) : (1535 - (p1 << 1));

  uint16_t p2 = fmPhase2 >> 22;
  int16_t tri2 = (p2 < 512) ? ((p2 << 1) - 512) : (1535 - (p2 << 1));

  // XOR for inharmonic metallic chaos
  return tri1 ^ tri2;
}

// --------------------------------------------------
// Algorithm: Noisegates
// --------------------------------------------------
volatile uint32_t ng1Phase = 0;
volatile uint32_t ng2Phase = 0;
volatile uint32_t ng1PhaseInc = 0;      // pot1 controlled
volatile uint32_t ng2BasePhaseInc = 0;  // pot2 controlled
volatile int32_t ng2RandomWalk = 0;     // Integer random walk offset
volatile int32_t randomWalkStep = 0;    // Step size for random walk
volatile uint32_t walkUpdateCounter = 0;
volatile uint32_t walkUpdatePeriod = SAMPLE_RATE / 8;

inline int16_t noisegates()
{
    // --- Update random walk at regular intervals ---
    walkUpdateCounter++;
    if (walkUpdateCounter >= walkUpdatePeriod) {
        walkUpdateCounter = 0;
        
        // Random direction: +/- step
        int32_t direction = (rand12() & 1) ? 1 : -1;
        ng2RandomWalk += direction * randomWalkStep;
        
        // Clamp random walk (prevent extreme deviations)
        int32_t maxWalk = randomWalkStep * 500;  // Max deviation
        if (ng2RandomWalk < -maxWalk) ng2RandomWalk = -maxWalk;
        if (ng2RandomWalk > maxWalk) ng2RandomWalk = maxWalk;
    }
    
    // --- Triangle 1 (regular) ---
    ng1Phase += ng1PhaseInc;
    
    // Extract triangle from phase (top 10 bits)
    uint16_t phase1_10bit = ng1Phase >> 22;
    int16_t tri1;
    if (phase1_10bit < 512) {
        tri1 = (phase1_10bit << 1) - 512;
    } else {
        tri1 = 1535 - (phase1_10bit << 1);
    }
    
    // Gate 1: open when triangle > ~486 (out of 511, ~95%)
    bool gate1 = (tri1 > 486);
    
    // --- Triangle 2 (with random walk) ---
    // Apply random walk to phase increment
    int32_t ng2PhaseInc = (int32_t)ng2BasePhaseInc + ng2RandomWalk;
    if (ng2PhaseInc < 0) ng2PhaseInc = 0;  // Ensure positive
    
    ng2Phase += (uint32_t)ng2PhaseInc;
    
    // Extract triangle from phase
    uint16_t phase2_10bit = ng2Phase >> 22;
    int16_t tri2;
    if (phase2_10bit < 512) {
        tri2 = (phase2_10bit << 1) - 512;
    } else {
        tri2 = 1535 - (phase2_10bit << 1);
    }
    
    // Gate 2: open when triangle > ~486
    bool gate2 = (tri2 > 486);
    
    // --- Generate gated noise ---
    int16_t n = noise1();
    
    int16_t gated1 = gate1 ? n : 0;
    int16_t gated2 = gate2 ? n : 0;
    
    // Mix gated outputs
    return (gated1 + gated2) >> 1;  // Divide by 2
}

// --------------------------------------------------
// Algorithm: Generative Waveform 1
// --------------------------------------------------
// Algorithm: Generative Waveforms (3 variants, shared buffer)
// --------------------------------------------------
#define WAVEFORM_SIZE 4000  // Shared buffer size

// SHARED BUFFER - all 3 algorithms write to this
volatile int16_t waveformBuffer[WAVEFORM_SIZE];
volatile uint16_t playbackPhase = 0;
volatile float playbackSpeed = 1.0f;

// Silence chunk state for GW1
volatile float gw1SilenceProb = 0.0f;
volatile uint16_t gw1SilenceChunkRemaining = 0;

// ============================================
// VARIANT 1: Sparse Glitchy
// ============================================
#define GW1_CHUNK_SIZE_1 32
#define GW1_CHUNK_SIZE_2 8
#define GW1_CHUNK_SIZE_3 64
#define GW1_NOISE_PROB 1
#define GW1_BLIP_SIZE 50
#define GW1_BLIP_FREQ_MIN 220.0f
#define GW1_BLIP_FREQ_MAX 7000.0f

void generateWaveform1()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW1_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW1_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW1_CHUNK_SIZE_3; break;
      default: chunkSize = GW1_CHUNK_SIZE_1; break;
    }
    
    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }
    
    bool writeNoise = (rand12() % 100) < GW1_NOISE_PROB;
    
    if (writeNoise) {
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }
  
  // Triangle blip
  if (GW1_BLIP_SIZE > 0 && GW1_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW1_BLIP_SIZE);
    float blipFreq = GW1_BLIP_FREQ_MIN + 
                     ((float)(rand12() % 1000) / 1000.0f) * 
                     (GW1_BLIP_FREQ_MAX - GW1_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;
    
    for (uint16_t i = 0; i < GW1_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

// ============================================
// VARIANT 2: Dense Microglitch
// ============================================
#define GW2_CHUNK_SIZE_1 5
#define GW2_CHUNK_SIZE_2 2
#define GW2_CHUNK_SIZE_3 21
#define GW2_NOISE_PROB 3
#define GW2_BLIP_SIZE 77
#define GW2_BLIP_FREQ_MIN  400.0f
#define GW2_BLIP_FREQ_MAX 2503.0f

volatile float gw2BlipFreq = 2100.0f;
volatile bool gw2BlipFreqSet = false;


void generateWaveform2()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW2_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW2_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW2_CHUNK_SIZE_3; break;
      default: chunkSize = GW2_CHUNK_SIZE_1; break;
    }
    
    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }
    
    bool writeNoise = (rand12() % 100) < GW2_NOISE_PROB;
    
    if (writeNoise) {
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 1) crushedNoise = noise1();  // More grainy!
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }
  
  // Shorter, higher-pitched blip
  if (GW2_BLIP_SIZE > 0 && GW2_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW2_BLIP_SIZE);
  if (!gw2BlipFreqSet) {
        gw2BlipFreq = GW2_BLIP_FREQ_MIN + 
                      ((float)(rand12() % 1000) / 1000.0f) * 
                      (GW2_BLIP_FREQ_MAX - GW2_BLIP_FREQ_MIN);
      gw2BlipFreqSet = true;
    }
    float blipFreq = gw2BlipFreq;
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;
    
    for (uint16_t i = 0; i < GW2_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

// ============================================
// VARIANT 3: Spacey Pulses
// ============================================
#define GW3_CHUNK_SIZE_1 0
#define GW3_CHUNK_SIZE_2 33
#define GW3_CHUNK_SIZE_3 1
#define GW3_NOISE_PROB 4
#define GW3_BLIP_SIZE 0  // NO BLIP - pure pulses
#define GW3_BLIP_FREQ_MIN 100.0f
#define GW3_BLIP_FREQ_MAX 500.0f

// Separate silence chunk sizes for GW3 (since noise chunks have 0s)
#define GW3_SILENCE_CHUNK_1 64
#define GW3_SILENCE_CHUNK_2 128
#define GW3_SILENCE_CHUNK_3 256

volatile uint8_t gw3PlayCount = 0;
volatile bool gw3NeedsRegen = false;
#define GW3_REGEN_COUNT 5  // ADJUST: 4=western, 5=buchla

void generateWaveform3()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW3_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW3_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW3_CHUNK_SIZE_3; break;
      default: chunkSize = GW3_CHUNK_SIZE_1; break;
    }
    
    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }
    
    bool writeNoise = (rand12() % 100) < GW3_NOISE_PROB;
    
    if (writeNoise) {
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 2) crushedNoise = noise1();  // Less grainy - smoother pulses
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }
  
  // No blip for variant 3 (GW3_BLIP_SIZE = 0)
}

// Silence chunk state for GW2
volatile float gw2SilenceProb = 0.0f;
volatile uint16_t gw2SilenceChunkRemaining = 0;

// Silence chunk state for GW3
volatile float gw3SilenceProb = 0.0f;
volatile uint16_t gw3SilenceChunkRemaining = 0;

// ============================================
// PLAYBACK with chunked silence (separate for each variant)
// ============================================

inline int16_t generativeWaveform1()
{
  // Chunked silence injection for GW1
  if (gw1SilenceChunkRemaining == 0) {
    // Choose random chunk size (same sizes as GW1)
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW1_CHUNK_SIZE_1; break;  // 32
      case 1: chunkSize = GW1_CHUNK_SIZE_2; break;  // 8
      case 2: chunkSize = GW1_CHUNK_SIZE_3; break;  // 64
      default: chunkSize = GW1_CHUNK_SIZE_1; break;
    }
    
    float randNorm = (rand12() % 1000) / 1000.0f;
    bool isSilentChunk = (randNorm < gw1SilenceProb);
    
    if (isSilentChunk) {
      gw1SilenceChunkRemaining = chunkSize;
    }
  }
  
  if (gw1SilenceChunkRemaining > 0) {
    gw1SilenceChunkRemaining--;
    // Still advance playback phase even during silence
    static float fractionalPhase1 = 0.0f;
    fractionalPhase1 += playbackSpeed;
    uint16_t phaseIncrement = (uint16_t)fractionalPhase1;
    fractionalPhase1 -= phaseIncrement;
    playbackPhase += phaseIncrement;
    while (playbackPhase >= WAVEFORM_SIZE) playbackPhase -= WAVEFORM_SIZE;
    return 0;  // Silent
  }
  
  // Normal playback
  int16_t sample = waveformBuffer[playbackPhase];
  static float fractionalPhase1 = 0.0f;
  fractionalPhase1 += playbackSpeed;
  uint16_t phaseIncrement = (uint16_t)fractionalPhase1;
  fractionalPhase1 -= phaseIncrement;
  playbackPhase += phaseIncrement;
  while (playbackPhase >= WAVEFORM_SIZE) playbackPhase -= WAVEFORM_SIZE;
  return sample;
}

inline int16_t generativeWaveform2()
{
  // Chunked silence injection for GW2
  if (gw2SilenceChunkRemaining == 0) {
    // Choose random chunk size (same sizes as GW2)
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW2_CHUNK_SIZE_1; break;  // 8
      case 1: chunkSize = GW2_CHUNK_SIZE_2; break;  // 2
      case 2: chunkSize = GW2_CHUNK_SIZE_3; break;  // 16
      default: chunkSize = GW2_CHUNK_SIZE_1; break;
    }
    
    float randNorm = (rand12() % 1000) / 1000.0f;
    bool isSilentChunk = (randNorm < gw2SilenceProb);
    
    if (isSilentChunk) {
      gw2SilenceChunkRemaining = chunkSize;
    }
  }
  
  if (gw2SilenceChunkRemaining > 0) {
    gw2SilenceChunkRemaining--;
    static float fractionalPhase2 = 0.0f;
    fractionalPhase2 += playbackSpeed;
    uint16_t phaseIncrement = (uint16_t)fractionalPhase2;
    fractionalPhase2 -= phaseIncrement;
    playbackPhase += phaseIncrement;
    while (playbackPhase >= WAVEFORM_SIZE) playbackPhase -= WAVEFORM_SIZE;
    return 0;  // Silent
  }
  
  // Normal playback
  int16_t sample = waveformBuffer[playbackPhase];
  static float fractionalPhase2 = 0.0f;
  fractionalPhase2 += playbackSpeed;
  uint16_t phaseIncrement = (uint16_t)fractionalPhase2;
  fractionalPhase2 -= phaseIncrement;
  playbackPhase += phaseIncrement;
  while (playbackPhase >= WAVEFORM_SIZE) playbackPhase -= WAVEFORM_SIZE;
  return sample;
}

inline int16_t generativeWaveform3()
{
  // Chunked silence injection for GW3
  if (gw3SilenceChunkRemaining == 0) {
    // Choose random chunk size from SEPARATE silence sizes (not noise chunks)
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW3_SILENCE_CHUNK_1; break;  // 64
      case 1: chunkSize = GW3_SILENCE_CHUNK_2; break;  // 128
      case 2: chunkSize = GW3_SILENCE_CHUNK_3; break;  // 256
      default: chunkSize = GW3_SILENCE_CHUNK_1; break;
    }
    
    float randNorm = (rand12() % 1000) / 1000.0f;
    bool isSilentChunk = (randNorm < gw3SilenceProb);
    
    if (isSilentChunk) {
      gw3SilenceChunkRemaining = chunkSize;
    }
  }
  
  if (gw3SilenceChunkRemaining > 0) {
    gw3SilenceChunkRemaining--;
    static float fractionalPhase3 = 0.0f;
    fractionalPhase3 += playbackSpeed;
    uint16_t phaseIncrement = (uint16_t)fractionalPhase3;
    fractionalPhase3 -= phaseIncrement;
    playbackPhase += phaseIncrement;
    while (playbackPhase >= WAVEFORM_SIZE) playbackPhase -= WAVEFORM_SIZE;
    return 0;  // Silent
  }
  
  // Normal playback
  int16_t sample = waveformBuffer[playbackPhase];
  static float fractionalPhase3 = 0.0f;
  fractionalPhase3 += playbackSpeed;
  uint16_t phaseIncrement = (uint16_t)fractionalPhase3;
  fractionalPhase3 -= phaseIncrement;
playbackPhase += phaseIncrement;
if (playbackPhase >= WAVEFORM_SIZE) {
  playbackPhase -= WAVEFORM_SIZE;
  gw3PlayCount++;
  if (gw3PlayCount >= GW3_REGEN_COUNT) {
    gw3PlayCount = 0;
    gw3NeedsRegen = true;
  }
}
return sample;
}

// ============================================
// VARIANT 4: Random Jump Glitch
// ============================================
#define GW4_CHUNK_SIZE_1 16
#define GW4_CHUNK_SIZE_2 4
#define GW4_CHUNK_SIZE_3 32
#define GW4_NOISE_PROB 25
#define GW4_BLIP_SIZE 30
#define GW4_BLIP_FREQ_MIN 500.0f
#define GW4_BLIP_FREQ_MAX 3000.0f

volatile float gw4PlaybackSpeed = 1.0f;      // pot1
volatile float gw4SilenceProb = 0.0f;        // pot2 controls this (0.0 to 1.0)
volatile uint32_t gw4JumpCounter = 0;
volatile uint32_t gw4JumpPeriod = SAMPLE_RATE / 3;  // Fixed 3 Hz
volatile uint16_t gw4SilenceChunkRemaining = 0;  // Samples left in current silence chunk

void generateWaveform4()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW4_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW4_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW4_CHUNK_SIZE_3; break;
      default: chunkSize = GW4_CHUNK_SIZE_1; break;
    }
    
    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }
    
    bool writeNoise = (rand12() % 100) < GW4_NOISE_PROB;
    
    if (writeNoise) {
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }
  
  // Triangle blip
  if (GW4_BLIP_SIZE > 0 && GW4_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW4_BLIP_SIZE);
    float blipFreq = GW4_BLIP_FREQ_MIN + 
                     ((float)(rand12() % 1000) / 1000.0f) * 
                     (GW4_BLIP_FREQ_MAX - GW4_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;
    
    for (uint16_t i = 0; i < GW4_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform4()
{
  // Random jumping through buffer
  gw4JumpCounter++;
  if (gw4JumpCounter >= gw4JumpPeriod) {
    gw4JumpCounter = 0;
    // Jump to random position in full buffer
    playbackPhase = rand12() % WAVEFORM_SIZE;
  }
  
  // Chunked silence injection (pot2 controls probability)
  // When a chunk ends, decide if next chunk is silent or not
  if (gw4SilenceChunkRemaining == 0) {
    // Choose random chunk size (same sizes as waveform generation)
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW4_CHUNK_SIZE_1; break;  // 16
      case 1: chunkSize = GW4_CHUNK_SIZE_2; break;  // 4
      case 2: chunkSize = GW4_CHUNK_SIZE_3; break;  // 32
      default: chunkSize = GW4_CHUNK_SIZE_1; break;
    }
    
    // Decide if this chunk should be silent based on pot2
    float randNorm = (rand12() % 1000) / 1000.0f;
    bool isSilentChunk = (randNorm < gw4SilenceProb);
    
    if (isSilentChunk) {
      gw4SilenceChunkRemaining = chunkSize;  // Start silence chunk
    } else {
      gw4SilenceChunkRemaining = 0;  // Continue playing normally
    }
  }
  
  // If we're in a silence chunk, output silence and count down
  if (gw4SilenceChunkRemaining > 0) {
    gw4SilenceChunkRemaining--;
    return 0;  // Silent sample
  }
  
  // Otherwise, read and play buffer normally
  int16_t sample = waveformBuffer[playbackPhase];
  
  static float fractionalPhase = 0.0f;
  fractionalPhase += gw4PlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  
  while (playbackPhase >= WAVEFORM_SIZE) {
    playbackPhase -= WAVEFORM_SIZE;
  }
  
  return sample;
}

// ============================================
// VARIANT 5: Wandering Window
// ============================================
#define GW5_CHUNK_SIZE_1 0
#define GW5_CHUNK_SIZE_2 6
#define GW5_CHUNK_SIZE_3 48
#define GW5_NOISE_PROB 22
#define GW5_BLIP_SIZE 12
#define GW5_BLIP_FREQ_MIN 600.0f
#define GW5_BLIP_FREQ_MAX 2500.0f
#define GW5_WINDOW_SIZE (WAVEFORM_SIZE / 50)  // Window is buffer/50

volatile float gw5PlaybackSpeed = 1.0f;      // pot1
volatile uint16_t gw5WindowStart = 0;        // Wandering window position
volatile uint32_t gw5WalkCounter = 0;
volatile uint32_t gw5WalkPeriod = SAMPLE_RATE / 5;  // pot2 controls this

void generateWaveform5()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW5_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW5_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW5_CHUNK_SIZE_3; break;
      default: chunkSize = GW5_CHUNK_SIZE_1; break;
    }
    
    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }
    
    bool writeNoise = (rand12() % 100) < GW5_NOISE_PROB;
    
    if (writeNoise) {
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }
  
  // Triangle blip
  if (GW5_BLIP_SIZE > 0 && GW5_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW5_BLIP_SIZE);
    float blipFreq = GW5_BLIP_FREQ_MIN + 
                     ((float)(rand12() % 1000) / 1000.0f) * 
                     (GW5_BLIP_FREQ_MAX - GW5_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;
    
    for (uint16_t i = 0; i < GW5_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform5()
{
  // Random walk of window position (pot2 controls walk rate)
  gw5WalkCounter++;
  if (gw5WalkCounter >= gw5WalkPeriod) {
    gw5WalkCounter = 0;
    // Random walk: move window start ±1 to ±20 samples
    int16_t step = (rand12() & 1) ? (1 + rand12() % 20) : -(1 + rand12() % 20);
    int32_t newStart = (int32_t)gw5WindowStart + step;
    
    // Wrap around buffer
    if (newStart < 0) newStart += WAVEFORM_SIZE;
    if (newStart >= WAVEFORM_SIZE) newStart -= WAVEFORM_SIZE;
    
    gw5WindowStart = (uint16_t)newStart;
  }
  
  // Play within wandering window, loop at window boundaries
  uint16_t windowEnd = gw5WindowStart + GW5_WINDOW_SIZE;
  if (windowEnd > WAVEFORM_SIZE) windowEnd = WAVEFORM_SIZE;
  
  // Keep playback within current window
  if (playbackPhase < gw5WindowStart || playbackPhase >= windowEnd) {
    playbackPhase = gw5WindowStart;
  }
  
  int16_t sample = waveformBuffer[playbackPhase];
  
  static float fractionalPhase = 0.0f;
  fractionalPhase += gw5PlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  
  // Loop at window boundary
  if (playbackPhase >= windowEnd) {
    playbackPhase = gw5WindowStart;
  }
  
  return sample;
}

// ============================================
// VARIANT 6: Manual Window with Spray
// ============================================
#define GW6_CHUNK_SIZE_1 20
#define GW6_CHUNK_SIZE_2 5
#define GW6_CHUNK_SIZE_3 40
#define GW6_NOISE_PROB 35
#define GW6_BLIP_SIZE 15
#define GW6_BLIP_FREQ_MIN 700.0f
#define GW6_BLIP_FREQ_MAX 3500.0f
#define GW6_SPRAY_AMOUNT 30  // ±30 samples random offset

volatile uint16_t gw6WindowStart = 0;        // pot1 controls this
volatile uint16_t gw6WindowSize = WAVEFORM_SIZE;  // pot2 controls this
volatile uint32_t gw6SprayCounter = 0;
volatile int16_t gw6SprayOffset = 0;

void generateWaveform6()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW6_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW6_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW6_CHUNK_SIZE_3; break;
      default: chunkSize = GW6_CHUNK_SIZE_1; break;
    }
    
    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }
    
    bool writeNoise = (rand12() % 100) < GW6_NOISE_PROB;
    
    if (writeNoise) {
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }
  
  // Triangle blip
  if (GW6_BLIP_SIZE > 0 && GW6_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW6_BLIP_SIZE);
    float blipFreq = GW6_BLIP_FREQ_MIN + 
                     ((float)(rand12() % 1000) / 1000.0f) * 
                     (GW6_BLIP_FREQ_MAX - GW6_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;
    
    for (uint16_t i = 0; i < GW6_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform6()
{
  // Spray modulation - update based on window size
  // Smaller window = faster spray updates
  uint32_t sprayPeriod = gw6WindowSize;  // Spray updates relative to window size
  if (sprayPeriod < 100) sprayPeriod = 100;  // Minimum update rate
  
  gw6SprayCounter++;
  if (gw6SprayCounter >= sprayPeriod) {
    gw6SprayCounter = 0;
    // Random spray offset: ±GW6_SPRAY_AMOUNT samples
    gw6SprayOffset = (rand12() % (GW6_SPRAY_AMOUNT * 2 + 1)) - GW6_SPRAY_AMOUNT;
  }
  
  // Calculate actual window start with spray
  int32_t actualStart = (int32_t)gw6WindowStart + gw6SprayOffset;
  
  // Keep within buffer bounds
  while (actualStart < 0) actualStart += WAVEFORM_SIZE;
  while (actualStart >= WAVEFORM_SIZE) actualStart -= WAVEFORM_SIZE;
  
  uint16_t windowEnd = actualStart + gw6WindowSize;
  if (windowEnd > WAVEFORM_SIZE) windowEnd = WAVEFORM_SIZE;
  
  // Keep playback within current window
  if (playbackPhase < actualStart || playbackPhase >= windowEnd) {
    playbackPhase = actualStart;
  }
  
  int16_t sample = waveformBuffer[playbackPhase];
  
  // Fixed slow playback speed (0.5x)
  static float fractionalPhase = 0.0f;
  fractionalPhase += 0.5f;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  
  // Loop at window boundary
  if (playbackPhase >= windowEnd) {
    playbackPhase = actualStart;
  }
  
  return sample;
}

// ==================================================
// VARIANT 7: Noise or Saw Window (based on GW5)
// ==================================================
// ADJUST HERE: Chunk sizes (0 = silence, larger = more content)
#define GW7_CHUNK_SIZE_1 0      // Silence chunks
#define GW7_CHUNK_SIZE_2 6      // Short chunks
#define GW7_CHUNK_SIZE_3 48     // Long chunks

// ADJUST HERE: Probability of writing noise/saw vs silence (0-100)
#define GW7_NOISE_PROB 22       // 22% chance of noise/saw, 78% silence

// ADJUST HERE: Saw frequency range when chosen
#define GW7_SAW_FREQ_MIN 30.0f   // ADJUST: Min saw frequency (e.g., 30 Hz)
#define GW7_SAW_FREQ_MAX 1000.0f // ADJUST: Max saw frequency (e.g., 1000 Hz or 32 Hz for narrow range)

// ADJUST HERE: Probability of saw vs noise when writing audio (0-100)
#define GW7_SAW_PROB 50         // 50% = equal chance, 100% = always saw, 0% = always noise

#define GW7_BLIP_SIZE 12
#define GW7_BLIP_FREQ_MIN 600.0f
#define GW7_BLIP_FREQ_MAX 2500.0f
#define GW7_WINDOW_SIZE (WAVEFORM_SIZE / 50)

volatile uint16_t gw7WindowStart = 0;
volatile uint32_t gw7WalkCounter = 0;
volatile uint32_t gw7WalkPeriod = SAMPLE_RATE / 5;  // pot2 controls this
volatile float gw7PlaybackSpeed = 1.0f;  // pot1 controls this

void generateWaveform7()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW7_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW7_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW7_CHUNK_SIZE_3; break;
      default: chunkSize = GW7_CHUNK_SIZE_1; break;
    }
    
    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }
    
    bool writeAudio = (rand12() % 100) < GW7_NOISE_PROB;
    
    if (writeAudio) {
      // Decide: noise or saw?
      bool useSaw = (rand12() % 100) < GW7_SAW_PROB;
      
      if (useSaw) {
        // Write SAW with random frequency
        float sawFreq = GW7_SAW_FREQ_MIN + 
                        ((float)(rand12() % 1000) / 1000.0f) * 
                        (GW7_SAW_FREQ_MAX - GW7_SAW_FREQ_MIN);
        float sawPhase = 0.0f;
        float sawPhaseInc = sawFreq / SAMPLE_RATE;
        
        for (uint16_t i = 0; i < chunkSize; i++) {
          int16_t sawSample = (int16_t)((sawPhase * 2.0f - 1.0f) * 511.0f);
          waveformBuffer[writePos + i] = sawSample;
          sawPhase += sawPhaseInc;
          if (sawPhase >= 1.0f) sawPhase -= 1.0f;
        }
      } else {
        // Write latched NOISE
        int16_t crushedNoise = noise1();
        for (uint16_t i = 0; i < chunkSize; i++) {
          if ((rand12() % 10) < 3) crushedNoise = noise1();
          waveformBuffer[writePos + i] = crushedNoise;
        }
      }
    } else {
      // Write silence
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }
  
  // Triangle blip
  if (GW7_BLIP_SIZE > 0 && GW7_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW7_BLIP_SIZE);
    float blipFreq = GW7_BLIP_FREQ_MIN + 
                     ((float)(rand12() % 1000) / 1000.0f) * 
                     (GW7_BLIP_FREQ_MAX - GW7_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;
    
    for (uint16_t i = 0; i < GW7_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform7()
{
  // Random walk of window position (pot2 controls walk rate)
  gw7WalkCounter++;
  if (gw7WalkCounter >= gw7WalkPeriod) {
    gw7WalkCounter = 0;
    // Random walk: move window start ±1 to ±20 samples
    int16_t step = (rand12() & 1) ? (1 + rand12() % 20) : -(1 + rand12() % 20);
    int32_t newStart = (int32_t)gw7WindowStart + step;
    
    // Wrap around buffer
    if (newStart < 0) newStart += WAVEFORM_SIZE;
    if (newStart >= WAVEFORM_SIZE) newStart -= WAVEFORM_SIZE;
    
    gw7WindowStart = (uint16_t)newStart;
  }
  
  // Play within wandering window, loop at window boundaries
  uint16_t windowEnd = gw7WindowStart + GW7_WINDOW_SIZE;
  if (windowEnd > WAVEFORM_SIZE) windowEnd = WAVEFORM_SIZE;
  
  // Keep playback within current window
  if (playbackPhase < gw7WindowStart || playbackPhase >= windowEnd) {
    playbackPhase = gw7WindowStart;
  }
  
  int16_t sample = waveformBuffer[playbackPhase];
  
  static float fractionalPhase = 0.0f;
  fractionalPhase += gw7PlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  
  // Loop at window boundary
  if (playbackPhase >= windowEnd) {
    playbackPhase = gw7WindowStart;
  }
  
  return sample;
}

// ==================================================
// VARIANT 17: Harmonic Drone Builder
// ==================================================
// Multiple triangle harmonics (root, 2×, 3×, 4×) + latched noise
// Creates bell-like drones and harmonic textures
// Two triangles mixed per harmonic chunk for instant harmony

#define GW17_CHUNK_SIZE_NOISE 2        // Tiny noise crackles
#define GW17_CHUNK_SIZE_HARMONIC 324   // Long harmonic drones

#define GW17_SILENCE_CHUNK_1 31        // Short gaps
#define GW17_SILENCE_CHUNK_2 127       // Medium gaps  
#define GW17_SILENCE_CHUNK_3 251       // Long gaps

#define GW17_NOISE_PROB 30      // 30% noise
#define GW17_HARMONIC_PROB 50   // 50% harmonics (20% silence)

#define GW17_ROOT_FREQ_MIN 80.0f   // Low drones
#define GW17_ROOT_FREQ_MAX 240.0f  // Higher tonality

#define GW17_REGEN_INTERVAL (SAMPLE_RATE * 3)  // Regenerate every 3 seconds
#define GW17_PLAYBACK_SPEED 0.3f   // Fixed sweet spot!
#define GW17_STUTTER_MAX_PROB 0.005f  // CCW = 0.5% stutter (subtle!)

// GW17 state
volatile uint32_t gw17RegenCounter = 0;
volatile float gw17RootFreq = 150.0f;  // Stored root frequency (base, never changes)
volatile float gw17OctaveMult = 1.0f;  // Octave multiplier from pot1 (applied to new chunks only)
volatile float gw17StutterProb = 0.0f; // Stutter probability from pot2
volatile uint16_t gw17StutterChunkRemaining = 0; // Samples to hold frozen

// Full buffer generation (called on algo selection)
void generateWaveform17()
{
  // Pick NEW root frequency for this algo session
  gw17RootFreq = GW17_ROOT_FREQ_MIN + 
                 ((float)(rand12() % 1000) / 1000.0f) * 
                 (GW17_ROOT_FREQ_MAX - GW17_ROOT_FREQ_MIN);
  
  // Initial generation uses BASE root freq (no octave multiplier yet!)
  // Harmonic series: root, 2×, 3×, 4×
  float harmonics[4] = {gw17RootFreq, gw17RootFreq * 2.0f, gw17RootFreq * 3.0f, gw17RootFreq * 4.0f};
  
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    uint8_t roll = rand12() % 100;
    
    if (roll < GW17_NOISE_PROB) {
      // ===== NOISE CHUNK =====
      uint16_t chunkSize = GW17_CHUNK_SIZE_NOISE;
      if (writePos + chunkSize > WAVEFORM_SIZE) {
        chunkSize = WAVEFORM_SIZE - writePos;
      }
      
      // Latched noise
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
      writePos += chunkSize;
      
    } else if (roll < (GW17_NOISE_PROB + GW17_HARMONIC_PROB)) {
      // ===== HARMONIC CHUNK =====
      uint16_t chunkSize = GW17_CHUNK_SIZE_HARMONIC;
      if (writePos + chunkSize > WAVEFORM_SIZE) {
        chunkSize = WAVEFORM_SIZE - writePos;
      }
      
      // Pick 2 different harmonics randomly
      uint8_t harm1 = rand12() % 4;
      uint8_t harm2 = rand12() % 4;
      while (harm2 == harm1) {
        harm2 = rand12() % 4;  // Ensure different
      }
      
      float freq1 = harmonics[harm1];
      float freq2 = harmonics[harm2];
      
      float phase1 = 0.0f;
      float phase2 = 0.0f;
      float phaseInc1 = freq1 / SAMPLE_RATE;
      float phaseInc2 = freq2 / SAMPLE_RATE;
      
      // Write two triangles mixed 50/50
      for (uint16_t i = 0; i < chunkSize; i++) {
        // Triangle 1
        float tri1 = (phase1 < 0.5f) ? (phase1 * 2.0f) : (2.0f - phase1 * 2.0f);
        int16_t sample1 = (int16_t)((tri1 * 2.0f - 1.0f) * 255.0f);  // Half amplitude
        
        // Triangle 2
        float tri2 = (phase2 < 0.5f) ? (phase2 * 2.0f) : (2.0f - phase2 * 2.0f);
        int16_t sample2 = (int16_t)((tri2 * 2.0f - 1.0f) * 255.0f);  // Half amplitude
        
        // Mix 50/50
        waveformBuffer[writePos + i] = sample1 + sample2;
        
        phase1 += phaseInc1;
        if (phase1 >= 1.0f) phase1 -= 1.0f;
        
        phase2 += phaseInc2;
        if (phase2 >= 1.0f) phase2 -= 1.0f;
      }
      writePos += chunkSize;
      
    } else {
      // ===== SILENCE CHUNK =====
      uint16_t silenceSize;
      uint8_t choice = rand12() % 3;
      switch(choice) {
        case 0: silenceSize = GW17_SILENCE_CHUNK_1; break;
        case 1: silenceSize = GW17_SILENCE_CHUNK_2; break;
        case 2: silenceSize = GW17_SILENCE_CHUNK_3; break;
        default: silenceSize = GW17_SILENCE_CHUNK_1; break;
      }
      
      if (writePos + silenceSize > WAVEFORM_SIZE) {
        silenceSize = WAVEFORM_SIZE - writePos;
      }
      
      for (uint16_t i = 0; i < silenceSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
      writePos += silenceSize;
    }
  }
}

// Partial buffer regeneration (called every 3s for gradual evolution)
// Regenerates ~30% of buffer with new harmonic pairs, keeps root freq same
// NEW: Applies pot1 octave multiplier to new chunks!
void partialRegenWaveform17()
{
  // Use stored root frequency with current octave multiplier from pot1!
  // This way new chunks slide in at different octave while old chunks stay
  float harmonics[4] = {
    gw17RootFreq * gw17OctaveMult, 
    gw17RootFreq * gw17OctaveMult * 2.0f, 
    gw17RootFreq * gw17OctaveMult * 3.0f, 
    gw17RootFreq * gw17OctaveMult * 4.0f
  };
  
  // Regenerate 5 random chunks scattered through buffer (~30% coverage)
  for (uint8_t chunk = 0; chunk < 5; chunk++) {
    // Pick random start position
    uint16_t startPos = rand12() % (WAVEFORM_SIZE - GW17_CHUNK_SIZE_HARMONIC);
    
    // Decide: noise or harmonic (skip silence to avoid killing existing content)
    uint8_t roll = rand12() % 100;
    
    if (roll < 40) {
      // ===== NOISE CHUNK =====
      uint16_t chunkSize = GW17_CHUNK_SIZE_NOISE;
      
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[startPos + i] = crushedNoise;
      }
      
    } else {
      // ===== HARMONIC CHUNK with NEW random harmonic pair =====
      uint16_t chunkSize = GW17_CHUNK_SIZE_HARMONIC;
      
      // Pick 2 different harmonics
      uint8_t harm1 = rand12() % 4;
      uint8_t harm2 = rand12() % 4;
      while (harm2 == harm1) {
        harm2 = rand12() % 4;
      }
      
      float freq1 = harmonics[harm1];
      float freq2 = harmonics[harm2];
      
      float phase1 = 0.0f;
      float phase2 = 0.0f;
      float phaseInc1 = freq1 / SAMPLE_RATE;
      float phaseInc2 = freq2 / SAMPLE_RATE;
      
      // Write two triangles mixed 50/50
      for (uint16_t i = 0; i < chunkSize; i++) {
        float tri1 = (phase1 < 0.5f) ? (phase1 * 2.0f) : (2.0f - phase1 * 2.0f);
        int16_t sample1 = (int16_t)((tri1 * 2.0f - 1.0f) * 255.0f);
        
        float tri2 = (phase2 < 0.5f) ? (phase2 * 2.0f) : (2.0f - phase2 * 2.0f);
        int16_t sample2 = (int16_t)((tri2 * 2.0f - 1.0f) * 255.0f);
        
        waveformBuffer[startPos + i] = sample1 + sample2;
        
        phase1 += phaseInc1;
        if (phase1 >= 1.0f) phase1 -= 1.0f;
        
        phase2 += phaseInc2;
        if (phase2 >= 1.0f) phase2 -= 1.0f;
      }
    }
  }
}

inline int16_t generativeWaveform17()
{
  // Auto-regenerate parts of buffer every 3 seconds
  gw17RegenCounter++;
  if (gw17RegenCounter >= GW17_REGEN_INTERVAL) {
    partialRegenWaveform17();  // Smooth morphing!
    gw17RegenCounter = 0;
  }
  
  // Stutter/pause logic - FREEZE playback for chunk durations!
  if (gw17StutterChunkRemaining == 0) {
    // Pick chunk duration (MUCH longer than before for less aggressive stuttering!)
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = 666; break;   // ~42ms (the devil's stutter!)
      case 1: chunkSize = 1000; break;  // ~62ms
      case 2: chunkSize = 1533; break;  // ~96ms
      default: chunkSize = 666; break;
    }
    
    // Decide if this chunk should stutter/freeze based on pot2
    float randNorm = (rand12() % 1000) / 1000.0f;
    bool shouldStutter = (randNorm < gw17StutterProb);
    
    if (shouldStutter) {
      gw17StutterChunkRemaining = chunkSize;  // Start stutter chunk
    } else {
      gw17StutterChunkRemaining = 0;  // Play normally
    }
  }
  
  // If in stutter chunk, FREEZE playback (don't advance phase!)
  if (gw17StutterChunkRemaining > 0) {
    gw17StutterChunkRemaining--;
    return waveformBuffer[playbackPhase];  // Return same sample = STUTTER!
  }
  
  // Normal playback (0.3× sweet spot)
  static float fractionalPhase = 0.0f;
  fractionalPhase += GW17_PLAYBACK_SPEED;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;
  
  return waveformBuffer[playbackPhase];
}

// ==================================================
// VARIANT 18: BitBend Quad (FOUR bit manipulation layers!)
// ==================================================
// Modified Spacey Pulses with super-latched noise
// FOUR simultaneous bit manipulations on different ranges
// Bits 0-1: XOR, Bits 2-3: HOLD, Bits 4-6: SET_0, Bits 7-9: SET_1

// Quad buffer generation (Spacey Pulses style with super-latched noise)
#define GW18_CHUNK_SIZE_1 0
#define GW18_CHUNK_SIZE_2 57
#define GW18_CHUNK_SIZE_3 0
#define GW18_NOISE_PROB 12        // Sparse!
#define GW18_BLIP_SIZE 99         // Big blips
#define GW18_BLIP_FREQ_MIN 600.0f
#define GW18_BLIP_FREQ_MAX 900.0f
#define GW18_SILENCE_CHUNK_1 32
#define GW18_SILENCE_CHUNK_2 146
#define GW18_SILENCE_CHUNK_3 299  // HUGE gaps!

// GW18 BitBend Quad state
volatile float gw18PlaybackSpeed = 1.0f;
volatile uint32_t gw18BitClockPhase = 0;
volatile uint32_t gw18BitClockPhaseInc = 0;
volatile uint8_t gw18BitPos1 = 0;      // XOR on bits 0-1
volatile uint8_t gw18BitPos2 = 2;      // HOLD on bits 2-3
volatile uint8_t gw18BitPos3 = 5;      // SET_0 on bits 4-6
volatile uint8_t gw18BitPos4 = 8;      // SET_1 on bits 7-9
volatile uint16_t gw18HeldBits = 0;

void generateWaveform18()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    // Decide: noise or silence
    bool writeAudio = (rand12() % 100) < GW18_NOISE_PROB;
    
    if (writeAudio) {
      // Write super-latched noise chunk (only size 2)
      uint16_t chunkSize = GW18_CHUNK_SIZE_2;
      if (writePos + chunkSize > WAVEFORM_SIZE) {
        chunkSize = WAVEFORM_SIZE - writePos;
      }
      
      // SUPER latched - only 10% update rate = crunchy!
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 1) crushedNoise = noise1();  // 10% update = super latched!
        waveformBuffer[writePos + i] = crushedNoise;
      }
      writePos += chunkSize;
    } else {
      // Write silence chunk (variable size - BIG gaps!)
      uint16_t silenceSize;
      uint8_t choice = rand12() % 3;
      switch(choice) {
        case 0: silenceSize = GW18_SILENCE_CHUNK_1; break;
        case 1: silenceSize = GW18_SILENCE_CHUNK_2; break;
        case 2: silenceSize = GW18_SILENCE_CHUNK_3; break;
        default: silenceSize = GW18_SILENCE_CHUNK_1; break;
      }
      
      if (writePos + silenceSize > WAVEFORM_SIZE) {
        silenceSize = WAVEFORM_SIZE - writePos;
      }
      
      for (uint16_t i = 0; i < silenceSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
      writePos += silenceSize;
    }
  }
  
  // Big blips scattered (3 blips)
  for (uint8_t b = 0; b < 3; b++) {
    if (GW18_BLIP_SIZE > 0 && GW18_BLIP_SIZE < WAVEFORM_SIZE) {
      uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW18_BLIP_SIZE);
      float blipFreq = GW18_BLIP_FREQ_MIN + 
                       ((float)(rand12() % 1000) / 1000.0f) * 
                       (GW18_BLIP_FREQ_MAX - GW18_BLIP_FREQ_MIN);
      float blipPhase = 0.0f;
      float blipPhaseInc = blipFreq / SAMPLE_RATE;
      
      for (uint16_t i = 0; i < GW18_BLIP_SIZE; i++) {
        float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
        int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
        waveformBuffer[blipPos + i] = blipSample;
        blipPhase += blipPhaseInc;
        if (blipPhase >= 1.0f) blipPhase -= 1.0f;
      }
    }
  }
}

inline int16_t generativeWaveform18()
{
  // Bit clock with 50/50 Bernoulli gate
  static uint32_t lastBitClockState = 0;
  gw18BitClockPhase += gw18BitClockPhaseInc;
  uint32_t bitClockState = gw18BitClockPhase & 0x80000000;
  
  if (bitClockState && !lastBitClockState) {
    if (rand12() & 1) {  // 50% Bernoulli gate
      // Random walk all FOUR positions independently
      int8_t step1 = (rand12() & 1) ? 1 : -1;
      gw18BitPos1 = (gw18BitPos1 + step1) % 2;  // Keep in 0-1
      
      int8_t step2 = (rand12() & 1) ? 1 : -1;
      gw18BitPos2 = 2 + ((gw18BitPos2 - 2 + step2 + 2) % 2);  // Keep in 2-3
      
      int8_t step3 = (rand12() & 1) ? 1 : -1;
      gw18BitPos3 = 4 + ((gw18BitPos3 - 4 + step3 + 3) % 3);  // Keep in 4-6
      
      int8_t step4 = (rand12() & 1) ? 1 : -1;
      gw18BitPos4 = 7 + ((gw18BitPos4 - 7 + step4 + 3) % 3);  // Keep in 7-9
      
      // Capture HOLD bits (range 2-3)
      uint16_t holdMask = ((1 << (gw18BitPos2 + 1)) - 1) & ~((1 << 2) - 1);
      gw18HeldBits = playbackPhase & holdMask;
    }
  }
  lastBitClockState = bitClockState;
  
  // Advance playback
  static float fractionalPhase = 0.0f;
  fractionalPhase += gw18PlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;
  
  // QUAD manipulation: XOR + HOLD + SET_0 + SET_1
  uint16_t manipulatedAddress = playbackPhase;
  
  // 1. XOR on bits 0-1 (micro chaos)
  uint16_t xorMask = (1 << (gw18BitPos1 + 1)) - 1;
  manipulatedAddress ^= xorMask;
  
  // 2. HOLD on bits 2-3 (freeze layer)
  uint16_t holdMask = ((1 << (gw18BitPos2 + 1)) - 1) & ~((1 << 2) - 1);
  manipulatedAddress = (manipulatedAddress & ~holdMask) | (gw18HeldBits & holdMask);
  
  // 3. SET_0 on bits 4-6 (pull to lower addresses)
  uint16_t set0Mask = ((1 << (gw18BitPos3 + 1)) - 1) & ~((1 << 4) - 1);
  manipulatedAddress &= ~set0Mask;
  
  // 4. SET_1 on bits 7-9 (push to higher addresses)
  uint16_t set1Mask = ((1 << (gw18BitPos4 + 1)) - 1) & ~((1 << 7) - 1);
  manipulatedAddress |= set1Mask;
  
  manipulatedAddress %= WAVEFORM_SIZE;
  
  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// VARIANT 8: BitBender (SoundScaper-style address manipulation)
// ==================================================
// Inspired by SoundScaper's "Clock Address Lines"
// Manipulates binary address bits to create glitches, loops, and granular chaos

// ADJUST HERE: Buffer content generation (same as GW7 for now)
#define GW8_CHUNK_SIZE_1 0      // Silence chunks
#define GW8_CHUNK_SIZE_2 8      // Short chunks  
#define GW8_CHUNK_SIZE_3 64     // Long chunks
#define GW8_NOISE_PROB 55       // % chance of audio vs silence
#define GW8_SAW_PROB 40         // % of audio that's saw (vs noise)
#define GW8_SAW_FREQ_MIN 40.0f
#define GW8_SAW_FREQ_MAX 800.0f
#define GW8_BLIP_SIZE 16
#define GW8_BLIP_FREQ_MIN 800.0f
#define GW8_BLIP_FREQ_MAX 3000.0f

// BitBender state
volatile float gw8PlaybackSpeed = 1.0f;         // pot1: playback speed
volatile uint32_t gw8BitClockPhase = 0;
volatile uint32_t gw8BitClockPhaseInc = 0;      // pot2: how fast bit manipulation changes
volatile uint8_t gw8BitPosition = 5;            // Current bit being manipulated (0-11)
volatile uint8_t gw8BitMode = 0;                // 0=SET_0, 1=SET_1, 2=XOR, 3=HOLD
volatile uint16_t gw8HeldBits = 0;              // For HOLD mode

void generateWaveform8()
{
  // Generate buffer content (similar to GW7 - noise or saw chunks)
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW8_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW8_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW8_CHUNK_SIZE_3; break;
      default: chunkSize = GW8_CHUNK_SIZE_1; break;
    }
    
    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }
    
    bool writeAudio = (rand12() % 100) < GW8_NOISE_PROB;
    
    if (writeAudio) {
      // Decide: noise or saw?
      bool useSaw = (rand12() % 100) < GW8_SAW_PROB;
      
      if (useSaw) {
        // Write SAW
        float sawFreq = GW8_SAW_FREQ_MIN + 
                        ((float)(rand12() % 1000) / 1000.0f) * 
                        (GW8_SAW_FREQ_MAX - GW8_SAW_FREQ_MIN);
        float sawPhase = 0.0f;
        float sawPhaseInc = sawFreq / SAMPLE_RATE;
        
        for (uint16_t i = 0; i < chunkSize; i++) {
          int16_t sawSample = (int16_t)((sawPhase * 2.0f - 1.0f) * 511.0f);
          waveformBuffer[writePos + i] = sawSample;
          sawPhase += sawPhaseInc;
          if (sawPhase >= 1.0f) sawPhase -= 1.0f;
        }
      } else {
        // Write latched NOISE
        int16_t crushedNoise = noise1();
        for (uint16_t i = 0; i < chunkSize; i++) {
          if ((rand12() % 10) < 3) crushedNoise = noise1();
          waveformBuffer[writePos + i] = crushedNoise;
        }
      }
    } else {
      // Write silence
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }
  
  // Triangle blip
  if (GW8_BLIP_SIZE > 0 && GW8_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW8_BLIP_SIZE);
    float blipFreq = GW8_BLIP_FREQ_MIN + 
                     ((float)(rand12() % 1000) / 1000.0f) * 
                     (GW8_BLIP_FREQ_MAX - GW8_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;
    
    for (uint16_t i = 0; i < GW8_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform8()
{
  // Generative bit manipulation clock (pot2 controlled)
  static uint32_t lastBitClockState = 0;
  gw8BitClockPhase += gw8BitClockPhaseInc;
  uint32_t bitClockState = gw8BitClockPhase & 0x80000000;
  
  // Rising edge - change bit manipulation generatively
  if (bitClockState && !lastBitClockState) {
    // 50% chance to change mode
    if (rand12() & 1) {
      gw8BitMode = rand12() % 4;  // 0-3: SET_0, SET_1, XOR, HOLD
    }
    
    // Random walk bit position (wrap at 12 bits for 4096 buffer)
    int8_t step = (rand12() & 1) ? 1 : -1;
    gw8BitPosition = (gw8BitPosition + step) % 12;
    
    // For HOLD mode, capture current address state
    if (gw8BitMode == 3) {
      gw8HeldBits = playbackPhase & ((1 << gw8BitPosition) - 1);
    }
  }
  lastBitClockState = bitClockState;
  
  // Advance playback position
  static float fractionalPhase = 0.0f;
  fractionalPhase += gw8PlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;
  
  // *** THE BITBENDING MAGIC ***
  // Manipulate address bits before reading buffer (SoundScaper-style!)
  uint16_t manipulatedAddress = playbackPhase;
  uint16_t bitMask = (1 << gw8BitPosition) - 1;  // Mask for bits 0 to bitPosition
  
  switch(gw8BitMode) {
    case 0: // SET_0 - Force lower bits to 0 (creates loops in lower addresses)
      manipulatedAddress &= ~bitMask;
      break;
      
    case 1: // SET_1 - Force lower bits to 1 (jumps to higher addresses)
      manipulatedAddress |= bitMask;
      break;
      
    case 2: // XOR - Toggle bits (chaotic jumping)
      manipulatedAddress ^= bitMask;
      break;
      
    case 3: // HOLD - Freeze lower bits at captured state (repeating patterns)
      manipulatedAddress = (manipulatedAddress & ~bitMask) | (gw8HeldBits & bitMask);
      break;
  }
  
  // Keep within buffer bounds
  manipulatedAddress %= WAVEFORM_SIZE;
  
  // Read from manipulated address!
  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// VARIANT 9: BitBend Sparse (WF1 content + manual mode selection)
// ==================================================
// Uses Sparse Glitchy buffer (GW1) with bit manipulation
// More predictable than Chaos - pot2 selects mode directly

// BitBend Sparse state
volatile float gw9PlaybackSpeed = 1.0f;         // pot1: playback speed
const uint32_t GW9_BIT_CLOCK_FREQ = (uint32_t)(8.0f * 268435.456f);  // Fixed 8Hz bit clock
volatile uint32_t gw9BitClockPhase = 0;
volatile uint8_t gw9BitPosition = 5;            // Current bit being manipulated
volatile uint8_t gw9BitMode = 0;                // pot2: 0=SET_0, 1=SET_1, 2=XOR, 3=HOLD
volatile uint16_t gw9HeldBits = 0;              // For HOLD mode

// Use same generation as GW1 (Sparse Glitchy)
void generateWaveform9()
{
  // Just call GW1 generation - it uses shared waveformBuffer
  generateWaveform1();
}

inline int16_t generativeWaveform9()
{
  // Fixed 8Hz bit manipulation clock
  static uint32_t lastBitClockState = 0;
  gw9BitClockPhase += GW9_BIT_CLOCK_FREQ;
  uint32_t bitClockState = gw9BitClockPhase & 0x80000000;
  
  // Rising edge - random walk bit position only (mode is pot-controlled)
  if (bitClockState && !lastBitClockState) {
    // Random walk bit position
    int8_t step = (rand12() & 1) ? 1 : -1;
    gw9BitPosition = (gw9BitPosition + step) % 12;
    
    // For HOLD mode, capture current address state
    if (gw9BitMode == 3) {
      gw9HeldBits = playbackPhase & ((1 << gw9BitPosition) - 1);
    }
  }
  lastBitClockState = bitClockState;
  
  // Advance playback position
  static float fractionalPhase = 0.0f;
  fractionalPhase += gw9PlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;
  
  // Bit manipulation (mode selected by pot2!)
  uint16_t manipulatedAddress = playbackPhase;
  uint16_t bitMask = (1 << gw9BitPosition) - 1;
  
  switch(gw9BitMode) {
    case 0: // SET_0 - Loops in lower addresses
      manipulatedAddress &= ~bitMask;
      break;
      
    case 1: // SET_1 - Jumps to higher addresses
      manipulatedAddress |= bitMask;
      break;
      
    case 2: // XOR - Chaotic bit flipping
      manipulatedAddress ^= bitMask;
      break;
      
    case 3: // HOLD - Freeze bits (locked loops)
      manipulatedAddress = (manipulatedAddress & ~bitMask) | (gw9HeldBits & bitMask);
      break;
  }
  
  manipulatedAddress %= WAVEFORM_SIZE;
  
  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// VARIANT 10: BitBend Dual (XOR + HOLD on different bit ranges)
// ==================================================
// Uses Spacey Pulses buffer (GW3) with TWO simultaneous bit manipulators
// XOR on low bits (chaotic), HOLD on high bits (locked loops)

// BitBend Dual state
volatile float gw10PlaybackSpeed = 1.0f;        // pot1: playback speed
volatile uint32_t gw10BitClockPhase = 0;
volatile uint32_t gw10BitClockPhaseInc = 0;     // pot2: clock speed (Bernoulli gated)

// Two bit manipulators on different ranges
volatile uint8_t gw10BitPosXOR = 3;             // XOR works on bits 0-3 (low range)
volatile uint8_t gw10BitPosHOLD = 8;            // HOLD works on bits 4-8 (high range)
volatile uint16_t gw10HeldBits = 0;             // Captured bits for HOLD mode

// Use same generation as GW3 (Spacey Pulses)
void generateWaveform10()
{
  generateWaveform3();
}

inline int16_t generativeWaveform10()
{
  // Clock with 50/50 Bernoulli gate (pot2 controlled)
  static uint32_t lastBitClockState = 0;
  gw10BitClockPhase += gw10BitClockPhaseInc;
  uint32_t bitClockState = gw10BitClockPhase & 0x80000000;
  
  // Rising edge + 50/50 Bernoulli gate
  if (bitClockState && !lastBitClockState) {
    if (rand12() & 1) {  // 50% chance to process clock
      // Random walk both bit positions independently
      int8_t step1 = (rand12() & 1) ? 1 : -1;
      gw10BitPosXOR = (gw10BitPosXOR + step1) % 4;  // Keep in range 0-3
      
      int8_t step2 = (rand12() & 1) ? 1 : -1;
      gw10BitPosHOLD = 4 + ((gw10BitPosHOLD - 4 + step2 + 5) % 5);  // Keep in range 4-8
      
      // Capture state for HOLD (only high bits)
      uint16_t holdMask = ((1 << (gw10BitPosHOLD + 1)) - 1) & ~((1 << 4) - 1);
      gw10HeldBits = playbackPhase & holdMask;
    }
  }
  lastBitClockState = bitClockState;
  
  // Advance playback
  static float fractionalPhase = 0.0f;
  fractionalPhase += gw10PlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;
  
  // *** DUAL BIT MANIPULATION ***
  uint16_t manipulatedAddress = playbackPhase;
  
  // 1. XOR on low bits (0 to bitPosXOR) - chaotic jumping
  uint16_t xorMask = (1 << (gw10BitPosXOR + 1)) - 1;
  manipulatedAddress ^= xorMask;
  
  // 2. HOLD on high bits (4 to bitPosHOLD) - frozen loops
  uint16_t holdMask = ((1 << (gw10BitPosHOLD + 1)) - 1) & ~((1 << 4) - 1);
  manipulatedAddress = (manipulatedAddress & ~holdMask) | (gw10HeldBits & holdMask);
  
  manipulatedAddress %= WAVEFORM_SIZE;
  
  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// BitBend Freeze (SET_0 + HOLD on different bit ranges)
// ==================================================
// Uses Wandering Window buffer (GW5 approach) with fixed 23Hz walk
// Less chaotic than Dual - stable loops with subtle variations

// BitBend Freeze state
volatile float bbFreezePlaybackSpeed = 1.0f;
volatile uint32_t bbFreezeBitClockPhase = 0;
volatile uint32_t bbFreezeBitClockPhaseInc = 0;
volatile uint8_t bbFreezeBitPosLow = 2;         // SET_0 on bits 0-2
volatile uint8_t bbFreezeBitPosHigh = 6;        // HOLD on bits 3-6
volatile uint16_t bbFreezeHeldBits = 0;

// Use GW5-style generation with fixed walk rate
void generateWaveform11()
{
  generateWaveform5();  // Reuse Wandering Window generation
}

inline int16_t generativeWaveform11()
{
  // Fixed 23Hz bit clock (no Bernoulli gate)
  static uint32_t lastBitClockState = 0;
  bbFreezeBitClockPhase += bbFreezeBitClockPhaseInc;
  uint32_t bitClockState = bbFreezeBitClockPhase & 0x80000000;
  
  // Rising edge - random walk both bit positions
  if (bitClockState && !lastBitClockState) {
    int8_t step1 = (rand12() & 1) ? 1 : -1;
    bbFreezeBitPosLow = (bbFreezeBitPosLow + step1) % 3;  // Keep in 0-2
    
    int8_t step2 = (rand12() & 1) ? 1 : -1;
    bbFreezeBitPosHigh = 3 + ((bbFreezeBitPosHigh - 3 + step2 + 4) % 4);  // Keep in 3-6
    
    // Capture HOLD bits
    uint16_t holdMask = ((1 << (bbFreezeBitPosHigh + 1)) - 1) & ~((1 << 3) - 1);
    bbFreezeHeldBits = playbackPhase & holdMask;
  }
  lastBitClockState = bitClockState;
  
  // Advance playback
  static float fractionalPhase = 0.0f;
  fractionalPhase += bbFreezePlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;
  
  // Dual manipulation: SET_0 + HOLD
  uint16_t manipulatedAddress = playbackPhase;
  
  // 1. SET_0 on low bits (0 to bitPosLow) - loops in lower addresses
  uint16_t set0Mask = (1 << (bbFreezeBitPosLow + 1)) - 1;
  manipulatedAddress &= ~set0Mask;
  
  // 2. HOLD on high bits (3 to bitPosHigh) - frozen macro position
  uint16_t holdMask = ((1 << (bbFreezeBitPosHigh + 1)) - 1) & ~((1 << 3) - 1);
  manipulatedAddress = (manipulatedAddress & ~holdMask) | (bbFreezeHeldBits & holdMask);
  
  manipulatedAddress %= WAVEFORM_SIZE;
  
  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// BitBend Ping (XOR + SET_1 on different bit ranges)
// ==================================================
// Uses Noise or Triangle buffer (sparse, high-freq tris)
// Forward-rushing glitches that never settle

// Ping buffer generation params
#define BB_PING_CHUNK_SIZE_1 0
#define BB_PING_CHUNK_SIZE_2 7
#define BB_PING_CHUNK_SIZE_3 31
#define BB_PING_NOISE_PROB 17        // 17% audio, 83% silence
#define BB_PING_TRI_FREQ_MIN 3000.0f // High freq triangles
#define BB_PING_TRI_FREQ_MAX 4000.0f
#define BB_PING_TRI_PROB 37          // 37% triangle, 63% noise
#define BB_PING_BLIP_SIZE 12
#define BB_PING_BLIP_FREQ_MIN 600.0f
#define BB_PING_BLIP_FREQ_MAX 2500.0f

// BitBend Ping state
volatile float bbPingPlaybackSpeed = 1.0f;
volatile uint32_t bbPingBitClockPhase = 0;
volatile uint32_t bbPingBitClockPhaseInc = 0;
volatile uint8_t bbPingBitPosLow = 2;   // XOR on bits 0-2
volatile uint8_t bbPingBitPosHigh = 7;  // SET_1 on bits 3-7

void generateWaveform12()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = BB_PING_CHUNK_SIZE_1; break;
      case 1: chunkSize = BB_PING_CHUNK_SIZE_2; break;
      case 2: chunkSize = BB_PING_CHUNK_SIZE_3; break;
      default: chunkSize = BB_PING_CHUNK_SIZE_1; break;
    }
    
    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }
    
    bool writeAudio = (rand12() % 100) < BB_PING_NOISE_PROB;
    
    if (writeAudio) {
      bool useTri = (rand12() % 100) < BB_PING_TRI_PROB;
      
      if (useTri) {
        // High-frequency triangle
        float triFreq = BB_PING_TRI_FREQ_MIN + 
                        ((float)(rand12() % 1000) / 1000.0f) * 
                        (BB_PING_TRI_FREQ_MAX - BB_PING_TRI_FREQ_MIN);
        float triPhase = 0.0f;
        float triPhaseInc = triFreq / SAMPLE_RATE;
        
        for (uint16_t i = 0; i < chunkSize; i++) {
          float tri = (triPhase < 0.5f) ? (triPhase * 2.0f) : (2.0f - triPhase * 2.0f);
          int16_t triSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
          waveformBuffer[writePos + i] = triSample;
          triPhase += triPhaseInc;
          if (triPhase >= 1.0f) triPhase -= 1.0f;
        }
      } else {
        // Latched noise
        int16_t crushedNoise = noise1();
        for (uint16_t i = 0; i < chunkSize; i++) {
          if ((rand12() % 10) < 3) crushedNoise = noise1();
          waveformBuffer[writePos + i] = crushedNoise;
        }
      }
    } else {
      // Silence
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }
  
  // Triangle blip
  if (BB_PING_BLIP_SIZE > 0 && BB_PING_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - BB_PING_BLIP_SIZE);
    float blipFreq = BB_PING_BLIP_FREQ_MIN + 
                     ((float)(rand12() % 1000) / 1000.0f) * 
                     (BB_PING_BLIP_FREQ_MAX - BB_PING_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;
    
    for (uint16_t i = 0; i < BB_PING_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform12()
{
  // Bit clock with 50/50 Bernoulli gate
  static uint32_t lastBitClockState = 0;
  bbPingBitClockPhase += bbPingBitClockPhaseInc;
  uint32_t bitClockState = bbPingBitClockPhase & 0x80000000;
  
  if (bitClockState && !lastBitClockState) {
    if (rand12() & 1) {  // 50% chance
      // Random walk both positions
      int8_t step1 = (rand12() & 1) ? 1 : -1;
      bbPingBitPosLow = (bbPingBitPosLow + step1) % 3;  // Keep in 0-2
      
      int8_t step2 = (rand12() & 1) ? 1 : -1;
      bbPingBitPosHigh = 3 + ((bbPingBitPosHigh - 3 + step2 + 5) % 5);  // Keep in 3-7
    }
  }
  lastBitClockState = bitClockState;
  
  // Advance playback
  static float fractionalPhase = 0.0f;
  fractionalPhase += bbPingPlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;
  
  // Dual manipulation: XOR + SET_1
  uint16_t manipulatedAddress = playbackPhase;
  
  // 1. XOR on low bits - chaotic micro-jumps
  uint16_t xorMask = (1 << (bbPingBitPosLow + 1)) - 1;
  manipulatedAddress ^= xorMask;
  
  // 2. SET_1 on high bits - force skip forward
  uint16_t set1Mask = ((1 << (bbPingBitPosHigh + 1)) - 1) & ~((1 << 3) - 1);
  manipulatedAddress |= set1Mask;
  
  manipulatedAddress %= WAVEFORM_SIZE;
  
  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// BitBend Mirror (SET_0 + SET_1 on different bit ranges)
// ==================================================
// Uses Noise or Saw buffer with frequency sweep on saws
// Conflicting forces lock address to middle range - stable loops

// Mirror buffer generation with saw sweep
#define BB_MIRROR_CHUNK_SIZE_1 0
#define BB_MIRROR_CHUNK_SIZE_2 7
#define BB_MIRROR_CHUNK_SIZE_3 31
#define BB_MIRROR_NOISE_PROB 17
#define BB_MIRROR_SAW_PROB 79          // Mostly saws!
#define BB_MIRROR_SAW_FREQ_START 30.0f // Start freq
#define BB_MIRROR_SAW_FREQ_STEP 1.0f   // Increment per sample
#define BB_MIRROR_BLIP_SIZE 12
#define BB_MIRROR_BLIP_FREQ_MIN 600.0f
#define BB_MIRROR_BLIP_FREQ_MAX 2500.0f

// BitBend Mirror state
volatile float bbMirrorPlaybackSpeed = 1.0f;
volatile uint32_t bbMirrorBitClockPhase = 0;
volatile uint32_t bbMirrorBitClockPhaseInc = 0;
volatile uint8_t bbMirrorBitPosLow = 2;   // SET_0 on bits 0-2
volatile uint8_t bbMirrorBitPosHigh = 7;  // SET_1 on bits 3-7

void generateWaveform13()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = BB_MIRROR_CHUNK_SIZE_1; break;
      case 1: chunkSize = BB_MIRROR_CHUNK_SIZE_2; break;
      case 2: chunkSize = BB_MIRROR_CHUNK_SIZE_3; break;
      default: chunkSize = BB_MIRROR_CHUNK_SIZE_1; break;
    }
    
    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }
    
    bool writeAudio = (rand12() % 100) < BB_MIRROR_NOISE_PROB;
    
    if (writeAudio) {
      bool useSaw = (rand12() % 100) < BB_MIRROR_SAW_PROB;
      
      if (useSaw) {
        // Saw with frequency sweep
        float sawFreq = BB_MIRROR_SAW_FREQ_START;
        float sawPhase = 0.0f;
        
        for (uint16_t i = 0; i < chunkSize; i++) {
          int16_t sawSample = (int16_t)((sawPhase * 2.0f - 1.0f) * 511.0f);
          waveformBuffer[writePos + i] = sawSample;
          
          float sawPhaseInc = sawFreq / SAMPLE_RATE;
          sawPhase += sawPhaseInc;
          if (sawPhase >= 1.0f) sawPhase -= 1.0f;
          
          // Increment frequency for sweep
          sawFreq += BB_MIRROR_SAW_FREQ_STEP;
        }
      } else {
        // Latched noise
        int16_t crushedNoise = noise1();
        for (uint16_t i = 0; i < chunkSize; i++) {
          if ((rand12() % 10) < 3) crushedNoise = noise1();
          waveformBuffer[writePos + i] = crushedNoise;
        }
      }
    } else {
      // Silence
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }
  
  // Triangle blip
  if (BB_MIRROR_BLIP_SIZE > 0 && BB_MIRROR_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - BB_MIRROR_BLIP_SIZE);
    float blipFreq = BB_MIRROR_BLIP_FREQ_MIN + 
                     ((float)(rand12() % 1000) / 1000.0f) * 
                     (BB_MIRROR_BLIP_FREQ_MAX - BB_MIRROR_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;
    
    for (uint16_t i = 0; i < BB_MIRROR_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform13()
{
  // Bit clock with 50/50 Bernoulli gate
  static uint32_t lastBitClockState = 0;
  bbMirrorBitClockPhase += bbMirrorBitClockPhaseInc;
  uint32_t bitClockState = bbMirrorBitClockPhase & 0x80000000;
  
  if (bitClockState && !lastBitClockState) {
    if (rand12() & 1) {  // 50% chance
      // Random walk both positions
      int8_t step1 = (rand12() & 1) ? 1 : -1;
      bbMirrorBitPosLow = (bbMirrorBitPosLow + step1) % 3;  // Keep in 0-2
      
      int8_t step2 = (rand12() & 1) ? 1 : -1;
      bbMirrorBitPosHigh = 3 + ((bbMirrorBitPosHigh - 3 + step2 + 5) % 5);  // Keep in 3-7
    }
  }
  lastBitClockState = bitClockState;
  
  // Advance playback
  static float fractionalPhase = 0.0f;
  fractionalPhase += bbMirrorPlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;
  
  // Dual manipulation: SET_0 + SET_1 (conflicting forces!)
  uint16_t manipulatedAddress = playbackPhase;
  
  // 1. SET_0 on low bits - pull to lower addresses
  uint16_t set0Mask = (1 << (bbMirrorBitPosLow + 1)) - 1;
  manipulatedAddress &= ~set0Mask;
  
  // 2. SET_1 on high bits - push to higher addresses
  uint16_t set1Mask = ((1 << (bbMirrorBitPosHigh + 1)) - 1) & ~((1 << 3) - 1);
  manipulatedAddress |= set1Mask;
  
  manipulatedAddress %= WAVEFORM_SIZE;
  
  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// BitBend Triple (XOR + HOLD + SET_0 on three bit ranges)
// ==================================================
// Uses Spacey Pulses approach - triangle blips with silence
// THREE layers of manipulation = maximum complexity!

// Triple buffer generation (Spacey Pulses style)
#define BB_TRIPLE_CHUNK_SIZE_1 0
#define BB_TRIPLE_CHUNK_SIZE_2 33
#define BB_TRIPLE_CHUNK_SIZE_3 0
#define BB_TRIPLE_NOISE_PROB 21
#define BB_TRIPLE_BLIP_SIZE 31
#define BB_TRIPLE_BLIP_FREQ_MIN 100.0f
#define BB_TRIPLE_BLIP_FREQ_MAX 300.0f
#define BB_TRIPLE_SILENCE_CHUNK_1 63
#define BB_TRIPLE_SILENCE_CHUNK_2 127
#define BB_TRIPLE_SILENCE_CHUNK_3 251

// BitBend Triple state
volatile float bbTriplePlaybackSpeed = 1.0f;
volatile uint32_t bbTripleBitClockPhase = 0;
volatile uint32_t bbTripleBitClockPhaseInc = 0;
volatile uint8_t bbTripleBitPos1 = 1;   // XOR on bits 0-1 (chaos)
volatile uint8_t bbTripleBitPos2 = 4;   // HOLD on bits 2-4 (freeze)
volatile uint8_t bbTripleBitPos3 = 9;   // SET_0 on bits 5-9 (loop)
volatile uint16_t bbTripleHeldBits = 0;

void generateWaveform14()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    // Decide: noise or silence
    bool writeAudio = (rand12() % 100) < BB_TRIPLE_NOISE_PROB;
    
    if (writeAudio) {
      // Write noise chunk
      uint16_t chunkSize = (rand12() & 1) ? BB_TRIPLE_CHUNK_SIZE_2 : BB_TRIPLE_CHUNK_SIZE_2;
      if (writePos + chunkSize > WAVEFORM_SIZE) {
        chunkSize = WAVEFORM_SIZE - writePos;
      }
      
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
      writePos += chunkSize;
    } else {
      // Write silence chunk (variable size)
      uint16_t silenceSize;
      uint8_t choice = rand12() % 3;
      switch(choice) {
        case 0: silenceSize = BB_TRIPLE_SILENCE_CHUNK_1; break;
        case 1: silenceSize = BB_TRIPLE_SILENCE_CHUNK_2; break;
        case 2: silenceSize = BB_TRIPLE_SILENCE_CHUNK_3; break;
        default: silenceSize = BB_TRIPLE_SILENCE_CHUNK_1; break;
      }
      
      if (writePos + silenceSize > WAVEFORM_SIZE) {
        silenceSize = WAVEFORM_SIZE - writePos;
      }
      
      for (uint16_t i = 0; i < silenceSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
      writePos += silenceSize;
    }
  }
  
  // Triangle blips scattered throughout
  for (uint8_t b = 0; b < 3; b++) {  // 3 blips
    if (BB_TRIPLE_BLIP_SIZE > 0 && BB_TRIPLE_BLIP_SIZE < WAVEFORM_SIZE) {
      uint16_t blipPos = rand12() % (WAVEFORM_SIZE - BB_TRIPLE_BLIP_SIZE);
      float blipFreq = BB_TRIPLE_BLIP_FREQ_MIN + 
                       ((float)(rand12() % 1000) / 1000.0f) * 
                       (BB_TRIPLE_BLIP_FREQ_MAX - BB_TRIPLE_BLIP_FREQ_MIN);
      float blipPhase = 0.0f;
      float blipPhaseInc = blipFreq / SAMPLE_RATE;
      
      for (uint16_t i = 0; i < BB_TRIPLE_BLIP_SIZE; i++) {
        float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
        int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
        waveformBuffer[blipPos + i] = blipSample;
        blipPhase += blipPhaseInc;
        if (blipPhase >= 1.0f) blipPhase -= 1.0f;
      }
    }
  }
}

inline int16_t generativeWaveform14()
{
  // Bit clock with 50/50 Bernoulli gate
  static uint32_t lastBitClockState = 0;
  bbTripleBitClockPhase += bbTripleBitClockPhaseInc;
  uint32_t bitClockState = bbTripleBitClockPhase & 0x80000000;
  
  if (bitClockState && !lastBitClockState) {
    if (rand12() & 1) {  // 50% chance
      // Random walk all three positions independently
      int8_t step1 = (rand12() & 1) ? 1 : -1;
      bbTripleBitPos1 = (bbTripleBitPos1 + step1) % 2;  // Keep in 0-1
      
      int8_t step2 = (rand12() & 1) ? 1 : -1;
      bbTripleBitPos2 = 2 + ((bbTripleBitPos2 - 2 + step2 + 3) % 3);  // Keep in 2-4
      
      int8_t step3 = (rand12() & 1) ? 1 : -1;
      bbTripleBitPos3 = 5 + ((bbTripleBitPos3 - 5 + step3 + 5) % 5);  // Keep in 5-9
      
      // Capture HOLD bits (range 2-4)
      uint16_t holdMask = ((1 << (bbTripleBitPos2 + 1)) - 1) & ~((1 << 2) - 1);
      bbTripleHeldBits = playbackPhase & holdMask;
    }
  }
  lastBitClockState = bitClockState;
  
  // Advance playback
  static float fractionalPhase = 0.0f;
  fractionalPhase += bbTriplePlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;
  
  // TRIPLE manipulation: XOR + HOLD + SET_0
  uint16_t manipulatedAddress = playbackPhase;
  
  // 1. XOR on lowest bits (0 to bitPos1) - chaotic micro-jumps
  uint16_t xorMask = (1 << (bbTripleBitPos1 + 1)) - 1;
  manipulatedAddress ^= xorMask;
  
  // 2. HOLD on middle bits (2 to bitPos2) - frozen patterns
  uint16_t holdMask = ((1 << (bbTripleBitPos2 + 1)) - 1) & ~((1 << 2) - 1);
  manipulatedAddress = (manipulatedAddress & ~holdMask) | (bbTripleHeldBits & holdMask);
  
  // 3. SET_0 on highest bits (5 to bitPos3) - loop in lower addresses
  uint16_t set0Mask = ((1 << (bbTripleBitPos3 + 1)) - 1) & ~((1 << 5) - 1);
  manipulatedAddress &= ~set0Mask;
  
  manipulatedAddress %= WAVEFORM_SIZE;
  
  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// BitBend Sweep (dual HOLD with different walk speeds)
// ==================================================
// Uses Dense Microglitch buffer
// Both manipulators use HOLD mode but walk at different speeds
// Creates nested loop structures evolving at different rates

// Sweep buffer generation (Dense Microglitch style)
#define BB_SWEEP_CHUNK_SIZE_1 5
#define BB_SWEEP_CHUNK_SIZE_2 0
#define BB_SWEEP_CHUNK_SIZE_3 47
#define BB_SWEEP_NOISE_PROB 7
#define BB_SWEEP_BLIP_SIZE 66
#define BB_SWEEP_BLIP_FREQ_MIN 2000.0f
#define BB_SWEEP_BLIP_FREQ_MAX 4200.0f

// BitBend Sweep state
volatile float bbSweepPlaybackSpeed = 1.0f;
volatile uint32_t bbSweepBitClock1Phase = 0;  // Slow walk
volatile uint32_t bbSweepBitClock2Phase = 0;  // Fast walk
volatile uint32_t bbSweepBitClockPhaseInc = 0; // Base clock (pot2)
volatile uint8_t bbSweepBitPosLow = 2;         // HOLD on bits 0-2 (slow)
volatile uint8_t bbSweepBitPosHigh = 6;        // HOLD on bits 3-6 (fast)
volatile uint16_t bbSweepHeldBitsLow = 0;
volatile uint16_t bbSweepHeldBitsHigh = 0;

void generateWaveform15()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = BB_SWEEP_CHUNK_SIZE_1; break;
      case 1: chunkSize = BB_SWEEP_CHUNK_SIZE_2; break;
      case 2: chunkSize = BB_SWEEP_CHUNK_SIZE_3; break;
      default: chunkSize = BB_SWEEP_CHUNK_SIZE_1; break;
    }
    
    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }
    
    bool writeAudio = (rand12() % 100) < BB_SWEEP_NOISE_PROB;
    
    if (writeAudio) {
      // Latched noise
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      // Silence
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }
  
  // High-freq triangle blip
  if (BB_SWEEP_BLIP_SIZE > 0 && BB_SWEEP_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - BB_SWEEP_BLIP_SIZE);
    float blipFreq = BB_SWEEP_BLIP_FREQ_MIN + 
                     ((float)(rand12() % 1000) / 1000.0f) * 
                     (BB_SWEEP_BLIP_FREQ_MAX - BB_SWEEP_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;
    
    for (uint16_t i = 0; i < BB_SWEEP_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform15()
{
  // Two independent clocks with different speeds
  // Clock 1: Slow walk (pot2 / 3)
  static uint32_t lastBitClock1State = 0;
  bbSweepBitClock1Phase += (bbSweepBitClockPhaseInc / 3);  // 3x slower
  uint32_t bitClock1State = bbSweepBitClock1Phase & 0x80000000;
  
  if (bitClock1State && !lastBitClock1State) {
    // Slow walk - low bits
    int8_t step1 = (rand12() & 1) ? 1 : -1;
    bbSweepBitPosLow = (bbSweepBitPosLow + step1) % 3;  // Keep in 0-2
    
    // Capture HOLD bits for low range
    uint16_t holdMask = (1 << (bbSweepBitPosLow + 1)) - 1;
    bbSweepHeldBitsLow = playbackPhase & holdMask;
  }
  lastBitClock1State = bitClock1State;
  
  // Clock 2: Fast walk (pot2)
  static uint32_t lastBitClock2State = 0;
  bbSweepBitClock2Phase += bbSweepBitClockPhaseInc;  // Full speed
  uint32_t bitClock2State = bbSweepBitClock2Phase & 0x80000000;
  
  if (bitClock2State && !lastBitClock2State) {
    // Fast walk - high bits
    int8_t step2 = (rand12() & 1) ? 1 : -1;
    bbSweepBitPosHigh = 3 + ((bbSweepBitPosHigh - 3 + step2 + 4) % 4);  // Keep in 3-6
    
    // Capture HOLD bits for high range
    uint16_t holdMask = ((1 << (bbSweepBitPosHigh + 1)) - 1) & ~((1 << 3) - 1);
    bbSweepHeldBitsHigh = playbackPhase & holdMask;
  }
  lastBitClock2State = bitClock2State;
  
  // Advance playback
  static float fractionalPhase = 0.0f;
  fractionalPhase += bbSweepPlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;
  
  // Dual HOLD manipulation at different rates
  uint16_t manipulatedAddress = playbackPhase;
  
  // 1. HOLD on low bits (slow evolution)
  uint16_t holdMaskLow = (1 << (bbSweepBitPosLow + 1)) - 1;
  manipulatedAddress = (manipulatedAddress & ~holdMaskLow) | (bbSweepHeldBitsLow & holdMaskLow);
  
  // 2. HOLD on high bits (fast evolution)
  uint16_t holdMaskHigh = ((1 << (bbSweepBitPosHigh + 1)) - 1) & ~((1 << 3) - 1);
  manipulatedAddress = (manipulatedAddress & ~holdMaskHigh) | (bbSweepHeldBitsHigh & holdMaskHigh);
  
  manipulatedAddress %= WAVEFORM_SIZE;
  
  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// BitBend Triple B (SET_0 + SET_1 + HOLD on three bit ranges)
// ==================================================
// Different combo than Triple A: tug-of-war with frozen center
// SET_0 pulls low, SET_1 pushes high, HOLD freezes the middle

// Triple B buffer generation
#define BB_TRIPLEB_CHUNK_SIZE_1 0
#define BB_TRIPLEB_CHUNK_SIZE_2 47
#define BB_TRIPLEB_CHUNK_SIZE_3 0
#define BB_TRIPLEB_NOISE_PROB 17        // Sparse noise
#define BB_TRIPLEB_BLIP_SIZE 67         // Larger blip
#define BB_TRIPLEB_BLIP_FREQ_MIN 4000.0f  // High freq!
#define BB_TRIPLEB_BLIP_FREQ_MAX 6000.0f
#define BB_TRIPLEB_SILENCE_CHUNK_1 41
#define BB_TRIPLEB_SILENCE_CHUNK_2 111
#define BB_TRIPLEB_SILENCE_CHUNK_3 253

// BitBend Triple B state
volatile float bbTripleBPlaybackSpeed = 1.0f;
volatile uint32_t bbTripleBBitClockPhase = 0;
volatile uint32_t bbTripleBBitClockPhaseInc = 0;
volatile uint8_t bbTripleBBitPos1 = 1;   // SET_0 on bits 0-1 (pull to low)
volatile uint8_t bbTripleBBitPos2 = 4;   // HOLD on bits 2-4 (freeze center)
volatile uint8_t bbTripleBBitPos3 = 9;   // SET_1 on bits 5-9 (push to high)
volatile uint16_t bbTripleBHeldBits = 0;

void generateWaveform16()
{
  uint16_t writePos = 0;
  
  while (writePos < WAVEFORM_SIZE) {
    // Decide: noise or silence
    bool writeAudio = (rand12() % 100) < BB_TRIPLEB_NOISE_PROB;
    
    if (writeAudio) {
      // Write noise chunk (only size 2)
      uint16_t chunkSize = BB_TRIPLEB_CHUNK_SIZE_2;
      if (writePos + chunkSize > WAVEFORM_SIZE) {
        chunkSize = WAVEFORM_SIZE - writePos;
      }
      
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
      writePos += chunkSize;
    } else {
      // Write silence chunk (variable size - BIG gaps!)
      uint16_t silenceSize;
      uint8_t choice = rand12() % 3;
      switch(choice) {
        case 0: silenceSize = BB_TRIPLEB_SILENCE_CHUNK_1; break;
        case 1: silenceSize = BB_TRIPLEB_SILENCE_CHUNK_2; break;
        case 2: silenceSize = BB_TRIPLEB_SILENCE_CHUNK_3; break;
        default: silenceSize = BB_TRIPLEB_SILENCE_CHUNK_1; break;
      }
      
      if (writePos + silenceSize > WAVEFORM_SIZE) {
        silenceSize = WAVEFORM_SIZE - writePos;
      }
      
      for (uint16_t i = 0; i < silenceSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
      writePos += silenceSize;
    }
  }
  
  // High-freq triangle blips scattered (3 blips)
  for (uint8_t b = 0; b < 3; b++) {
    if (BB_TRIPLEB_BLIP_SIZE > 0 && BB_TRIPLEB_BLIP_SIZE < WAVEFORM_SIZE) {
      uint16_t blipPos = rand12() % (WAVEFORM_SIZE - BB_TRIPLEB_BLIP_SIZE);
      float blipFreq = BB_TRIPLEB_BLIP_FREQ_MIN + 
                       ((float)(rand12() % 1000) / 1000.0f) * 
                       (BB_TRIPLEB_BLIP_FREQ_MAX - BB_TRIPLEB_BLIP_FREQ_MIN);
      float blipPhase = 0.0f;
      float blipPhaseInc = blipFreq / SAMPLE_RATE;
      
      for (uint16_t i = 0; i < BB_TRIPLEB_BLIP_SIZE; i++) {
        float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
        int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
        waveformBuffer[blipPos + i] = blipSample;
        blipPhase += blipPhaseInc;
        if (blipPhase >= 1.0f) blipPhase -= 1.0f;
      }
    }
  }
}

inline int16_t generativeWaveform16()
{
  // Bit clock with 50/50 Bernoulli gate
  static uint32_t lastBitClockState = 0;
  bbTripleBBitClockPhase += bbTripleBBitClockPhaseInc;
  uint32_t bitClockState = bbTripleBBitClockPhase & 0x80000000;
  
  if (bitClockState && !lastBitClockState) {
    if (rand12() & 1) {  // 50% chance
      // Random walk all three positions independently
      int8_t step1 = (rand12() & 1) ? 1 : -1;
      bbTripleBBitPos1 = (bbTripleBBitPos1 + step1) % 2;  // Keep in 0-1
      
      int8_t step2 = (rand12() & 1) ? 1 : -1;
      bbTripleBBitPos2 = 2 + ((bbTripleBBitPos2 - 2 + step2 + 3) % 3);  // Keep in 2-4
      
      int8_t step3 = (rand12() & 1) ? 1 : -1;
      bbTripleBBitPos3 = 5 + ((bbTripleBBitPos3 - 5 + step3 + 5) % 5);  // Keep in 5-9
      
      // Capture HOLD bits (range 2-4)
      uint16_t holdMask = ((1 << (bbTripleBBitPos2 + 1)) - 1) & ~((1 << 2) - 1);
      bbTripleBHeldBits = playbackPhase & holdMask;
    }
  }
  lastBitClockState = bitClockState;
  
  // Advance playback
  static float fractionalPhase = 0.0f;
  fractionalPhase += bbTripleBPlaybackSpeed;
  
  uint16_t phaseIncrement = (uint16_t)fractionalPhase;
  fractionalPhase -= phaseIncrement;
  
  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;
  
  // TRIPLE manipulation: SET_0 + HOLD + SET_1
  uint16_t manipulatedAddress = playbackPhase;
  
  // 1. SET_0 on lowest bits (0 to bitPos1) - pull to lower addresses
  uint16_t set0Mask = (1 << (bbTripleBBitPos1 + 1)) - 1;
  manipulatedAddress &= ~set0Mask;
  
  // 2. HOLD on middle bits (2 to bitPos2) - frozen center
  uint16_t holdMask = ((1 << (bbTripleBBitPos2 + 1)) - 1) & ~((1 << 2) - 1);
  manipulatedAddress = (manipulatedAddress & ~holdMask) | (bbTripleBHeldBits & holdMask);
  
  // 3. SET_1 on highest bits (5 to bitPos3) - push to higher addresses
  uint16_t set1Mask = ((1 << (bbTripleBBitPos3 + 1)) - 1) & ~((1 << 5) - 1);
  manipulatedAddress |= set1Mask;
  
  manipulatedAddress %= WAVEFORM_SIZE;
  
  return waveformBuffer[manipulatedAddress];
}

// --------------------------------------------------
// Algorithm: Random Triangle (Bank 4 - Blips)
// --------------------------------------------------
volatile uint32_t randomTriPhase = 0;
volatile uint32_t randomTriPhaseInc = 0;  // Phase increment (sample & held)

// Simple envelope (attack = 0, decay controlled by pot2)
volatile float randomTriEnvelope = 0.0f;
volatile float randomTriEnvDecay = 0.9995f;  // Decay coefficient (pot2 controlled in params.h)

// Trigger timing (controlled by pot1 in params.h)
volatile uint32_t randomTriTriggerCounter = 0;
volatile uint32_t randomTriTriggerPeriod = SAMPLE_RATE / 2;  // pot1: 0.05s to 0.5s

inline int16_t randomTriangle()
{
  // Trigger clock with cheap irregularity
  randomTriTriggerCounter++;
  if (randomTriTriggerCounter >= randomTriTriggerPeriod) {
    randomTriTriggerCounter = 0;
    
    // 50% chance to skip this trigger
    bool skipTrigger = (rand12() & 1);
    
    if (!skipTrigger) {
      randomTriEnvelope = 1.0f;  // Attack = instant (0ms)
      
      // Sample & hold: new random frequency (250 Hz - 2.78 kHz)
      float randNorm = (rand12() % 1000) / 1000.0f;
      float randomFreq = 250.0f + randNorm * (2780.0f - 250.0f);
      randomTriPhaseInc = (uint32_t)(randomFreq * 268435.456f);
    }
  }
  
  // Apply decay
  randomTriEnvelope *= randomTriEnvDecay;
  
  // Advance phase (automatic wrap)
  randomTriPhase += randomTriPhaseInc;
  
  // Generate triangle from phase
  uint16_t phase_10bit = randomTriPhase >> 22;
  int16_t tri;
  if (phase_10bit < 512) {
    tri = (phase_10bit << 1) - 512;
  } else {
    tri = 1535 - (phase_10bit << 1);
  }
  
  // Apply envelope
  return (int16_t)(tri * randomTriEnvelope);
}

// --------------------------------------------------
// Algorithm: Saw Clicks (Bank 3 - Blips)
// --------------------------------------------------
// Integer math version for better performance
volatile uint32_t sawClick1Phase = 0;        // 32-bit phase accumulator
volatile uint32_t sawClick2Phase = 0;        // 32-bit phase accumulator
volatile uint32_t sawClick1PhaseInc = 65536; // Phase increment (pot1 controlled)
volatile uint32_t sawClick2PhaseInc = 65536; // Phase increment (pot2 controlled)

inline int16_t sawClicks()
{
  // Advance phase accumulators (32-bit wraps automatically)
  sawClick1Phase += sawClick1PhaseInc;
  sawClick2Phase += sawClick2PhaseInc;
  
  // Extract top 10 bits as sawtooth value (0 to 1023)
  // Shift right by 22 bits to get top 10 bits from 32-bit phase
  int16_t saw1 = (sawClick1Phase >> 22);  // 0 to 1023
  int16_t saw2 = (sawClick2Phase >> 22);  // 0 to 1023
  
  // Convert to signed: 0-1023 becomes -512 to +511
  saw1 -= 512;
  saw2 -= 512;
  
  // Mix: average the two saws (divide by 2)
  int16_t mix = (saw1 + saw2) >> 1;  // Bit shift for fast divide by 2
  
  return mix;
}

// --------------------------------------------------
// Algorithm: Dust Burst (wandering probability)
// --------------------------------------------------
volatile float dustBurstProb = 0.0f;          // current wandering probability
volatile float dustBurstWalkSpeed = 0.001f;   // how fast prob wanders (pot1)
volatile float dustBurstStepSize = 0.1f;      // how big each step is (pot2)
volatile float dustBurstFilterAlpha = 1.0f;
volatile float dustBurstFilterState = 0.0f;

inline int16_t dustBurst()
{
  // Random walk the probability
  // Each sample: small chance to take a step up or down
  if (rand12() < (uint16_t)(dustBurstWalkSpeed * 4095.0f)) {
    // Take a step
    float step = (rand12() & 1) ? dustBurstStepSize : -dustBurstStepSize;
    dustBurstProb += step;
    // Gentle gravity toward silence
    //dustBurstProb *= 0.995f;

    // Clamp 0.0 to 1.0
    if (dustBurstProb < 0.0f) dustBurstProb = 0.0f;
    if (dustBurstProb > 1.0f) dustBurstProb = 1.0f;

  }

  // Single dust generator driven by wandering probability
  int16_t rawClick = (rand12() < (uint16_t)(dustBurstProb * 4095.0f)) ? noise1() : 0;

  // 1-pole lowpass
  dustBurstFilterState += dustBurstFilterAlpha * ((float)rawClick - dustBurstFilterState);

  return (int16_t)dustBurstFilterState;
}

// --------------------------------------------------
// Algorithm: Highpass Noise (bit-masked highpass + irregular AM)
// --------------------------------------------------
const uint16_t HIGHPASS_NOISE_MASK = 0x380;  // Fixed highpass: bits 7-9
volatile uint32_t highpassNoiseAMPhase = 0;
volatile uint32_t highpassNoiseAMPhaseInc = 0;  // current cycle speed
volatile uint32_t highpassNoiseBasePhaseInc = 0; // pot2: base speed
volatile float highpassNoiseAMDepth = 1.0f;      // pot1: AM depth

// Weighted rate table - mostly 1.0 (stay), occasionally jumps
// ADJUST: change ratios or weighting to taste
const float highpassLFORateTable[16] = {
  1.0f, 1.0f, 1.0f, 1.0f,   // 25% stay same
  1.0f, 1.0f, 1.0f, 1.0f,   // 25% stay same
  1.33f, 0.5f, 0.75f, 0.89f, // 25% slight variation
  0.6f, 0.9f, 0.43f, 0.71f,  // 25% bigger jump
};

inline int16_t highpassNoise()
{
  // Generate highpass filtered noise
  int16_t n = noise1() & HIGHPASS_NOISE_MASK;

  // Advance LFO
  uint32_t prevPhase = highpassNoiseAMPhase;
  highpassNoiseAMPhase += highpassNoiseAMPhaseInc;

  // Detect LFO cycle wrap - pick new rate from table
  if (highpassNoiseAMPhase < prevPhase) {  // wrapped!
    uint8_t idx = rand12() % 16;
    highpassNoiseAMPhaseInc = (uint32_t)(highpassNoiseBasePhaseInc * 
                               highpassLFORateTable[idx]);
  }

  // Generate triangle LFO (0-1023)
  uint16_t lfoPhase10bit = highpassNoiseAMPhase >> 22;
  uint16_t lfo = (lfoPhase10bit < 512) ? (lfoPhase10bit << 1) : 
                                          (2047 - (lfoPhase10bit << 1));

  // Apply AM with depth control
  uint16_t staticLevel = 1023;
  uint16_t blendedLFO = (uint16_t)(lfo * highpassNoiseAMDepth + 
                          staticLevel * (1.0f - highpassNoiseAMDepth));
  int32_t modulated = ((int32_t)n * blendedLFO) >> 10;

  return (int16_t)modulated;
}

// --------------------------------------------------
// Algorithm: High Noise Rhythm (clock-divided rhythms)
// --------------------------------------------------
volatile uint32_t highNoiseRhythmClockPhase = 0;
volatile uint32_t highNoiseRhythmClockPhaseInc = 0;  // pot1: 1-10 Hz
volatile uint8_t highNoiseRhythmDivision = 1;        // pot2: 1-7 clock division
volatile float highNoiseRhythmEnv1 = 0.0f;
volatile float highNoiseRhythmEnv2 = 0.0f;
const float HIGH_NOISE_ENV1_DECAY = 0.965f;  // ADJUST 
const float HIGH_NOISE_ENV2_DECAY = 0.985f;  // ADJUST 
const uint16_t HIGH_NOISE_MASK = 0x3E0;       // Fixed ~5kHz highpass mask

inline int16_t highNoiseRhythm()
{
  // Main clock
  static uint32_t lastClockState = 0;
  static uint8_t dividerCounter = 0;
  
  highNoiseRhythmClockPhase += highNoiseRhythmClockPhaseInc;
  uint32_t clockState = highNoiseRhythmClockPhase & 0x80000000;
  
  // Rising edge detection
  if (clockState && !lastClockState) {
    // Main clock triggers envelope 1
    highNoiseRhythmEnv1 = 1.0f;
    
    // Clock divider for envelope 2
    dividerCounter++;
    if (dividerCounter >= highNoiseRhythmDivision) {
      dividerCounter = 0;
      highNoiseRhythmEnv2 = 1.0f;
    }
  }
  lastClockState = clockState;
  
  // Decay envelopes
  highNoiseRhythmEnv1 *= HIGH_NOISE_ENV1_DECAY;
  highNoiseRhythmEnv2 *= HIGH_NOISE_ENV2_DECAY;
  
  // Generate highpass filtered noise for env1
  int16_t n1 = noise1() & HIGH_NOISE_MASK;
  int16_t env1Out = (int16_t)(n1 * highNoiseRhythmEnv1);
  
  // Generate raw noise for env2 (divided clock)
  int16_t n2 = noise1();
  int16_t env2Out = (int16_t)(n2 * highNoiseRhythmEnv2);
  
  // Mix
  return (env1Out + env2Out) >> 1;
}

// --------------------------------------------------
// Algorithm: Vinyl Crackle (PROGMEM sample)
// --------------------------------------------------
#include "sample.h"

// Silence chunk sizes (ADJUST these to taste)
#define VC_SILENCE_1  57
#define VC_SILENCE_2 113
#define VC_SILENCE_3 331

// All state variables
volatile uint16_t vcWindowSize = 1000;       // pot2 controlled
volatile float vcPlaybackSpeed = 0.5f;       // pot1 controlled
volatile float vcFractionalPhase = 0.0f;
volatile uint16_t vcPlaybackPos = 0;
volatile uint16_t vcChunkRemaining = 0;
volatile bool vcInSilence = false;
inline int16_t vinylCrackleAlgo()
{
  // --- Chunk state machine ---
  if (vcChunkRemaining == 0) {
    if (vcInSilence) {
      // Silence just ended → start audio
      vcInSilence = false;
      vcPlaybackPos = rand12() % (VINYL_CRACKLE_SAMPLES - vcWindowSize);
      vcChunkRemaining = vcWindowSize;
    } else {
      // Audio just ended → maybe silence, maybe chain another window
      if (rand12() % 4 > 0) {  // 75% chance of silence
        vcInSilence = true;
        uint8_t choice = rand12() % 3;
        switch(choice) {
          case 0: vcChunkRemaining = VC_SILENCE_1; break;
          case 1: vcChunkRemaining = VC_SILENCE_2; break;
          case 2: vcChunkRemaining = VC_SILENCE_3; break;
        }
      } else {
        // Chain straight to another random window
        vcPlaybackPos = rand12() % (VINYL_CRACKLE_SAMPLES - vcWindowSize);
        vcChunkRemaining = vcWindowSize;
      }
    }
  }

  vcChunkRemaining--;

  // --- Output ---
  if (vcInSilence) {
    return 0;
  }

  // Read from PROGMEM
  uint16_t byteAddr = vcPlaybackPos * 2;
  uint8_t lo = pgm_read_byte(&vinylCrackle[byteAddr]);
  uint8_t hi = pgm_read_byte(&vinylCrackle[byteAddr + 1]);
  int16_t sample = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
  sample = sample >> 6;

  // Advance with fractional phase (pot1 = speed/pitch)
  vcFractionalPhase += vcPlaybackSpeed;
  uint16_t increment = (uint16_t)vcFractionalPhase;
  vcFractionalPhase -= increment;
  vcPlaybackPos += increment;

  return sample;
}


// --------------------------------------------------
// Algorithm: Fast Triangle (Bank 3 - Blips)
// --------------------------------------------------
volatile uint32_t fastTriPhase = 0;
volatile uint32_t fastTriPhaseInc = 0;    // pot1 controlled (20 - 6000 Hz)
volatile float fastTriEnvelope = 0.0f;
volatile float fastTriEnvDecay = 0.9995f; // pot2 controlled
volatile uint8_t fastTriBitcrush = 10;    // Random bitcrush per trigger (3-12 bits)

// Trigger timing (FAST! ~960 BPM with 75% skip)
volatile uint32_t fastTriTriggerCounter = 0;
volatile uint32_t fastTriTriggerPeriod = SAMPLE_RATE / 16;  // ~960 BPM

inline int16_t fastTriangle()
{
  // Trigger clock with 75% skip (only 25% of triggers fire)
  fastTriTriggerCounter++;
  if (fastTriTriggerCounter >= fastTriTriggerPeriod) {
    fastTriTriggerCounter = 0;
    
    // 75% skip
    bool skipTrigger = (rand12() & 3) > 0;
    
    if (!skipTrigger) {
      fastTriEnvelope = 1.0f;  // Attack = instant
      
      // Sample & hold: random bitcrush amount (3-12 bits)
      fastTriBitcrush = 3 + (rand12() % 10);
    }
  }
  
  // Apply envelope decay
  fastTriEnvelope *= fastTriEnvDecay;
  
  // Advance phase (automatic wrap)
  fastTriPhase += fastTriPhaseInc;
  
  // Generate triangle from phase
  uint16_t phase_10bit = fastTriPhase >> 22;
  int16_t tri;
  if (phase_10bit < 512) {
    tri = (phase_10bit << 1) - 512;
  } else {
    tri = 1535 - (phase_10bit << 1);
  }
  
  // Apply envelope
  int16_t enveloped = (int16_t)(tri * fastTriEnvelope);
  
  // AGGRESSIVE BITCRUSHING
  int16_t crushMask = ~((1 << (10 - fastTriBitcrush)) - 1);
  int16_t crushed = enveloped & crushMask;
  
  return crushed;
}

// --------------------------------------------------
// Algorithm: Harmonic Tris (Bank 3 - Blips and Tones)
// --------------------------------------------------
// 3 triangle oscillators at harmonic intervals (1x, 3x, 4x)
// Each amplitude modulated by slow triangle LFOs at different rates
volatile uint32_t harmTriPhase1 = 0, harmTriPhase2 = 0, harmTriPhase3 = 0;
volatile uint32_t harmTriLFOPhase1 = 0, harmTriLFOPhase2 = 0, harmTriLFOPhase3 = 0;
volatile uint32_t harmTriPhaseInc1 = 0;  // pot1: 50-400 Hz
volatile uint32_t harmTriPhaseInc2 = 0;  // pot1 * 3
volatile uint32_t harmTriPhaseInc3 = 0;  // pot1 * 4
volatile uint32_t harmTriLFOInc1 = 0;    // pot2 controlled
volatile uint32_t harmTriLFOInc2 = 0;    // pot2 * 1.33 (4:3 polyrhythm)
volatile uint32_t harmTriLFOInc3 = 0;    // pot2 * 1.732 (√3)

inline int16_t harmonicTris()
{
  // Advance audio oscillators
  harmTriPhase1 += harmTriPhaseInc1;
  harmTriPhase2 += harmTriPhaseInc2;
  harmTriPhase3 += harmTriPhaseInc3;
  
  // Advance LFO oscillators
  harmTriLFOPhase1 += harmTriLFOInc1;
  harmTriLFOPhase2 += harmTriLFOInc2;
  harmTriLFOPhase3 += harmTriLFOInc3;
  
  // Generate triangles from phases
  uint16_t phase1_10bit = harmTriPhase1 >> 22;
  int16_t tri1 = (phase1_10bit < 512) ? ((phase1_10bit << 1) - 512) : (1535 - (phase1_10bit << 1));
  
  uint16_t phase2_10bit = harmTriPhase2 >> 22;
  int16_t tri2 = (phase2_10bit < 512) ? ((phase2_10bit << 1) - 512) : (1535 - (phase2_10bit << 1));
  
  uint16_t phase3_10bit = harmTriPhase3 >> 22;
  int16_t tri3 = (phase3_10bit < 512) ? ((phase3_10bit << 1) - 512) : (1535 - (phase3_10bit << 1));
  
  // Generate LFO triangles (0 to 1023 range for AM)
  uint16_t lfo1_10bit = harmTriLFOPhase1 >> 22;
  uint16_t lfo1 = (lfo1_10bit < 512) ? (lfo1_10bit << 1) : (2047 - (lfo1_10bit << 1));
  
  uint16_t lfo2_10bit = harmTriLFOPhase2 >> 22;
  uint16_t lfo2 = (lfo2_10bit < 512) ? (lfo2_10bit << 1) : (2047 - (lfo2_10bit << 1));
  
  uint16_t lfo3_10bit = harmTriLFOPhase3 >> 22;
  uint16_t lfo3 = (lfo3_10bit < 512) ? (lfo3_10bit << 1) : (2047 - (lfo3_10bit << 1));
  
  // Amplitude modulation (multiply and scale down)
  int16_t mod1 = (tri1 * (int16_t)lfo1) >> 10;  // Divide by 1024
  int16_t mod2 = (tri2 * (int16_t)lfo2) >> 10;
  int16_t mod3 = (tri3 * (int16_t)lfo3) >> 10;
  
  // Mix three modulated oscillators
  int32_t sum = (int32_t)mod1 + mod2 + mod3;
  return (int16_t)(sum / 3);  // Average
}

// --------------------------------------------------
// Algorithm: Phrygian Tri (Bank 3 - Blips and Tones)
// --------------------------------------------------
// Triangle with envelope, triggered in bursts
// Frequency follows Phrygian scale via random walk
volatile uint32_t phrygTriPhase = 0;
volatile uint32_t phrygTriPhaseInc = 0;  // Current note frequency
volatile float phrygTriEnvelope = 0.0f;
volatile uint32_t phrygTriEnvCounter = 0;
volatile uint32_t phrygTriTriggerCounter = 0;
volatile uint32_t phrygTriTriggerPeriod = SAMPLE_RATE / 4;  // Base trigger rate
volatile uint32_t phrygTriBurstMod = 0;  // Burst modulation phase
volatile uint32_t phrygTriBurstModInc = 0;  // pot2 controlled
volatile int8_t phrygTriScalePos = 0;  // Position in scale (0-7)

// Phrygian scale intervals (semitones from root): 0, 1, 3, 5, 7, 8, 10, 12
const float phrygianRatios[8] = {
  1.0f,      // Root
  1.0595f,   // Minor 2nd
  1.1892f,   // Minor 3rd
  1.3348f,   // Perfect 4th
  1.4983f,   // Perfect 5th
  1.5874f,   // Minor 6th
  1.7818f,   // Minor 7th
  2.0f       // Octave
};

inline int16_t phrygianTri()
{
  // Burst modulation - creates irregular trigger timing
  phrygTriBurstMod += phrygTriBurstModInc;
  uint16_t burstPhase = phrygTriBurstMod >> 22;  // 0-1023
  
  // Variable trigger period based on burst modulation
  uint32_t currentPeriod = phrygTriTriggerPeriod + (burstPhase << 4);  // Add variation
  
  // Trigger logic
  phrygTriTriggerCounter++;
  if (phrygTriTriggerCounter >= currentPeriod) {
    phrygTriTriggerCounter = 0;
    
    // Random walk through scale
    int8_t step = (rand12() & 1) ? 1 : -1;  // ±1
    phrygTriScalePos += step;
    if (phrygTriScalePos < 0) phrygTriScalePos = 7;
    if (phrygTriScalePos > 7) phrygTriScalePos = 0;
    
    // Trigger envelope
    phrygTriEnvelope = 1.0f;
    phrygTriEnvCounter = 0;
  }
  
  // Envelope decay (50ms = 800 samples at 16kHz)
  if (phrygTriEnvCounter < 800) {
    phrygTriEnvCounter++;
    phrygTriEnvelope *= 0.9963f;  // Decay coefficient for 50ms
  } else {
    phrygTriEnvelope = 0.0f;
  }
  
  // Generate triangle
  phrygTriPhase += phrygTriPhaseInc;
  uint16_t phase_10bit = phrygTriPhase >> 22;
  int16_t tri = (phase_10bit < 512) ? ((phase_10bit << 1) - 512) : (1535 - (phase_10bit << 1));
  
  // Apply envelope
  return (int16_t)(tri * phrygTriEnvelope);
}

// --------------------------------------------------
// Algorithm: Ring Modulation (Bank 3 - Blips and Tones)
// --------------------------------------------------
// Two triangles multiplied together for ring mod effect
volatile uint32_t ringModPhase1 = 0;
volatile uint32_t ringModPhase2 = 0;
volatile uint32_t ringModPhaseInc1 = 0;  // pot1 controlled
volatile uint32_t ringModPhaseInc2 = 0;  // pot2 controlled

inline int16_t ringMod()
{
  // Advance phases
  ringModPhase1 += ringModPhaseInc1;
  ringModPhase2 += ringModPhaseInc2;
  
  // Generate triangles
  uint16_t phase1_10bit = ringModPhase1 >> 22;
  int16_t tri1 = (phase1_10bit < 512) ? ((phase1_10bit << 1) - 512) : (1535 - (phase1_10bit << 1));
  
  uint16_t phase2_10bit = ringModPhase2 >> 22;
  int16_t tri2 = (phase2_10bit < 512) ? ((phase2_10bit << 1) - 512) : (1535 - (phase2_10bit << 1));
  
  // Ring modulation: multiply and scale
  int32_t product = (int32_t)tri1 * tri2;
  return (int16_t)(product >> 9);  // Divide by 512 to get back to ±512 range
}

// --------------------------------------------------
// Algorithm: Two Saws (Bank 3 - Blips and Tones)
// --------------------------------------------------
volatile uint32_t twoSawsPhase1 = 0;
volatile uint32_t twoSawsPhase2 = 0;
volatile uint32_t twoSawsPhaseInc1 = 0;  // pot1: 15-850 Hz
volatile uint32_t twoSawsPhaseInc2 = 0;  // pot2: 15-850 Hz

inline int16_t twoSaws()
{
  // Advance phases
  twoSawsPhase1 += twoSawsPhaseInc1;
  twoSawsPhase2 += twoSawsPhaseInc2;
  
  // Generate saws from top 10 bits
  int16_t saw1 = (twoSawsPhase1 >> 22) - 512;  // -512 to +511
  int16_t saw2 = (twoSawsPhase2 >> 22) - 512;  // -512 to +511
  
  // Mix (average)
  return (saw1 + saw2) >> 1;  // Divide by 2
}

// --------------------------------------------------
// Algorithm: Noise OR Square (Bank 3 - Blips and Tones)
// --------------------------------------------------
volatile uint32_t noiseOrSqPhase = 0;
volatile uint32_t noiseOrSqPhaseInc = 0;     // pot2: 35-5000 Hz square
volatile uint32_t noiseOrSqSHCounter = 0;
volatile uint32_t noiseOrSqSHPeriod = 0;     // pot1: 15-116 Hz S&H rate
volatile int16_t noiseOrSqHeldNoise = 0;

inline int16_t noiseOrSquare()
{
  // Sample & hold noise at rate controlled by pot1
  noiseOrSqSHCounter++;
  if (noiseOrSqSHCounter >= noiseOrSqSHPeriod) {
    noiseOrSqSHCounter = 0;
    noiseOrSqHeldNoise = noise1();
  }
  
  // Generate square wave
  noiseOrSqPhase += noiseOrSqPhaseInc;
  int16_t sq = (noiseOrSqPhase >> 22) - 512;
  
  // Bitwise OR
  return noiseOrSqHeldNoise | sq;
}

// --------------------------------------------------
// Algorithm: Major Tris (Bank 3 - Blips and Tones)
// --------------------------------------------------
// Fixed chord: 220 Hz (root), major 3rd, perfect 5th
// Each oscillator has its own triangle LFO for AM
volatile uint32_t majorTrisPhase1 = 0, majorTrisPhase2 = 0, majorTrisPhase3 = 0;
volatile uint32_t majorTrisLFO1Phase = 0, majorTrisLFO2Phase = 0, majorTrisLFO3Phase = 0;
const uint32_t MAJOR_TRIS_FREQ1 = (uint32_t)(220.0f * 268435.456f);          // 220 Hz
const uint32_t MAJOR_TRIS_FREQ2 = (uint32_t)(220.0f * 1.25992f * 268435.456f);  // Major 3rd (5/4)
const uint32_t MAJOR_TRIS_FREQ3 = (uint32_t)(220.0f * 1.5f * 268435.456f);      // Perfect 5th (3/2)
const uint32_t MAJOR_TRIS_LFO3 = (uint32_t)(0.2f * 268435.456f);           // Fixed 0.2 Hz
const uint32_t MAJOR_TRIS_LFO3_SLOW = (uint32_t)(0.035f * 268435.456f);    // Fixed 0.035 Hz
volatile uint32_t majorTrisLFO1PhaseInc = 0;  // pot1: 0.08-2 Hz
volatile uint32_t majorTrisLFO2PhaseInc = 0;  // pot2: 0.08-2 Hz

inline int16_t majorTris()
{
  // Advance audio oscillator phases
  majorTrisPhase1 += MAJOR_TRIS_FREQ1;
  majorTrisPhase2 += MAJOR_TRIS_FREQ2;
  majorTrisPhase3 += MAJOR_TRIS_FREQ3;
  
  // Generate triangles
  uint16_t p1 = majorTrisPhase1 >> 22;
  int16_t tri1 = (p1 < 512) ? ((p1 << 1) - 512) : (1535 - (p1 << 1));
  
  uint16_t p2 = majorTrisPhase2 >> 22;
  int16_t tri2 = (p2 < 512) ? ((p2 << 1) - 512) : (1535 - (p2 << 1));
  
  uint16_t p3 = majorTrisPhase3 >> 22;
  int16_t tri3 = (p3 < 512) ? ((p3 << 1) - 512) : (1535 - (p3 << 1));
  
  // Generate LFO triangles (0-1023 for AM)
  majorTrisLFO1Phase += majorTrisLFO1PhaseInc;
  uint16_t lfo1_10bit = majorTrisLFO1Phase >> 22;
  uint16_t lfo1 = (lfo1_10bit < 512) ? (lfo1_10bit << 1) : (2047 - (lfo1_10bit << 1));
  
  majorTrisLFO2Phase += majorTrisLFO2PhaseInc;
  uint16_t lfo2_10bit = majorTrisLFO2Phase >> 22;
  uint16_t lfo2 = (lfo2_10bit < 512) ? (lfo2_10bit << 1) : (2047 - (lfo2_10bit << 1));
  
  // LFO3: nested AM (0.2 Hz modulated by 0.035 Hz)
  majorTrisLFO3Phase += MAJOR_TRIS_LFO3;
  uint16_t lfo3_10bit = majorTrisLFO3Phase >> 22;
  uint16_t lfo3fast = (lfo3_10bit < 512) ? (lfo3_10bit << 1) : (2047 - (lfo3_10bit << 1));
  
  // Ultra-slow LFO (static variable for 4th phase)
  static uint32_t lfo3SlowPhase = 0;
  lfo3SlowPhase += MAJOR_TRIS_LFO3_SLOW;
  uint16_t lfo3slow_10bit = lfo3SlowPhase >> 22;
  uint16_t lfo3slow = (lfo3slow_10bit < 512) ? (lfo3slow_10bit << 1) : (2047 - (lfo3slow_10bit << 1));
  
  // Nested AM for LFO3
  uint16_t lfo3 = ((uint32_t)lfo3fast * lfo3slow) >> 10;
  
  // Apply amplitude modulation
  int16_t mod1 = (tri1 * (int16_t)lfo1) >> 10;
  int16_t mod2 = (tri2 * (int16_t)lfo2) >> 10;
  int16_t mod3 = (tri3 * (int16_t)lfo3) >> 10;
  
  // Mix
  int32_t sum = (int32_t)mod1 + mod2 + mod3;
  return (int16_t)(sum / 3);
}

// --------------------------------------------------
// Algorithm: Bernoulli Tris (Bank 3 - Blips and Tones)
// --------------------------------------------------
// Two Bernoulli gates triggering enveloped triangles
// Clock goes through 50/50 Bernoulli gate before triggering
volatile uint32_t bernTrisClockPhase = 0;
const uint32_t BERN_TRIS_CLOCK_FREQ = (uint32_t)(5.0f * 268435.456f);  // ADJUST HERE: clock speed (Hz)
const float BERN_TRIS_ENV_DECAY = 0.9985f;  // ADJUST HERE: envelope decay (0.99=fast, 0.999=slow)
volatile uint32_t bernTrisPhase1 = 0, bernTrisPhase2 = 0;
volatile uint32_t bernTrisPhaseInc1 = 0;  // Bernoulli gate 1 output
volatile uint32_t bernTrisPhaseInc2 = 0;  // Bernoulli gate 2 output
volatile float bernTrisEnv1 = 0.0f, bernTrisEnv2 = 0.0f;
volatile float bernTrisProb1 = 0.5f;  // pot1
volatile float bernTrisProb2 = 0.5f;  // pot2

inline int16_t bernoulliTris()
{
  // Clock (square wave for triggers)
  static uint32_t lastClockState = 0;
  bernTrisClockPhase += BERN_TRIS_CLOCK_FREQ;
  uint32_t clockState = bernTrisClockPhase & 0x80000000;
  
  // Detect rising edge
  if (clockState && !lastClockState) {
    // 50/50 Bernoulli gate on the clock itself
    if (rand12() & 1) {  // 50% chance to let trigger through
      // Trigger!
      // Bernoulli gate 1
      float r1 = (rand12() % 1000) / 1000.0f;
      if (r1 < bernTrisProb1) {
        // Gate 1 A: 220 Hz
        bernTrisPhaseInc1 = (uint32_t)(220.0f * 268435.456f);
      } else {
        // Gate 1 B: perfect fifth (220 * 3/2)
        bernTrisPhaseInc1 = (uint32_t)(330.0f * 268435.456f);
      }
      bernTrisEnv1 = 1.0f;
      
      // Bernoulli gate 2
      float r2 = (rand12() % 1000) / 1000.0f;
      if (r2 < bernTrisProb2) {
        // Gate 2 A: minor third (220 * 6/5)
        bernTrisPhaseInc2 = (uint32_t)(264.0f * 268435.456f);
      } else {
        // Gate 2 B: minor seventh (220 * 9/5)
        bernTrisPhaseInc2 = (uint32_t)(396.0f * 268435.456f);
      }
      bernTrisEnv2 = 1.0f;
    }
    // else: clock blocked by 50/50 gate, no trigger
  }
  lastClockState = clockState;
  
  // Decay envelopes
  bernTrisEnv1 *= BERN_TRIS_ENV_DECAY;
  bernTrisEnv2 *= BERN_TRIS_ENV_DECAY;
  
  // Generate triangles
  bernTrisPhase1 += bernTrisPhaseInc1;
  uint16_t p1 = bernTrisPhase1 >> 22;
  int16_t tri1 = (p1 < 512) ? ((p1 << 1) - 512) : (1535 - (p1 << 1));
  
  bernTrisPhase2 += bernTrisPhaseInc2;
  uint16_t p2 = bernTrisPhase2 >> 22;
  int16_t tri2 = (p2 < 512) ? ((p2 << 1) - 512) : (1535 - (p2 << 1));
  
  // Apply envelopes and mix
  int16_t env1 = (int16_t)(tri1 * bernTrisEnv1);
  int16_t env2 = (int16_t)(tri2 * bernTrisEnv2);
  
  return (env1 + env2) >> 1;
}

// --------------------------------------------------
// Algorithm: Pentatonic Blips (Bank 3 - Blips and Tones)
// --------------------------------------------------
// Random pentatonic notes with envelope
volatile uint32_t pentaBlipsClockPhase = 0;
volatile uint32_t pentaBlipsClockPhaseInc = 0;  // pot1: 3-11.5 Hz
volatile uint32_t pentaBlipsPhase = 0;
volatile uint32_t pentaBlipsPhaseInc = 0;  // Selected pentatonic note
volatile float pentaBlipsEnv = 0.0f;
volatile float pentaBlipsEnvDecay = 0.999f;  // pot2 controlled

// Pentatonic scale from 220 Hz (major pentatonic: 1, 9/8, 5/4, 3/2, 27/16)
const uint32_t pentaBlipsNotes[5] = {
  (uint32_t)(220.0f * 268435.456f),           // Root
  (uint32_t)(220.0f * 1.125f * 268435.456f),  // Major 2nd (9/8)
  (uint32_t)(220.0f * 1.25f * 268435.456f),   // Major 3rd (5/4)
  (uint32_t)(220.0f * 1.5f * 268435.456f),    // Perfect 5th (3/2)
  (uint32_t)(220.0f * 1.6875f * 268435.456f)  // Major 6th (27/16)
};

inline int16_t pentatonicBlips()
{
  // Clock (square LFO -> Bernoulli gate 50/50)
  static uint32_t lastClockState = 0;
  pentaBlipsClockPhase += pentaBlipsClockPhaseInc;
  uint32_t clockState = pentaBlipsClockPhase & 0x80000000;
  
  // Rising edge + Bernoulli gate (50/50)
  if (clockState && !lastClockState) {
    if (rand12() & 1) {  // 50% chance
      // Pick random pentatonic note
      uint8_t noteIdx = rand12() % 5;
      pentaBlipsPhaseInc = pentaBlipsNotes[noteIdx];
      pentaBlipsEnv = 1.0f;  // Trigger envelope
    }
  }
  lastClockState = clockState;
  
  // Decay envelope
  pentaBlipsEnv *= pentaBlipsEnvDecay;
  
  // Generate triangle
  pentaBlipsPhase += pentaBlipsPhaseInc;
  uint16_t p = pentaBlipsPhase >> 22;
  int16_t tri = (p < 512) ? ((p << 1) - 512) : (1535 - (p << 1));
  
  // Apply envelope
  return (int16_t)(tri * pentaBlipsEnv);
}

// ==================================================
// BANK 4: LOGIC OPERATIONS
// ==================================================

// --------------------------------------------------
// Algorithm 1: Three Cascaded Squares (Amplitude Modulation Chain)
// --------------------------------------------------
volatile uint32_t logicCascadePhase1 = 0;
volatile uint32_t logicCascadePhase2 = 0;
volatile uint32_t logicCascadePhase3 = 0;
volatile uint32_t logicCascadePhaseInc1 = 0;  // Fixed 9.96 Hz
volatile uint32_t logicCascadePhaseInc2 = 0;  // pot1: 0.6 - 1024 Hz
volatile uint32_t logicCascadePhaseInc3 = 0;  // pot2: 1 - 1024 Hz

inline int16_t threeCascadedSquares()
{
  // Advance phases (automatic wrap at 2^32)
  logicCascadePhase1 += logicCascadePhaseInc1;
  logicCascadePhase2 += logicCascadePhaseInc2;
  logicCascadePhase3 += logicCascadePhaseInc3;
  
  // Extract squares from top bit of phase (bit 31)
  int16_t sq1 = (logicCascadePhase1 & 0x80000000) ? 511 : -512;
  int16_t sq2 = (logicCascadePhase2 & 0x80000000) ? 511 : -512;
  int16_t sq3 = (logicCascadePhase3 & 0x80000000) ? 511 : -512;
  
  // Amplitude modulation chain: sq1 AM -> sq2, sq2 AM -> sq3
  // Convert to 0-511 range for AM
  int16_t sq1_am = (sq1 + 512) >> 1;  // 0 to 511
  int16_t sq2_am = (sq2 + 512) >> 1;  // 0 to 511
  
  // sq1 modulates sq2
  int16_t sq2_modulated = (sq2 * sq1_am) >> 9;  // Divide by 512
  
  // sq2 modulates sq3
  int16_t sq2_mod_am = (sq2_modulated + 512) >> 1;
  int16_t output = (sq3 * sq2_mod_am) >> 9;
  
  return output;
}

// --------------------------------------------------
// Algorithm 2: NOR Square
// --------------------------------------------------
volatile uint32_t logicNorPhase1 = 0;
volatile uint32_t logicNorPhase2 = 0;
volatile uint32_t logicNorPhaseInc1 = 0;   // pot1: 0.8 - 200 Hz
volatile uint32_t logicNorPhaseInc2 = 0;   // pot2: 0.73 - 215 Hz

inline int16_t norSquare()
{
  // Advance phases
  logicNorPhase1 += logicNorPhaseInc1;
  logicNorPhase2 += logicNorPhaseInc2;
  
  // Extract squares from top 10 bits (to match DAC resolution)
  int16_t sq1 = (logicNorPhase1 >> 22) - 512;  // -512 to +511
  int16_t sq2 = (logicNorPhase2 >> 22) - 512;  // -512 to +511
  
  // Bitwise NOR: ~(A | B)
  // This operates on the actual sample values, not booleans!
  int16_t result = ~(sq1 | sq2);
  
  // Mask to 10-bit range
  return result & 0x3FF;  // Keep only bottom 10 bits
}

// --------------------------------------------------
// Algorithm 3: Tri OR Saw
// --------------------------------------------------
volatile uint32_t logicTriOrPhase1 = 0;
volatile uint32_t logicTriOrPhase2 = 0;
volatile uint32_t logicTriOrPhaseInc1 = 0;   // pot1: 4.5 - 1024 Hz
volatile uint32_t logicTriOrPhaseInc2 = 0;   // pot2: 2 - 1024 Hz

inline int16_t triOrSaw()
{
  // Advance phases
  logicTriOrPhase1 += logicTriOrPhaseInc1;
  logicTriOrPhase2 += logicTriOrPhaseInc2;
  
  // Triangle: top 10 bits, convert to triangle shape
  uint16_t phase1_10bit = logicTriOrPhase1 >> 22;
  int16_t tri;
  if (phase1_10bit < 512) {
    tri = (phase1_10bit << 1) - 512;
  } else {
    tri = 1535 - (phase1_10bit << 1);
  }
  
  // Sawtooth: top 10 bits
  int16_t saw = (logicTriOrPhase2 >> 22) - 512;
  
  // Bitwise OR
  int16_t result = tri | saw;
  
  return result;
}

// --------------------------------------------------
// Algorithm 4: Tri NOR Tri
// --------------------------------------------------
volatile uint32_t logicTriNorPhase1 = 0;
volatile uint32_t logicTriNorPhase2 = 0;
volatile uint32_t logicTriNorPhaseInc1 = 0;    // pot1: 4 - 880 Hz
volatile uint32_t logicTriNorPhaseInc2 = 0;    // pot2: 15 - 900 Hz

inline int16_t triNorTri()
{
  // Advance phases
  logicTriNorPhase1 += logicTriNorPhaseInc1;
  logicTriNorPhase2 += logicTriNorPhaseInc2;
  
  // Triangle 1
  uint16_t phase1_10bit = logicTriNorPhase1 >> 22;
  int16_t tri1;
  if (phase1_10bit < 512) {
    tri1 = (phase1_10bit << 1) - 512;
  } else {
    tri1 = 1535 - (phase1_10bit << 1);
  }
  
  // Triangle 2
  uint16_t phase2_10bit = logicTriNorPhase2 >> 22;
  int16_t tri2;
  if (phase2_10bit < 512) {
    tri2 = (phase2_10bit << 1) - 512;
  } else {
    tri2 = 1535 - (phase2_10bit << 1);
  }
  
  // Bitwise NOR: ~(A | B)
  int16_t result = ~(tri1 | tri2);
  
  return result & 0x3FF;  // Mask to 10-bit
}

// --------------------------------------------------
// Algorithm 5: Tri XOR Tri
// --------------------------------------------------
volatile uint32_t logicTriXorPhase1 = 0;
volatile uint32_t logicTriXorPhase2 = 0;
volatile uint32_t logicTriXorPhaseInc1 = 0;    // pot1: 0.7 - 220 Hz
volatile uint32_t logicTriXorPhaseInc2 = 0;    // pot2: 0.6 - 440 Hz

inline int16_t triXorTri()
{
  // Advance phases
  logicTriXorPhase1 += logicTriXorPhaseInc1;
  logicTriXorPhase2 += logicTriXorPhaseInc2;
  
  // Triangle 1
  uint16_t phase1_10bit = logicTriXorPhase1 >> 22;
  int16_t tri1;
  if (phase1_10bit < 512) {
    tri1 = (phase1_10bit << 1) - 512;
  } else {
    tri1 = 1535 - (phase1_10bit << 1);
  }
  
  // Triangle 2
  uint16_t phase2_10bit = logicTriXorPhase2 >> 22;
  int16_t tri2;
  if (phase2_10bit < 512) {
    tri2 = (phase2_10bit << 1) - 512;
  } else {
    tri2 = 1535 - (phase2_10bit << 1);
  }
  
  // Bitwise XOR
  int16_t result = tri1 ^ tri2;
  
  return result;
}

// --------------------------------------------------
// Algorithm 6: Square XNOR Square
// --------------------------------------------------
volatile uint32_t logicXnorPhase1 = 0;
volatile uint32_t logicXnorPhase2 = 0;
volatile uint32_t logicXnorPhaseInc1 = 0;   // pot1: 0.5 - 440 Hz
volatile uint32_t logicXnorPhaseInc2 = 0;   // pot2: 0.6 - 150 Hz

inline int16_t squareXnorSquare()
{
  // Advance phases
  logicXnorPhase1 += logicXnorPhaseInc1;
  logicXnorPhase2 += logicXnorPhaseInc2;
  
  // Extract squares from top 10 bits
  int16_t sq1 = (logicXnorPhase1 >> 22) - 512;
  int16_t sq2 = (logicXnorPhase2 >> 22) - 512;
  
  // Bitwise XNOR: ~(A ^ B)
  int16_t result = ~(sq1 ^ sq2);
  
  return result & 0x3FF;  // Mask to 10-bit
}

// --------------------------------------------------
// Algorithm 7: Square NAND Square
// --------------------------------------------------
volatile uint32_t logicNandPhase1 = 0;
volatile uint32_t logicNandPhase2 = 0;
volatile uint32_t logicNandPhaseInc1 = 0;   // pot1: 0.1 - 50 Hz
volatile uint32_t logicNandPhaseInc2 = 0;   // pot2: 0.08 - 45 Hz

inline int16_t squareNandSquare()
{
  // Advance phases
  logicNandPhase1 += logicNandPhaseInc1;
  logicNandPhase2 += logicNandPhaseInc2;
  
  // Extract squares from top 10 bits
  int16_t sq1 = (logicNandPhase1 >> 22) - 512;
  int16_t sq2 = (logicNandPhase2 >> 22) - 512;
  
  // Bitwise NAND: ~(A & B)
  int16_t result = ~(sq1 & sq2);
  
  return result & 0x3FF;  // Mask to 10-bit
}

// --------------------------------------------------
// Algorithm 8: Noise NOR Noise
// --------------------------------------------------
volatile uint32_t noiseNorCounter1 = 0;
volatile uint32_t noiseNorCounter2 = 0;
volatile uint32_t noiseNorPeriod1 = 0;   // pot1: 5-120 Hz (samples between S&H)
volatile uint32_t noiseNorPeriod2 = 0;   // pot2: 5-120 Hz
volatile int16_t noiseNorHeld1 = 0;      // Held noise value 1
volatile int16_t noiseNorHeld2 = 0;      // Held noise value 2

inline int16_t noiseNorNoise()
{
  // Sample & hold 1 at rate controlled by pot1
  noiseNorCounter1++;
  if (noiseNorCounter1 >= noiseNorPeriod1) {
    noiseNorCounter1 = 0;
    noiseNorHeld1 = noise1();  // Sample noise
  }
  
  // Sample & hold 2 at rate controlled by pot2
  noiseNorCounter2++;
  if (noiseNorCounter2 >= noiseNorPeriod2) {
    noiseNorCounter2 = 0;
    noiseNorHeld2 = noise1();  // Sample noise (separate instance)
  }
  
  // Bitwise NOR: ~(A | B)
  int16_t result = ~(noiseNorHeld1 | noiseNorHeld2);
  
  return result & 0x3FF;  // Mask to 10-bit
}

// --------------------------------------------------
// Algorithm 9: Square OR Square
// --------------------------------------------------
volatile uint32_t logicSquareOrPhase1 = 0;
volatile uint32_t logicSquareOrPhase2 = 0;
volatile uint32_t logicSquareOrPhaseInc1 = 0;  // pot1: 15-850 Hz
volatile uint32_t logicSquareOrPhaseInc2 = 0;  // pot2: 15-850 Hz

inline int16_t squareOrSquare()
{
  // Advance phases
  logicSquareOrPhase1 += logicSquareOrPhaseInc1;
  logicSquareOrPhase2 += logicSquareOrPhaseInc2;
  
  // Extract squares from top 10 bits
  int16_t sq1 = (logicSquareOrPhase1 >> 22) - 512;
  int16_t sq2 = (logicSquareOrPhase2 >> 22) - 512;
  
  // Bitwise OR
  return sq1 | sq2;
}

#endif // ALGOS_H
