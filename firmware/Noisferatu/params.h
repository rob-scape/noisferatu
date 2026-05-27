#ifndef PARAMS_H
#define PARAMS_H

#include <Arduino.h>
#include "hardware.h"
#include "algos.h"

// --------------------------------------------------
// Latched Noise Parameter Ranges
// --------------------------------------------------
#define HOLD_MIN_HZ 9.0f
#define HOLD_MAX_HZ 8000.0f
#define LATCH_PROB_MIN 0.05f   // never totally dead
#define LATCH_PROB_MAX 1.0f

void updateLatchedNoiseParams()
{
  // pot2 → hold rate
  float freq =
      HOLD_MIN_HZ +
      (HOLD_MAX_HZ - HOLD_MIN_HZ) * (pot2_norm * pot2_norm);
  holdPeriodSamples = max(1, (uint32_t)(SAMPLE_RATE / freq));

  // pot1 → irregularity probability (quadratic for more CCW resolution)
  float curved = pot1_norm * pot1_norm;
  float p =
      LATCH_PROB_MIN +
      (LATCH_PROB_MAX - LATCH_PROB_MIN) * curved;
  latchProbThreshold = (uint16_t)(p * 4095.0f);
}

// --------------------------------------------------
// Dust Parameter Ranges
// --------------------------------------------------
#define DUST_MIN_HZ 1.0f     // one click every ~1s
#define DUST_MAX_HZ 2000.0f
#define DUST_CUTOFF_MIN 90.0f
#define DUST_CUTOFF_MAX 8000.0f

void updateDustParams()
{
  // pot1 → lowpass cutoff frequency (20 Hz - 8000 Hz)
  float cutoff = DUST_CUTOFF_MIN + (DUST_CUTOFF_MAX - DUST_CUTOFF_MIN) * pot1_norm;
  
  // Calculate filter coefficient (expensive math done here, not in ISR!)
  dustFilterAlpha = 1.0f - expf(-2.0f * PI * cutoff / SAMPLE_RATE);
  
  // Clamp alpha to valid range
  if (dustFilterAlpha < 0.0f) dustFilterAlpha = 0.0f;
  if (dustFilterAlpha > 1.0f) dustFilterAlpha = 1.0f;
  
  // pot2 → dust probability
  float eventsPerSecond =
    DUST_MIN_HZ +
    (DUST_MAX_HZ - DUST_MIN_HZ) * pot2_norm;

  dustProb = eventsPerSecond / SAMPLE_RATE;
}

// --------------------------------------------------
// Dust Burst Parameter Ranges
// --------------------------------------------------
#define DUST_BURST_WALK_SPEED_MIN 0.0001f  // ADJUST: slowest drift
#define DUST_BURST_WALK_SPEED_MAX 0.02f    // ADJUST: fastest drift
#define DUST_BURST_STEP_MIN 0.05f          // ADJUST: smallest step (subtle)
#define DUST_BURST_STEP_MAX 0.8f           // ADJUST: largest step (wild swings)

void updateDustBurstParams()
{
  // pot1 → walk speed (how often a step happens)
  // quadratic for more resolution at slow end
  float speedNorm = pot1_norm * pot1_norm;
  dustBurstWalkSpeed = DUST_BURST_WALK_SPEED_MIN +
                       (DUST_BURST_WALK_SPEED_MAX - DUST_BURST_WALK_SPEED_MIN) * speedNorm;

  // pot2 → step size (how big each jump in probability)
  dustBurstStepSize = DUST_BURST_STEP_MIN +
                      (DUST_BURST_STEP_MAX - DUST_BURST_STEP_MIN) * pot2_norm;

  // fixed lowpass on dust output
  float cutoff = 2000.0f;
  dustBurstFilterAlpha = 1.0f - expf(-2.0f * PI * cutoff / SAMPLE_RATE);
  if (dustBurstFilterAlpha < 0.0f) dustBurstFilterAlpha = 0.0f;
  if (dustBurstFilterAlpha > 1.0f) dustBurstFilterAlpha = 1.0f;
}

// --------------------------------------------------
// Highpass Noise Parameters
// --------------------------------------------------
#define HIGHPASS_NOISE_AM_MIN 0.3f   // ADJUST: slowest LFO
#define HIGHPASS_NOISE_AM_MAX 20.0f  // ADJUST: fastest LFO

void updateHighpassNoiseParams()
{
  // pot1 → AM depth (INVERTED: CCW=full modulation, CW=static)
  highpassNoiseAMDepth = 1.0f - pot1_norm;

  // pot2 → base LFO frequency (table applies ratios on top)
  float amFreq = HIGHPASS_NOISE_AM_MIN +
                 (HIGHPASS_NOISE_AM_MAX - HIGHPASS_NOISE_AM_MIN) * pot2_norm;
  highpassNoiseBasePhaseInc = (uint32_t)(amFreq * 268435.456f);

  // Only update current speed if not mid-cycle
  // (let the wrap detection handle speed changes naturally)
  if (highpassNoiseAMPhaseInc == 0) {
    highpassNoiseAMPhaseInc = highpassNoiseBasePhaseInc;
  }
}

// --------------------------------------------------
// High Noise Rhythm Parameters
// --------------------------------------------------
#define HIGH_NOISE_RHYTHM_CLOCK_MIN 1.0f
#define HIGH_NOISE_RHYTHM_CLOCK_MAX 10.0f

void updateHighNoiseRhythmParams()
{
  // pot1 → main clock frequency (1-10 Hz)
  float clockFreq = HIGH_NOISE_RHYTHM_CLOCK_MIN + 
                    (HIGH_NOISE_RHYTHM_CLOCK_MAX - HIGH_NOISE_RHYTHM_CLOCK_MIN) * pot1_norm;
  highNoiseRhythmClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
  
  // pot2 → clock division (1-7)
  highNoiseRhythmDivision = 1 + (uint8_t)(pot2_norm * 6.0f);  // 1 to 7
  if (highNoiseRhythmDivision < 1) highNoiseRhythmDivision = 1;
  if (highNoiseRhythmDivision > 7) highNoiseRhythmDivision = 7;
}

// --------------------------------------------------
// Vinyl Crackle Parameters
// --------------------------------------------------
#define VC_SPEED_MIN 0.04f
#define VC_SPEED_MAX 2.0f
#define VC_WINDOW_MIN 200
#define VC_WINDOW_MAX 4000

void updateVinylCrackleParams()
{
  // pot1 → playback speed (pitch)
  vcPlaybackSpeed = VC_SPEED_MIN + (VC_SPEED_MAX - VC_SPEED_MIN) * pot1_norm;
  
  // pot2 → window size (short bursts to long runs)
  // stored as global so ISR can read it
  vcWindowSize = VC_WINDOW_MIN + (uint16_t)((VC_WINDOW_MAX - VC_WINDOW_MIN) * pot2_norm);
}



// --------------------------------------------------
// FMnoise Parameter Ranges
// --------------------------------------------------
#define FM_FREQ_MIN 2.0f
#define FM_FREQ_MAX 2000.0f
#define FM_CLOCK_MIN_HZ 0.5f
#define FM_CLOCK_MAX_HZ 20.0f

// ADJUST: coincidence clock ratio - irrational = never repeating
#define FM_CLOCK_RATIO 2.37f  // try: 1.618, 1.73, 2.37, 3.14

const float fmRatios[8] = {
  1.41f,   // √2 - classic bell
  1.618f,  // golden ratio - metallic
  1.73f,   // √3
  2.11f,   // gong-like
  2.37f,   // inharmonic 5th
  2.81f,   // harsh metallic
  3.14f,   // π - chaotic
  4.0f,    // hard clang
};


void updateFMnoiseParams()
{
  // pot1 → base frequency (both oscillators scale together)
  float baseFreq = FM_FREQ_MIN + (FM_FREQ_MAX - FM_FREQ_MIN) * pot1_norm;
  fmPhaseInc1 = (uint32_t)(baseFreq * 268435.456f);

  // apply current ratio to osc2
  float ratio = fmRatios[fmCurrentRatioIdx];
  fmPhaseInc2 = (uint32_t)(baseFreq * ratio * 268435.456f);

  // pot2 → coincidence clock speed
  float clockFreq = FM_CLOCK_MIN_HZ + (FM_CLOCK_MAX_HZ - FM_CLOCK_MIN_HZ) * pot2_norm;
  fmClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);

  // clock2 runs at irrational ratio of clock1 - never repeating coincidence
  fmClockPhaseInc2 = (uint32_t)(clockFreq * FM_CLOCK_RATIO * 268435.456f);
}

// --------------------------------------------------
// Noisegates Parameter Ranges
// --------------------------------------------------
#define NOISEGATE_FREQ_MIN   0.01f
#define NOISEGATE_FREQ_MAX   5.0f
#define RANDOM_WALK_STEP_MIN 1000.0f     // Phase increment step (was 0.01 Hz)
#define RANDOM_WALK_STEP_MAX 300000.0f   // Phase increment step (was 3 Hz)
#define RANDOM_WALK_RATE 8.0f  // Hz - how often to update the walk

void updateNoisegatesParams()
{
  // pot1 -> base frequency for both triangles (convert to phase increment)
  float freq = NOISEGATE_FREQ_MIN + 
               (NOISEGATE_FREQ_MAX - NOISEGATE_FREQ_MIN) * pot1_norm;
  
  ng1PhaseInc = (uint32_t)(freq * 268435.456f);
  ng2BasePhaseInc = ng1PhaseInc;  // Start with same base
  
  // pot2 -> random walk step size (in phase increment units)
  float stepSize = RANDOM_WALK_STEP_MIN + 
                   (RANDOM_WALK_STEP_MAX - RANDOM_WALK_STEP_MIN) * pot2_norm;
  randomWalkStep = (int32_t)stepSize;
  
  // Update the random walk update period
  walkUpdatePeriod = SAMPLE_RATE / RANDOM_WALK_RATE;
}

// --------------------------------------------------
// Generative Waveform Parameter Ranges (shared playback speed for all 3)
// --------------------------------------------------
#define PLAYBACK_SPEED_MIN 0.01f    // very slow playback
#define PLAYBACK_SPEED_MAX 2.0f     // fast playback

void updateGenerativeWaveform1Params()
{
  // pot1 -> playback speed (shared by all variants)
  playbackSpeed = PLAYBACK_SPEED_MIN + 
                  (PLAYBACK_SPEED_MAX - PLAYBACK_SPEED_MIN) * pot1_norm;
  
  // pot2 -> silence chunk probability (INVERTED)
  // CCW (0.0) = all silence, CW (1.0) = no silence
  //gw1SilenceProb = 1.0f - pot2_norm;
  gw1SilenceProb = 0.2f - (pot2_norm * 0.2f);  // CCW=0.4 chance for silence, CW=0.0
}

void updateGenerativeWaveform2Params()
{
  // pot1 -> playback speed (shared)
  playbackSpeed = PLAYBACK_SPEED_MIN + 
                  (PLAYBACK_SPEED_MAX - PLAYBACK_SPEED_MIN) * pot1_norm;
  
  // pot2 -> silence chunk probability (INVERTED)
  // CCW (0.0) = all silence, CW (1.0) = no silence
  gw2SilenceProb = 1.0f - pot2_norm;
}

void updateGenerativeWaveform3Params()
{
  // pot1 -> playback speed (shared)
  playbackSpeed = PLAYBACK_SPEED_MIN + 
                  (PLAYBACK_SPEED_MAX - PLAYBACK_SPEED_MIN) * pot1_norm;
  
  // pot2 -> silence chunk probability (INVERTED)
  // CCW (0.0) = all silence, CW (1.0) = no silence
  //gw3SilenceProb = 1.0f - pot2_norm;
   gw3SilenceProb = 0.1f - (pot2_norm * 0.1f);  // CCW=0.1 chance for silence, CW=0.0
}

// --------------------------------------------------
// Generative Waveform 4 Parameters (Random Jump Glitch)
// --------------------------------------------------
#define GW4_JUMP_FREQ 3.0f  // Fixed jump frequency (3 Hz)

void updateGenerativeWaveform4Params()
{
  // pot1 -> playback speed
  gw4PlaybackSpeed = PLAYBACK_SPEED_MIN + 
                     (PLAYBACK_SPEED_MAX - PLAYBACK_SPEED_MIN) * pot1_norm;
  
  // pot2 -> silence injection probability (INVERTED)
  // CCW (0.0) = all silence (muted)
  // CW (1.0) = no silence (continuous noise)
  gw4SilenceProb = 1.0f - pot2_norm;
  
  // Fixed jump frequency
  gw4JumpPeriod = (uint32_t)(SAMPLE_RATE / GW4_JUMP_FREQ);
}

// --------------------------------------------------
// Generative Waveform 5 Parameters (Wandering Window)
// --------------------------------------------------
#define GW5_WALK_FREQ_MIN 0.01f    // 0.01 Hz (very slow wander)
#define GW5_WALK_FREQ_MAX 50.0f    // 50 Hz (rapid wander)

void updateGenerativeWaveform5Params()
{
  // pot1 -> playback speed
  gw5PlaybackSpeed = PLAYBACK_SPEED_MIN + 
                     (PLAYBACK_SPEED_MAX - PLAYBACK_SPEED_MIN) * pot1_norm;
  
  // pot2 -> random walk frequency
  float walkFreq = GW5_WALK_FREQ_MIN + 
                   (GW5_WALK_FREQ_MAX - GW5_WALK_FREQ_MIN) * pot2_norm;
  gw5WalkPeriod = (uint32_t)(SAMPLE_RATE / walkFreq);
}

// --------------------------------------------------
// Generative Waveform 6 Parameters (Manual Window with Spray)
// --------------------------------------------------
void updateGenerativeWaveform6Params()
{
  // pot1 -> window start position (0 to WAVEFORM_SIZE)
  gw6WindowStart = (uint16_t)(pot1_norm * (WAVEFORM_SIZE - 1));
  
  // pot2 -> window size (1 to full buffer)
  gw6WindowSize = 1 + (uint16_t)(pot2_norm * (WAVEFORM_SIZE - 1));
  
  // Ensure window doesn't exceed buffer
  if (gw6WindowStart + gw6WindowSize > WAVEFORM_SIZE) {
    gw6WindowSize = WAVEFORM_SIZE - gw6WindowStart;
  }
}

// --------------------------------------------------
// Generative Waveform 7 Parameters (Noise or Saw Window)
// --------------------------------------------------
#define GW7_SPEED_MIN 0.1f
#define GW7_SPEED_MAX 4.0f
#define GW7_WALK_RATE_MIN 0.5f
#define GW7_WALK_RATE_MAX 20.0f

void updateGenerativeWaveform7Params()
{
  // pot1 -> playback speed (0.1x to 4x)
  gw7PlaybackSpeed = GW7_SPEED_MIN + (GW7_SPEED_MAX - GW7_SPEED_MIN) * pot1_norm;
  
  // pot2 -> window walk rate (same as GW5)
  float walkRate = GW7_WALK_RATE_MIN + (GW7_WALK_RATE_MAX - GW7_WALK_RATE_MIN) * pot2_norm;
  gw7WalkPeriod = (uint32_t)(SAMPLE_RATE / walkRate);
}

// --------------------------------------------------
// Generative Waveform 17 Parameters (Harmonic Drone Builder)
// --------------------------------------------------
void updateGenerativeWaveform17Params()
{
  // Playback speed is fixed at 0.3×
  // Regen interval is fixed at 3 seconds
  
  // pot1 -> octave multiplier (5 positions)
  // New chunks slide in at shifted octave!
  if (pot1_norm < 0.2f) {
    gw17OctaveMult = 0.5f;      // Down octave
  } else if (pot1_norm < 0.4f) {
    gw17OctaveMult = 1.0f;      // Original pitch
  } else if (pot1_norm < 0.6f) {
    gw17OctaveMult = 2.0f;      // Up octave
  } else if (pot1_norm < 0.8f) {
    gw17OctaveMult = 4.0f;      // Up 2 octaves
  } else {
    gw17OctaveMult = 8.0f;      // Up 3 octaves
  }
  
  // pot2 -> stutter/pause probability (INVERTED for consistency)
  // CCW (0.0) = max stutter (GW17_STUTTER_MAX_PROB, adjust in algos.h!)
  // CW (1.0) = no stutter, smooth drones
  gw17StutterProb = GW17_STUTTER_MAX_PROB * (1.0f - pot2_norm);
}

// --------------------------------------------------
// Generative Waveform 18 Parameters (BitBend Quad)
// --------------------------------------------------
#define GW18_SPEED_MIN 0.1f
#define GW18_SPEED_MAX 4.0f
#define GW18_CLOCK_MIN 0.5f
#define GW18_CLOCK_MAX 20.0f

void updateGenerativeWaveform18Params()
{
  // pot1 -> playback speed
  gw18PlaybackSpeed = GW18_SPEED_MIN + (GW18_SPEED_MAX - GW18_SPEED_MIN) * pot1_norm;
  
  // pot2 -> bit clock rate (Bernoulli gated 50/50)
  float clockFreq = GW18_CLOCK_MIN + (GW18_CLOCK_MAX - GW18_CLOCK_MIN) * pot2_norm;
  gw18BitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// Generative Waveform 8 Parameters (BitBender)
// --------------------------------------------------
#define GW8_SPEED_MIN 0.1f
#define GW8_SPEED_MAX 4.0f
#define GW8_BIT_CLOCK_MIN 0.5f   // Slow bit manipulation changes
#define GW8_BIT_CLOCK_MAX 20.0f  // Fast chaotic bit changes

void updateGenerativeWaveform8Params()
{
  // pot1 -> playback speed (0.1x to 4x) - consistent with other wavetables!
  gw8PlaybackSpeed = GW8_SPEED_MIN + (GW8_SPEED_MAX - GW8_SPEED_MIN) * pot1_norm;
  
  // pot2 -> bit manipulation clock rate (how fast the glitching changes)
  float bitClockFreq = GW8_BIT_CLOCK_MIN + (GW8_BIT_CLOCK_MAX - GW8_BIT_CLOCK_MIN) * pot2_norm;
  gw8BitClockPhaseInc = (uint32_t)(bitClockFreq * 268435.456f);
}

// --------------------------------------------------
// Generative Waveform 9 Parameters (BitBend Sparse)
// --------------------------------------------------
#define GW9_SPEED_MIN 0.1f
#define GW9_SPEED_MAX 4.0f

void updateGenerativeWaveform9Params()
{
  // pot1 -> playback speed (0.1x to 4x)
  gw9PlaybackSpeed = GW9_SPEED_MIN + (GW9_SPEED_MAX - GW9_SPEED_MIN) * pot1_norm;
  
  // pot2 -> bit manipulation mode (0-3)
  // CCW = SET_0 (loops), 25% = SET_1 (jumps), 50% = XOR (chaos), CW = HOLD (freeze)
  gw9BitMode = (uint8_t)(pot2_norm * 3.999f);  // 0-3
  if (gw9BitMode > 3) gw9BitMode = 3;
}

// --------------------------------------------------
// Generative Waveform 10 Parameters (BitBend Dual)
// --------------------------------------------------
#define GW10_SPEED_MIN 0.1f
#define GW10_SPEED_MAX 4.0f
#define GW10_CLOCK_MIN 0.5f
#define GW10_CLOCK_MAX 20.0f

void updateGenerativeWaveform10Params()
{
  // pot1 -> playback speed (0.1x to 4x)
  gw10PlaybackSpeed = GW10_SPEED_MIN + (GW10_SPEED_MAX - GW10_SPEED_MIN) * pot1_norm;
  
  // pot2 -> bit manipulation clock rate (goes through 50/50 Bernoulli gate)
  float clockFreq = GW10_CLOCK_MIN + (GW10_CLOCK_MAX - GW10_CLOCK_MIN) * pot2_norm;
  gw10BitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// BitBend Freeze Parameters (GW11)
// --------------------------------------------------
#define BB_FREEZE_SPEED_MIN 0.1f
#define BB_FREEZE_SPEED_MAX 4.0f
#define BB_FREEZE_CLOCK_MIN 0.5f
#define BB_FREEZE_CLOCK_MAX 20.0f

void updateGenerativeWaveform11Params()
{
  // pot1 -> playback speed
  bbFreezePlaybackSpeed = BB_FREEZE_SPEED_MIN + (BB_FREEZE_SPEED_MAX - BB_FREEZE_SPEED_MIN) * pot1_norm;
  
  // pot2 -> bit clock (fixed 23Hz is at ~pot2=mid, but user can adjust)
  float clockFreq = BB_FREEZE_CLOCK_MIN + (BB_FREEZE_CLOCK_MAX - BB_FREEZE_CLOCK_MIN) * pot2_norm;
  bbFreezeBitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// BitBend Ping Parameters (GW12)
// --------------------------------------------------
#define BB_PING_SPEED_MIN 0.1f
#define BB_PING_SPEED_MAX 4.0f
#define BB_PING_CLOCK_MIN 0.5f
#define BB_PING_CLOCK_MAX 20.0f

void updateGenerativeWaveform12Params()
{
  // pot1 -> playback speed
  bbPingPlaybackSpeed = BB_PING_SPEED_MIN + (BB_PING_SPEED_MAX - BB_PING_SPEED_MIN) * pot1_norm;
  
  // pot2 -> bit clock rate (Bernoulli gated in ISR)
  float clockFreq = BB_PING_CLOCK_MIN + (BB_PING_CLOCK_MAX - BB_PING_CLOCK_MIN) * pot2_norm;
  bbPingBitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// BitBend Mirror Parameters (GW13)
// --------------------------------------------------
#define BB_MIRROR_SPEED_MIN 0.1f
#define BB_MIRROR_SPEED_MAX 4.0f
#define BB_MIRROR_CLOCK_MIN 0.5f
#define BB_MIRROR_CLOCK_MAX 20.0f

void updateGenerativeWaveform13Params()
{
  // pot1 -> playback speed
  bbMirrorPlaybackSpeed = BB_MIRROR_SPEED_MIN + (BB_MIRROR_SPEED_MAX - BB_MIRROR_SPEED_MIN) * pot1_norm;
  
  // pot2 -> bit clock rate (Bernoulli gated)
  float clockFreq = BB_MIRROR_CLOCK_MIN + (BB_MIRROR_CLOCK_MAX - BB_MIRROR_CLOCK_MIN) * pot2_norm;
  bbMirrorBitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// BitBend Triple Parameters (GW14)
// --------------------------------------------------
#define BB_TRIPLE_SPEED_MIN 0.1f
#define BB_TRIPLE_SPEED_MAX 4.0f
#define BB_TRIPLE_CLOCK_MIN 0.5f
#define BB_TRIPLE_CLOCK_MAX 20.0f

void updateGenerativeWaveform14Params()
{
  // pot1 -> playback speed
  bbTriplePlaybackSpeed = BB_TRIPLE_SPEED_MIN + (BB_TRIPLE_SPEED_MAX - BB_TRIPLE_SPEED_MIN) * pot1_norm;
  
  // pot2 -> bit clock rate (Bernoulli gated)
  float clockFreq = BB_TRIPLE_CLOCK_MIN + (BB_TRIPLE_CLOCK_MAX - BB_TRIPLE_CLOCK_MIN) * pot2_norm;
  bbTripleBitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// BitBend Sweep Parameters (GW15)
// --------------------------------------------------
#define BB_SWEEP_SPEED_MIN 0.1f
#define BB_SWEEP_SPEED_MAX 4.0f
#define BB_SWEEP_CLOCK_MIN 0.5f
#define BB_SWEEP_CLOCK_MAX 20.0f

void updateGenerativeWaveform15Params()
{
  // pot1 -> playback speed
  bbSweepPlaybackSpeed = BB_SWEEP_SPEED_MIN + (BB_SWEEP_SPEED_MAX - BB_SWEEP_SPEED_MIN) * pot1_norm;
  
  // pot2 -> base bit clock rate (clock1 is 3x slower, clock2 is full speed)
  float clockFreq = BB_SWEEP_CLOCK_MIN + (BB_SWEEP_CLOCK_MAX - BB_SWEEP_CLOCK_MIN) * pot2_norm;
  bbSweepBitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// BitBend Triple B Parameters (GW16)
// --------------------------------------------------
#define BB_TRIPLEB_SPEED_MIN 0.1f
#define BB_TRIPLEB_SPEED_MAX 4.0f
#define BB_TRIPLEB_CLOCK_MIN 0.5f
#define BB_TRIPLEB_CLOCK_MAX 20.0f

void updateGenerativeWaveform16Params()
{
  // pot1 -> playback speed
  bbTripleBPlaybackSpeed = BB_TRIPLEB_SPEED_MIN + (BB_TRIPLEB_SPEED_MAX - BB_TRIPLEB_SPEED_MIN) * pot1_norm;
  
  // pot2 -> bit clock rate (Bernoulli gated)
  float clockFreq = BB_TRIPLEB_CLOCK_MIN + (BB_TRIPLEB_CLOCK_MAX - BB_TRIPLEB_CLOCK_MIN) * pot2_norm;
  bbTripleBBitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// Random Triangle Parameter Ranges (Bank 4 - Blips)
// --------------------------------------------------
#define RANDOM_TRI_TRIGGER_MIN 0.05f   // 50ms between triggers (fast)
#define RANDOM_TRI_TRIGGER_MAX 0.5f    // 500ms between triggers (slow)
#define RANDOM_TRI_DECAY_MIN 0.005f    // 5ms decay (fast)
#define RANDOM_TRI_DECAY_MAX 0.6f      // 600ms decay (slow)

void updateRandomTriangleParams()
{
  // pot1 -> trigger rate (REVERSED: pot full CW = fast, CCW = slow)
  float triggerTime = RANDOM_TRI_TRIGGER_MIN + 
                      (RANDOM_TRI_TRIGGER_MAX - RANDOM_TRI_TRIGGER_MIN) * (1.0f - pot1_norm);
  randomTriTriggerPeriod = (uint32_t)(triggerTime * SAMPLE_RATE);
  
  // pot2 -> envelope decay time (5ms - 600ms)
  // Map decay time to coefficient using simple approximation (NO exp() needed!)
  // Longer decay = coefficient closer to 1.0
  float decayTime = RANDOM_TRI_DECAY_MIN + 
                    (RANDOM_TRI_DECAY_MAX - RANDOM_TRI_DECAY_MIN) * pot2_norm;
  
  // Simple coefficient calculation
  // For decay to reach ~0.001 in decayTime seconds:
  // coeff = 1 - (3 / (decayTime * SAMPLE_RATE))
  // Clamped to ensure 0 < coeff < 1
  float coeff = 1.0f - (3.0f / (decayTime * SAMPLE_RATE));
  if (coeff < 0.9f) coeff = 0.9f;          // Minimum (fastest decay)
  if (coeff > 0.9999f) coeff = 0.9999f;    // Maximum (slowest decay)
  
  randomTriEnvDecay = coeff;
}

// --------------------------------------------------
// Saw Clicks Parameter Ranges (Bank 3 - Blips)
// --------------------------------------------------
#define SAW_FREQ_MIN 0.001f
#define SAW_FREQ_MAX 20.0f

void updateSawClicksParams()
{
  // pot1 -> sawtooth 1 frequency (0.001 - 20 Hz)
  float freq1 = SAW_FREQ_MIN + (SAW_FREQ_MAX - SAW_FREQ_MIN) * pot1_norm;
  
  // pot2 -> sawtooth 2 frequency (0.001 - 20 Hz)
  float freq2 = SAW_FREQ_MIN + (SAW_FREQ_MAX - SAW_FREQ_MIN) * pot2_norm;
  
  // Convert frequency to 32-bit phase increment
  // phaseInc = freq * (2^32 / sampleRate)
  // For 16kHz: phaseInc = freq * 268435.456
  sawClick1PhaseInc = (uint32_t)(freq1 * 268435.456f);
  sawClick2PhaseInc = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Fast Triangle Parameter Ranges (Bank 3 - Blips)
// --------------------------------------------------
#define FAST_TRI_FREQ_MIN 11.0f
#define FAST_TRI_FREQ_MAX 6000.0f
#define FAST_TRI_DECAY_MIN 0.0074f   // 7.4ms
#define FAST_TRI_DECAY_MAX 0.7f      // 700ms

void updateFastTriangleParams()
{
  // pot1 -> triangle frequency (20 - 6000 Hz) - convert to phase increment
  float freq = FAST_TRI_FREQ_MIN + 
               (FAST_TRI_FREQ_MAX - FAST_TRI_FREQ_MIN) * pot1_norm;
  fastTriPhaseInc = (uint32_t)(freq * 268435.456f);
  
  // pot2 -> envelope decay (7.4ms - 700ms)
  float decayTime = FAST_TRI_DECAY_MIN + 
                    (FAST_TRI_DECAY_MAX - FAST_TRI_DECAY_MIN) * pot2_norm;
  
  float coeff = 1.0f - (3.0f / (decayTime * SAMPLE_RATE));
  if (coeff < 0.9f) coeff = 0.9f;
  if (coeff > 0.9999f) coeff = 0.9999f;
  
  fastTriEnvDecay = coeff;
  
  // Trigger period: ~960 BPM = 16 beats/sec = 0.0625s between triggers
  fastTriTriggerPeriod = SAMPLE_RATE / 16;
}

// --------------------------------------------------
// Harmonic Tris Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define HARM_TRI_FREQ_MIN 50.0f
#define HARM_TRI_FREQ_MAX 400.0f
#define HARM_TRI_LFO_MIN 0.001f   // Very slow (half a minute period)
#define HARM_TRI_LFO_MAX 10.0f    // Medium fast

void updateHarmonicTrisParams()
{
  // pot1 -> base frequency (50-400 Hz)
  float baseFreq = HARM_TRI_FREQ_MIN + 
                   (HARM_TRI_FREQ_MAX - HARM_TRI_FREQ_MIN) * pot1_norm;
  
  harmTriPhaseInc1 = (uint32_t)(baseFreq * 268435.456f);
  harmTriPhaseInc2 = (uint32_t)(baseFreq * 3.0f * 268435.456f);  // 3x harmonic
  harmTriPhaseInc3 = (uint32_t)(baseFreq * 4.0f * 268435.456f);  // 4x harmonic
  
  // pot2 -> LFO speed with EXPONENTIAL scaling
  // This keeps the slow range accessible across most of the pot's travel
  float expPot = pot2_norm * pot2_norm * pot2_norm;  // Cube for exponential curve
  float lfoFreq = HARM_TRI_LFO_MIN + 
                  (HARM_TRI_LFO_MAX - HARM_TRI_LFO_MIN) * expPot;
  
  harmTriLFOInc1 = (uint32_t)(lfoFreq * 268435.456f);              // Osc 1: base rate
  harmTriLFOInc2 = (uint32_t)(lfoFreq * 1.618f * 268435.456f);    // Osc 2: Golden ratio
  harmTriLFOInc3 = (uint32_t)(lfoFreq * 1.732f * 268435.456f);    // Osc 3: √3 ratio
}

// --------------------------------------------------
// Phrygian Tri Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define PHRYG_ROOT_MIN 50.0f
#define PHRYG_ROOT_MAX 200.0f
#define PHRYG_BURST_MIN 0.5f   // Slow burst modulation
#define PHRYG_BURST_MAX 8.0f   // Fast burst modulation

void updatePhrygianTriParams()
{
  // pot1 -> burst modulation speed (affects trigger irregularity) - SWAPPED!
  float burstFreq = PHRYG_BURST_MIN + 
                    (PHRYG_BURST_MAX - PHRYG_BURST_MIN) * pot1_norm;
  phrygTriBurstModInc = (uint32_t)(burstFreq * 268435.456f);
  
  // pot2 -> root frequency - SWAPPED!
  float rootFreq = PHRYG_ROOT_MIN + 
                   (PHRYG_ROOT_MAX - PHRYG_ROOT_MIN) * pot2_norm;
  
  // Apply current scale position
  float currentFreq = rootFreq * phrygianRatios[phrygTriScalePos];
  phrygTriPhaseInc = (uint32_t)(currentFreq * 268435.456f);
  
  // Base trigger rate from pot2 (slower = more space between bursts)
  phrygTriTriggerPeriod = SAMPLE_RATE / (uint32_t)(burstFreq * 2.0f);
}

// --------------------------------------------------
// Ring Mod Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define RING_FREQ1_MIN 15.0f
#define RING_FREQ1_MAX 2000.0f
#define RING_FREQ2_MIN 15.0f
#define RING_FREQ2_MAX 2000.0f

void updateRingModParams()
{
  // pot1 -> oscillator 1 frequency
  float freq1 = RING_FREQ1_MIN + 
                (RING_FREQ1_MAX - RING_FREQ1_MIN) * pot1_norm;
  ringModPhaseInc1 = (uint32_t)(freq1 * 268435.456f);
  
  // pot2 -> oscillator 2 frequency
  float freq2 = RING_FREQ2_MIN + 
                (RING_FREQ2_MAX - RING_FREQ2_MIN) * pot2_norm;
  ringModPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Two Saws Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define TWO_SAWS_FREQ_MIN 15.0f
#define TWO_SAWS_FREQ_MAX 850.0f

void updateTwoSawsParams()
{
  // pot1 -> saw 1 frequency
  float freq1 = TWO_SAWS_FREQ_MIN + (TWO_SAWS_FREQ_MAX - TWO_SAWS_FREQ_MIN) * pot1_norm;
  twoSawsPhaseInc1 = (uint32_t)(freq1 * 268435.456f);
  
  // pot2 -> saw 2 frequency
  float freq2 = TWO_SAWS_FREQ_MIN + (TWO_SAWS_FREQ_MAX - TWO_SAWS_FREQ_MIN) * pot2_norm;
  twoSawsPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Noise OR Square Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define NOISE_OR_SQ_SH_FREQ_MIN 15.0f
#define NOISE_OR_SQ_SH_FREQ_MAX 116.0f
#define NOISE_OR_SQ_FREQ_MIN 35.0f
#define NOISE_OR_SQ_FREQ_MAX 5000.0f

void updateNoiseOrSquareParams()
{
  // pot1 -> noise S&H frequency (converted to sample period)
  float shFreq = NOISE_OR_SQ_SH_FREQ_MIN + 
                 (NOISE_OR_SQ_SH_FREQ_MAX - NOISE_OR_SQ_SH_FREQ_MIN) * pot1_norm;
  noiseOrSqSHPeriod = (uint32_t)(SAMPLE_RATE / shFreq);
  
  // pot2 -> square wave frequency
  float sqFreq = NOISE_OR_SQ_FREQ_MIN + 
                 (NOISE_OR_SQ_FREQ_MAX - NOISE_OR_SQ_FREQ_MIN) * pot2_norm;
  noiseOrSqPhaseInc = (uint32_t)(sqFreq * 268435.456f);
}

// --------------------------------------------------
// Major Tris Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define MAJOR_TRIS_LFO_MIN 0.08f
#define MAJOR_TRIS_LFO_MAX 2.0f

void updateMajorTrisParams()
{
  // pot1 -> LFO 1 frequency
  float lfo1Freq = MAJOR_TRIS_LFO_MIN + 
                   (MAJOR_TRIS_LFO_MAX - MAJOR_TRIS_LFO_MIN) * pot1_norm;
  majorTrisLFO1PhaseInc = (uint32_t)(lfo1Freq * 268435.456f);
  
  // pot2 -> LFO 2 frequency
  float lfo2Freq = MAJOR_TRIS_LFO_MIN + 
                   (MAJOR_TRIS_LFO_MAX - MAJOR_TRIS_LFO_MIN) * pot2_norm;
  majorTrisLFO2PhaseInc = (uint32_t)(lfo2Freq * 268435.456f);
}

// --------------------------------------------------
// Bernoulli Tris Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
void updateBernoulliTrisParams()
{
  // pot1 -> Bernoulli gate 1 probability (0.0 to 1.0)
  bernTrisProb1 = pot1_norm;
  
  // pot2 -> Bernoulli gate 2 probability (0.0 to 1.0)
  bernTrisProb2 = pot2_norm;
}

// --------------------------------------------------
// Pentatonic Blips Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define PENTA_BLIPS_CLOCK_MIN 3.0f
#define PENTA_BLIPS_CLOCK_MAX 11.5f
#define PENTA_BLIPS_DECAY_MIN 0.001f   // 1ms
#define PENTA_BLIPS_DECAY_MAX 0.5f     // 500ms

void updatePentatonicBlipsParams()
{
  // pot1 -> clock frequency (3-11.5 Hz)
  float clockFreq = PENTA_BLIPS_CLOCK_MIN + 
                    (PENTA_BLIPS_CLOCK_MAX - PENTA_BLIPS_CLOCK_MIN) * pot1_norm;
  pentaBlipsClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
  
  // pot2 -> envelope decay time (1ms - 500ms)
  float decayTime = PENTA_BLIPS_DECAY_MIN + 
                    (PENTA_BLIPS_DECAY_MAX - PENTA_BLIPS_DECAY_MIN) * pot2_norm;
  
  // Calculate decay coefficient
  float coeff = 1.0f - (3.0f / (decayTime * SAMPLE_RATE));
  if (coeff < 0.9f) coeff = 0.9f;
  if (coeff > 0.9999f) coeff = 0.9999f;
  pentaBlipsEnvDecay = coeff;
}

// ==================================================
// BANK 4: LOGIC OPERATIONS PARAMETERS
// ==================================================

// --------------------------------------------------
// Three Cascaded Squares Parameters
// --------------------------------------------------
#define CASCADE_FREQ1 9.96f          // Fixed frequency for osc 1
#define CASCADE_FREQ2_MIN 0.6f
#define CASCADE_FREQ2_MAX 1024.0f
#define CASCADE_FREQ3_MIN 1.0f
#define CASCADE_FREQ3_MAX 1024.0f

void updateThreeCascadedSquaresParams()
{
  // Fixed osc 1 at 9.96 Hz
  logicCascadePhaseInc1 = (uint32_t)(CASCADE_FREQ1 * 268435.456f);
  
  // pot1 -> oscillator 2 frequency
  float freq2 = CASCADE_FREQ2_MIN + 
                (CASCADE_FREQ2_MAX - CASCADE_FREQ2_MIN) * pot1_norm;
  logicCascadePhaseInc2 = (uint32_t)(freq2 * 268435.456f);
  
  // pot2 -> oscillator 3 frequency
  float freq3 = CASCADE_FREQ3_MIN + 
                (CASCADE_FREQ3_MAX - CASCADE_FREQ3_MIN) * pot2_norm;
  logicCascadePhaseInc3 = (uint32_t)(freq3 * 268435.456f);
}

// --------------------------------------------------
// NOR Square Parameters
// --------------------------------------------------
#define NOR_FREQ1_MIN 0.8f
#define NOR_FREQ1_MAX 200.0f
#define NOR_FREQ2_MIN 0.73f
#define NOR_FREQ2_MAX 215.0f

void updateNorSquareParams()
{
  // pot1 -> oscillator 1 frequency
  float freq1 = NOR_FREQ1_MIN + (NOR_FREQ1_MAX - NOR_FREQ1_MIN) * pot1_norm;
  logicNorPhaseInc1 = (uint32_t)(freq1 * 268435.456f);
  
  // pot2 -> oscillator 2 frequency
  float freq2 = NOR_FREQ2_MIN + (NOR_FREQ2_MAX - NOR_FREQ2_MIN) * pot2_norm;
  logicNorPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Tri OR Saw Parameters
// --------------------------------------------------
#define TRI_OR_FREQ1_MIN 4.5f
#define TRI_OR_FREQ1_MAX 1024.0f
#define TRI_OR_FREQ2_MIN 2.0f
#define TRI_OR_FREQ2_MAX 1024.0f

void updateTriOrSawParams()
{
  // pot1 -> triangle frequency
  float freq1 = TRI_OR_FREQ1_MIN + 
                (TRI_OR_FREQ1_MAX - TRI_OR_FREQ1_MIN) * pot1_norm;
  logicTriOrPhaseInc1 = (uint32_t)(freq1 * 268435.456f);
  
  // pot2 -> sawtooth frequency
  float freq2 = TRI_OR_FREQ2_MIN + 
                (TRI_OR_FREQ2_MAX - TRI_OR_FREQ2_MIN) * pot2_norm;
  logicTriOrPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Tri NOR Tri Parameters
// --------------------------------------------------
#define TRI_NOR_FREQ1_MIN 4.0f
#define TRI_NOR_FREQ1_MAX 880.0f
#define TRI_NOR_FREQ2_MIN 15.0f
#define TRI_NOR_FREQ2_MAX 900.0f

void updateTriNorTriParams()
{
  // pot1 -> triangle 1 frequency
  float freq1 = TRI_NOR_FREQ1_MIN + 
                (TRI_NOR_FREQ1_MAX - TRI_NOR_FREQ1_MIN) * pot1_norm;
  logicTriNorPhaseInc1 = (uint32_t)(freq1 * 268435.456f);
  
  // pot2 -> triangle 2 frequency
  float freq2 = TRI_NOR_FREQ2_MIN + 
                (TRI_NOR_FREQ2_MAX - TRI_NOR_FREQ2_MIN) * pot2_norm;
  logicTriNorPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Tri XOR Tri Parameters
// --------------------------------------------------
#define TRI_XOR_FREQ1_MIN 0.7f
#define TRI_XOR_FREQ1_MAX 220.0f
#define TRI_XOR_FREQ2_MIN 0.6f
#define TRI_XOR_FREQ2_MAX 440.0f

void updateTriXorTriParams()
{
  // pot1 -> triangle 1 frequency
  float freq1 = TRI_XOR_FREQ1_MIN + 
                (TRI_XOR_FREQ1_MAX - TRI_XOR_FREQ1_MIN) * pot1_norm;
  logicTriXorPhaseInc1 = (uint32_t)(freq1 * 268435.456f);
  
  // pot2 -> triangle 2 frequency
  float freq2 = TRI_XOR_FREQ2_MIN + 
                (TRI_XOR_FREQ2_MAX - TRI_XOR_FREQ2_MIN) * pot2_norm;
  logicTriXorPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Square XNOR Square Parameters
// --------------------------------------------------
#define XNOR_FREQ1_MIN 0.5f
#define XNOR_FREQ1_MAX 440.0f
#define XNOR_FREQ2_MIN 0.6f
#define XNOR_FREQ2_MAX 150.0f

void updateSquareXnorSquareParams()
{
  // pot1 -> square 1 frequency
  float freq1 = XNOR_FREQ1_MIN + (XNOR_FREQ1_MAX - XNOR_FREQ1_MIN) * pot1_norm;
  logicXnorPhaseInc1 = (uint32_t)(freq1 * 268435.456f);
  
  // pot2 -> square 2 frequency
  float freq2 = XNOR_FREQ2_MIN + (XNOR_FREQ2_MAX - XNOR_FREQ2_MIN) * pot2_norm;
  logicXnorPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Square NAND Square Parameters
// --------------------------------------------------
#define NAND_FREQ1_MIN 0.1f
#define NAND_FREQ1_MAX 50.0f
#define NAND_FREQ2_MIN 0.08f
#define NAND_FREQ2_MAX 45.0f

void updateSquareNandSquareParams()
{
  // pot1 -> square 1 frequency (extremely low to low)
  float freq1 = NAND_FREQ1_MIN + (NAND_FREQ1_MAX - NAND_FREQ1_MIN) * pot1_norm;
  logicNandPhaseInc1 = (uint32_t)(freq1 * 268435.456f);
  
  // pot2 -> square 2 frequency (extremely low to low)
  float freq2 = NAND_FREQ2_MIN + (NAND_FREQ2_MAX - NAND_FREQ2_MIN) * pot2_norm;
  logicNandPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Noise NOR Noise Parameters
// --------------------------------------------------
#define NOISE_NOR_FREQ_MIN 5.0f
#define NOISE_NOR_FREQ_MAX 120.0f

void updateNoiseNorNoiseParams()
{
  // pot1 -> S&H rate 1 (5-120 Hz, converted to sample period)
  float freq1 = NOISE_NOR_FREQ_MIN + (NOISE_NOR_FREQ_MAX - NOISE_NOR_FREQ_MIN) * pot1_norm;
  noiseNorPeriod1 = (uint32_t)(SAMPLE_RATE / freq1);
  
  // pot2 -> S&H rate 2 (5-120 Hz, converted to sample period)
  float freq2 = NOISE_NOR_FREQ_MIN + (NOISE_NOR_FREQ_MAX - NOISE_NOR_FREQ_MIN) * pot2_norm;
  noiseNorPeriod2 = (uint32_t)(SAMPLE_RATE / freq2);
}

// --------------------------------------------------
// Square OR Square Parameters
// --------------------------------------------------
#define SQUARE_OR_FREQ_MIN 15.0f
#define SQUARE_OR_FREQ_MAX 850.0f

void updateSquareOrSquareParams()
{
  // pot1 -> square 1 frequency
  float freq1 = SQUARE_OR_FREQ_MIN + (SQUARE_OR_FREQ_MAX - SQUARE_OR_FREQ_MIN) * pot1_norm;
  logicSquareOrPhaseInc1 = (uint32_t)(freq1 * 268435.456f);
  
  // pot2 -> square 2 frequency
  float freq2 = SQUARE_OR_FREQ_MIN + (SQUARE_OR_FREQ_MAX - SQUARE_OR_FREQ_MIN) * pot2_norm;
  logicSquareOrPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Master Parameter Update (calls all individual updates)
// --------------------------------------------------
void updateAllParams()
{
  updateLatchedNoiseParams();
  updateDustParams();
  updateDustBurstParams();
  updateHighpassNoiseParams();
  updateHighNoiseRhythmParams();
  updateVinylCrackleParams();
  updateFMnoiseParams();
  updateNoisegatesParams();
  updateGenerativeWaveform1Params();
  updateGenerativeWaveform2Params();
  updateGenerativeWaveform3Params();
  updateGenerativeWaveform4Params();
  updateGenerativeWaveform5Params();
  updateGenerativeWaveform6Params();
  updateGenerativeWaveform7Params();
  updateGenerativeWaveform17Params();
  updateGenerativeWaveform18Params();
  updateGenerativeWaveform8Params();
  updateGenerativeWaveform9Params();
  updateGenerativeWaveform10Params();
  updateGenerativeWaveform11Params();
  updateGenerativeWaveform12Params();
  updateGenerativeWaveform13Params();
  updateGenerativeWaveform14Params();
  updateGenerativeWaveform15Params();
  updateGenerativeWaveform16Params();
  updateRandomTriangleParams();
  updateSawClicksParams();
  updateFastTriangleParams();
  updateHarmonicTrisParams();
  updatePhrygianTriParams();
  updateRingModParams();
  updateTwoSawsParams();
  updateNoiseOrSquareParams();
  updateMajorTrisParams();
  updateBernoulliTrisParams();
  updatePentatonicBlipsParams();
  updateThreeCascadedSquaresParams();
  updateNorSquareParams();
  updateTriOrSawParams();
  updateTriNorTriParams();
  updateTriXorTriParams();
  updateSquareXnorSquareParams();
  updateSquareNandSquareParams();
  updateNoiseNorNoiseParams();
  updateSquareOrSquareParams();
  
  // Future: Add new algorithm param updates here
}

#endif // PARAMS_H
