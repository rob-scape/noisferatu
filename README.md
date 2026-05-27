# NOISFERATU / Texture Synth
## Infinite textures and digital mayhem

![noisferatu](https://robertheel.com/noisferatu/Noisferatu.webp) 

# Sections
- [Project Overview](#project-overview)
- [Bank System](#bank-system)
- [Display](#display)
- [Current Algorithm Bank Overview](#current-algorithm-bank-overview)
- [Hardware Setup](#hardware-setup)
- [Pin Mapping](#pin-mapping)
- [Audio Output](#audio-output)
- [Global Effects Chain](#global-effects-chain)
- [File Structure](#file-structure)
- [Timer/ISR Configuration](#timerisr-configuration)
- [Algorithm Development Workflow](#algorithm-development-workflow)
- [Translating from Max/Gen or VCV Rack](#translating-from-maxgen-or-vcv-rack)
- [Debugging Tips](#debugging-tips)
- [Code Style Guidelines](#code-style-guidelines)
- [For AI Assistants](#for-ai-assistants)
- [Licenses](#licenses)


## Project Overview
NOISFERATU is a compact handheld generative texture synthesizer. 
**45 algorithms across 5 banks** produce crackling textures, noisy scapes, evolving drones, harmonic blips and digital chaos. Shaped but never fully controlled by four knobs. 
They generate endlessly evolving textures, tones and rhythmic patterns; designed to surprise, resist repetition and never quite settle.
Dial in a crackling drone, a stuttering melody, or pure digital chaos.

The front and back panel can be flipped.

It uses and abuses creative approaches like generative wavetables, BitBend address manipulation, probability gates, bitwise logic operations and wild combinations of these. 

Noisferatu is a noise and texture synthesis engine for the Seeed Studio XIAO SAMD21 microcontroller.

Controlled by 5 potentiometers and 3 buttons. Emphasis on efficient integer math, cheap DSP operations, and creative generative textures.

## Bank System:
- **Bank 1:** Wavetables (9 algorithms) - Generative buffer-based textures
- **Bank 2:** Noisy Textures (9 algorithms) - Noise-based processing
- **Bank 3:** BitBend Wavetables (9 algorithms) - Buffer playback with binary address manipulation
- **Bank 4:** Blips & Tones (9 algorithms) - Melodic/rhythmic generators
- **Bank 5:** Logic Disorder (9 algorithms) - Bitwise chaos on oscillators

**Total:** 45 algorithms across 5 banks

## Display
The Noisferatu uses a 4 digit 7 segment display with the [TM1637 library](https://github.com/avishorp/TM1637) by avishorp/TM1637 (LGPL-3.0).

By pressing the BANK button for 2 seconds you can toggle between display ON and display OFF. 

Choosing ON the display will always display the current bank and algo.

If you choose OFF the display turns off after 500 milliseconds after you used one of the buttons. 

## Current Algorithm Bank Overview

### Bank 1: Wavetables (9 algorithms)
Generative buffer-based textures using a shared 4000-sample wavetable. All variants regenerate on algorithm switch. Some auto regenerate during playback. GW17 and GW18 use advanced techniques (harmonic drones and quad bit manipulation) while still living in the wavetable bank.
<br>

| # | Name | Core Technique | pot1 | pot2 |
|---|------|---------------|------|------|
| 1 | Sparse Glitchy | Rare noise chunks (10%), 1 triangle blip (220–7kHz), chunked silence injection | speed | silence prob (max 20%) |
| 2 | Dense Microglitch | Tiny chunks (2/5/21 samples), denser noise (3%), high blip (2–2.2kHz) | speed | silence prob (0–100%) |
| 3 | Spacey Pulses | 33-sample chunks, 25% noise, no blip, chunked silence injection | speed | silence prob (max 10%) |
| 4 | Random Jump Glitch | 16/4/32 chunks, 50% noise, random buffer jump on silence | speed | silence prob |
| 5 | Wandering Window | Noise/silence chunks + blip (600–2.5kHz), window randomly walks ±20 samples | speed | walk rate |
| 6 | Manual Window + Spray | Moderate density + blip (700–3.5kHz), fixed 0.5× speed, ±30 sample spray offset | window pos | window size |
| 7 | Noise or Saw Window | Chunks are 50/50 noise or saw (30–1kHz) + blip, wandering window playback | speed | walk rate |
| 8 | Chord Loop | 30% noise crackles + harmonic triangle pairs (root/2×/3×/4×), partial regen every 3s, pot2 stutter/freeze | octave mult | stutter prob |
| 9 | BitBend Quad | Super-latched sparse noise, 3× big blips (600–900Hz), 4-layer address manipulation (XOR+HOLD+SET_0+SET_1) | speed | bit clock |

### Bank 2: Noisy Textures (9 algorithms)
Noise-based processing, filtering, and rhythmic textures. Slot 9 uses a PROGMEM sample (`sample.h`) — requires `sample.h` to be populated with raw 16-bit PCM data exported at 16kHz mono.
<br>

| # | Name | Core Technique | pot1 | pot2 |
|---|------|---------------|------|------|
| 1 | Latched Noise | S&H noise with probability gate | clock prob | clock rate |
| 2 | Dust | Sparse clicks + 1-pole lowpass filter | lowpass cutoff | density |
| 3 | FMnoise | S&H noise modulates phase increment of sawtooth | base freq | noise S&H rate |
| 4 | Noisegates | Two triangle oscillators gate noise; osc2 has random walk on freq | base freq | walk step |
| 5 | Saw Clicks | Two detuned saws, averaged | saw1 freq | saw2 freq |
| 6 | Noise NOR Noise | 2 S&H noise sources | `~(held1 \| held2) & 0x3FF` | pot1=S&H1 rate, pot2=S&H2 rate |
| 7 | Dust Burst | Dust + Bernoulli-gated noise bursts (15% duty cycle) | burst clock rate | dust density |
| 8 | Highpass Noise | Bit-masked highpass noise + triangle LFO amplitude modulation | AM depth (inverted) | AM rate |
| 9 | Crackles | PROGMEM sample, random window jumps with chunked silence (57/113/331 samples). Speed 0.04–2× for pitch/texture | speed/pitch | window size (200–4000 samples) |

### Bank 3: BitBend Wavetables (9 algorithms)
The BitBend technique: **manipulate the read address before accessing the buffer** is inspired by SoundScaper's "Clock Address Lines (SoundScaper is a great software for ios by Igor Vasiliev). Instead of changing *what* is in the buffer, you change *where* you read from on each sample.
Four modes: SET_0 (force bits to 0 → loops in lower addresses), SET_1 (force bits to 1 → jumps forward), XOR (flip bits → chaos), HOLD (freeze captured bits → locked loops). 
Algorithms combine 1–4 modes on different bit ranges simultaneously, with a clocked Bernoulli gate randomly evolving the bit positions. This means you can pretty much listen to a sound and it will keep evolving and changing, since the clocked Bernoulli gate probabilistically reassigns which bit positions are affected over time.

All 9: pot1 = playback speed, pot2 = bit clock rate
<br>

| # | Name | Buffer Source | Bit Operations |
|---|------|--------------|---------------|
| 1 | BB Chaos | Dense noise+saw chunks | 1× generative, auto-picks mode randomly |
| 2 | BB Sparse | Sparse Glitchy (GW1) | 1× mode, pot2 selects mode directly (4 options) |
| 3 | BB Dual | Spacey Pulses (GW3) | XOR (low bits) + HOLD (high bits), 50/50 Bernoulli gate |
| 4 | BB Freeze | Wandering Window (GW5) | SET_0 (low bits) + HOLD (high bits), fixed 23Hz clock |
| 5 | BB Ping | Custom sparse noise + high-freq tris (3–4kHz) | XOR (low bits) + SET_1 (high bits), 50/50 Bernoulli gate |
| 6 | BB Mirror | Custom noise+saw with freq sweep | SET_0 (low) + SET_1 (high) — conflicting forces lock address to middle range |
| 7 | BB Triple | Custom | XOR (bits 0-2) + HOLD (mid) + SET_0 (bits 6-9), 50/50 Bernoulli gate |
| 8 | BB Sweep | Custom | Dual HOLD at two speeds — slow clock on low bits, fast clock on high bits |
| 9 | BB Triple B | Custom sparse noise + 3× high-freq blips (4–6kHz) | SET_0 (bits 0-1) + HOLD (bits 2-4) + SET_1 (bits 5-9), 50/50 Bernoulli gate |

### Bank 4: Blips & Tones (9 algorithms)
Melodic and rhythmic generators. Enveloped oscillators, scales, ring mod, Bernoulli gates.
<br>

| # | Name | Core Technique | pot1 | pot2 |
|---|------|---------------|------|------|
| 1 | Random Triangle | Enveloped triangle, 50% skip trigger, random freq 250–2.78kHz | trigger rate | env decay |
| 2 | Harmonic Tris | 3 triangles (1×, 3×, 4×) + 3 LFOs with irrational polyrhythm ratios (1, 4:3, √3) | LFO1 rate | LFO2 rate |
| 3 | Fast Triangle | ~960 BPM clock, 75% skip, random bitcrush per trigger (3–12 bits) | freq | decay |
| 4 | Phrygian Tri | Triangle walks 8-note Phrygian scale via random walk, burst-modulated trigger rate, 50ms decay | burst speed | root freq |
| 5 | Ring Mod | Two triangles multiplied `(tri1×tri2)>>9`, inharmonic sidebands | osc1 freq | osc2 freq |
| 6 | Noise OR Square | S&H noise bitwise OR'd with square wave | noise S&H rate | square freq |
| 7 | Major Tris | Fixed 220Hz major chord (root + maj3rd + perf5th), nested LFO AM on each voice | LFO1 rate | LFO2 rate |
| 8 | Bernoulli Minor 7th | 5Hz clock → 50/50 gate → two Bernoulli note gates (220/330 Hz vs 264/396 Hz = minor 7th chord) | gate1 prob | gate2 prob |
| 9 | Pentatonic Blips | Random major pentatonic notes from 220Hz, 50/50 Bernoulli clock gate | clock speed | env decay |

### Bank 5: Logic Disorder (9 algorithms)
Pure bitwise operations on live oscillator values. It's doing what a CMOS logic gate would do — but on 10 bits simultaneously, once per sample, 16000 times per second. Single ISR instruction creates complex harmonic content.
In this bank, pot1 and pot2 each control an oscillator frequency (freq os1, freq os2).
Exception: algo 1 has osc1 fixed, pot1 = osc2 freq, pot2 = osc3 freq.

| # | Name | Waveforms | Operation |
|---|------|-----------|-----------|
| 1 | Three Cascaded Squares | 3 squares | AM chain: sq1 modulates sq2, sq2 modulates sq3 |
| 2 | NOR Square | 2 squares | `~(sq1 \| sq2) & 0x3FF` |
| 3 | Tri OR Saw | Triangle + Saw | `tri \| saw` |
| 4 | Tri NOR Tri | 2 triangles | `~(tri1 \| tri2) & 0x3FF` |
| 5 | Tri XOR Tri | 2 triangles | `tri1 ^ tri2` — complex crackles |
| 6 | Square XNOR | 2 squares | `~(sq1 ^ sq2) & 0x3FF` |
| 7 | Square NAND | 2 squares | `~(sq1 & sq2) & 0x3FF` — extremely low freq drones |
| 8 | Two Saws | Dual saws (moved from Bank 2), averaged | `(saw1 + saw2) >> 1` average two saws |
| 9 | Square OR Square | 2 squares | `sq1 \| sq2` |

| # | Name | Waveforms | Operation | Sound character |
|---|------|-----------|-----------|----------------|
| 1 | Three Cascaded Squares | 3 squares | AM chain: sq1 modulates sq2, sq2 modulates sq3 | Rhythmic gating, stutters. Alarm and distorted mayhem.|
| 2 | NOR Square | 2 squares | `~(sq1 \| sq2) & 0x3FF` | Noisy drones. |
| 3 | Tri OR Saw | Triangle + Saw | `tri \| saw` | Buzzy and dense. High and low drones. |
| 4 | Tri NOR Tri | 2 triangles | `~(tri1 \| tri2) & 0x3FF` | Flickering tones. Chirping madness.|
| 5 | Tri XOR Tri | 2 triangles | `tri1 ^ tri2` | Beating interference patterns. Noise and harmonics.|
| 6 | Square XNOR | 2 squares | `~(sq1 ^ sq2) & 0x3FF` | Noisy beating. |
| 7 | Square NAND | 2 squares | `~(sq1 & sq2) & 0x3FF` | Transmission and static interferences. |
| 8 | Two Saws | Dual saws, averaged | `(saw1 + saw2) >> 1` | Rich sonic scapes.|
| 9 | Square OR Square | 2 squares | `sq1 \| sq2` | Marching toys. |

<sub>[Back to sections list](#sections)</sub>

## Hardware Setup

### Microcontroller
- **Board:** Seeed Studio XIAO SAMD21
- **CPU Clock:** 48 MHz
- **Sample Rate:** 16 kHz
- **DAC Resolution:** 10-bit (0-1023)
- **ADC Resolution:** 12-bit (0-4095)
- **Program Storage Used:** ~50% (room for ~40+ more algorithms!)

### Pin Mapping
```
AUDIO OUTPUT:
A0  (DAC_PIN)         → Audio output (10-bit DAC)

ALGORITHM PARAMETERS:
A1  (PARAM1_PIN)      → Pot 1 (algorithm-specific parameter)
A2  (PARAM2_PIN)      → Pot 2 (algorithm-specific parameter)

GLOBAL EFFECTS:
A3  (BITCRUSH_PIN)    → Global bitcrush (1-10 bits)
A4  (SAMPLE_RATE_PIN) → Sample rate reduction (1x-40x decimation)
A5  (VOLUME_PIN)      → Master volume (logarithmic curve)

BUTTON CONTROLS:
D9  (BUTTON_PREV)     → Previous algorithm - Btn3 AlgoPrev
D6  (BUTTON_ALGO)     → Next algorithm (within current bank) - Btn2 AlgoNext
D7 (BUTTON_BANK)      → Next bank (4 banks: Wavetables, Noisy Textures, Blips & Tones, Logic)  - cycles through banks// Btn1 BankUp 

4 Digit DISPLAY:
D10  (TX)             → TM1637 DIO (7-segment display) DIO
A8  (SCK)             → TM1637 CLK (7-segment display) CLOCK
```

### Audio Output
- **DAC Output:** 10-bit centered at 511
- **Output Range:** 0-1023 (maps to ~0-3.3V)
- **Algorithm Output:** Signed 10-bit (-512 to +511)
- **Signal Chain:** Algorithm → Sample Rate Reduction → Bitcrush → Dither → Volume → DAC
- **Final Output:** `analogWrite(DAC_PIN, DAC_CENTER + volumed)`

### Control Input
- **Potentiometers:** Read as 12-bit (0-4095)
- **Normalized:** Converted to 0.0-1.0 float (`pot1_norm`, `pot2_norm`)
- **Update Rate:** Every loop iteration (~control rate)
- **Volume Curve:** Quadratic (x²) for natural audio taper

### Global Effects Chain
All algorithms pass through global effects before output:
1. **Sample Rate Reduction** (pot A4): 1x to 40x decimation (16kHz down to 400Hz)
2. **Bitcrush** (pot A3): 10-bit down to 1-bit (clean to extreme distortion)
3. **Dither**: Symmetric ±1 dither to reduce quantization noise
4. **Master Volume** (pot A5): Quadratic curve for smooth control

<sub>[Back to sections list](#sections)</sub>

## File Structure

### noiseferatu.ino
**Purpose:** Main orchestrator - setup, loop, ISR, bank/algorithm routing

**Key Components:**
- `setup()` - Initialize ADC/DAC resolution, pins, timer
- `loop()` - Read pots, handle buttons, update parameters
- `TC5_Handler()` - Audio ISR @ 16kHz, calls current algorithm
- 4-bank system with algorithm enums
- Button debouncing (50ms)
- Wavetable regeneration on algorithm switch


### hardware.h
**Purpose:** Physical I/O configuration and constants

**Contains:**
- Pin definitions (DAC, ADC, buttons, display pins)
- Sample rate and resolution constants
- Global hardware state (`pot1_norm`, `pot2_norm`, `masterVolume`, `globalBitcrush`, `sampleRateDecimation`)
- DAC center point (511)
- Volume lookup table (quadratic, stored in PROGMEM)

**Rule:** Only hardware-level definitions. No algorithm-specific constants.

### algos.h
**Purpose:** Pure DSP algorithm implementations

**Contains:**
- Shared PRNG state and functions (`noise1()`, `rand12()`)
- All 45 algorithm implementations
- Algorithm-specific state variables (phases, counters, envelopes, etc.)
- Shared wavetable buffer (4000 samples) and all generation functions
- GW17 Harmonic Drone Builder with partial buffer regen (`partialRegenWaveform17()`)
- GW18 BitBend Quad (4-layer address manipulation, lives in Bank 1)
- BitBend variants (Bank 3): 9 algorithms using binary address manipulation on shared buffer
- Pure math - no hardware knowledge

**Algorithm Return:** Signed 10-bit integer (-512 to +511)

**Organization:** Grouped by bank

### params.h
**Purpose:** Parameter mapping - how pots control each algorithm

**Contains:**
- Algorithm-specific constant ranges (MIN/MAX values)
- `updateXParams()` functions for each algorithm (45 functions!)
- `updateAllParams()` master update function (calls all param functions)

**Rule:** All algorithm parameters are updated every loop, regardless of which algorithm is active. This prevents parameter jumps when switching algorithms.

### sample.h
**Purpose:** PROGMEM sample storage for the Crackles algorithm

**Contains:**
- `vinylCrackle[]` — raw signed 16-bit PCM, 16kHz mono, stored in flash
- `VINYL_CRACKLE_BYTES` — total byte count
- `VINYL_CRACKLE_SAMPLES` — sample count (bytes ÷ 2)

**To update:** export from Audacity as raw signed 16-bit PCM at 16kHz mono, run `xxd -i` in terminal, paste byte values into array. Edit locally.

<sub>[Back to sections list](#sections)</sub>


## Timer/ISR Configuration

### TC5 Timer Setup
- **Timer:** TC5 (16-bit mode)
- **Prescaler:** DIV1 (48 MHz)
- **Frequency:** 48,000,000 / SAMPLE_RATE = 3000 counts
- **Mode:** Match Frequency (MFRQ)
- **Interrupt:** MC0 (Match/Capture 0)

### ISR Execution
```cpp
void TC5_Handler()
{
  // Clear interrupt flag
  TC5->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;
  
  // Get sample from current bank/algorithm
  int16_t sample = 0;
  switch(currentBank) {
    case BANK_WAVETABLES:
      sample = generativeWaveformX();
      break;
    // ... more banks
  }
  
  // Global effects chain
  // 1. Sample rate reduction
  // 2. Bitcrush
  // 3. Dither
  // 4. Volume
  
  analogWrite(DAC_PIN, DAC_CENTER + volumed);
}
```

**Critical:** ISR runs at 16 kHz. Keep it lean - no heavy processing, no delays.



### Optimization Pattern (Core Principle)

```cpp
// In params.h (called once per loop - float OK here):
algorithmPhaseInc = (uint32_t)(frequency * 268435.456f);  // Magic number for 16kHz
algorithmDecayCoeff = 1.0f - expf(-2.0f * PI * cutoff / SAMPLE_RATE);

// In ISR (called 16,000x/sec - integer only!):
phase += phaseInc;  // Auto-wraps at 2^32
envelope *= decayCoeff;  // Simple multiply
output = (phase >> 22) - 512;  // Extract top 10 bits, center to ±512
```

**Key Insight:** Do expensive float math ONCE in updateParams(), convert to integer phase increment, then ISR just does cheap integer adds!

<sub>[Back to sections list](#sections)</sub>

## Algorithm Development Workflow

### Adding a New Algorithm

1. **In algos.h:**
```cpp
// Add state variables (volatile for ISR access)
volatile uint32_t newAlgoPhase = 0;
volatile uint32_t newAlgoPhaseInc = 0;

// Implement algorithm (inline for performance)
inline int16_t newAlgorithm()
{
  // Your DSP code here
  // Use 32-bit phase accumulators
  newAlgoPhase += newAlgoPhaseInc;
  
  // Extract and return signed 10-bit: -512 to +511
  return (newAlgoPhase >> 22) - 512;
}
```

2. **In params.h:**
```cpp
// Add parameter ranges
#define NEW_ALGO_FREQ_MIN 10.0f
#define NEW_ALGO_FREQ_MAX 1000.0f

// Add update function
void updateNewAlgoParams()
{
  // Map pot to frequency
  float freq = NEW_ALGO_FREQ_MIN + 
               (NEW_ALGO_FREQ_MAX - NEW_ALGO_FREQ_MIN) * pot1_norm;
  
  // Calculate phase increment (magic number for 16kHz)
  newAlgoPhaseInc = (uint32_t)(freq * 268435.456f);
}

// Add to updateAllParams()
void updateAllParams()
{
  // ... existing updates ...
  updateNewAlgoParams();  // <-- Add here
}
```

3. **In noiseferatu.ino:**
```cpp
// Add to appropriate bank enum
enum BlipAndTonesAlgos {
  // ... existing algos ...
  BLIP_NEW_ALGORITHM,  // <-- Add here
  BLIP_ALGO_COUNT
};

// Update algo count
const uint8_t algoCountPerBank[BANK_COUNT] = {
  WT_ALGO_COUNT,
  NT_ALGO_COUNT,
  BLIP_ALGO_COUNT,  // Increment this
  LOGIC_ALGO_COUNT
};

// Add to switch statement in TC5_Handler()
case BANK_BLIPS:
  switch(currentAlgo) {
    // ... existing cases ...
    case BLIP_NEW_ALGORITHM:
      sample = newAlgorithm();
      break;
  }
  break;
```

### Translating from Max/Gen or VCV Rack

**Common Patterns:**

| Max/Gen/VCV | C/Arduino (Efficient) |
|-------------|----------------------|
| `phasor~` | `phase += phaseInc; out = phase >> 22;` |
| `noise~` | `noise1()` (10-bit signed) |
| `>~` | `if (signal > threshold)` |
| `*~` | `a * b` then scale: `>> 10` |
| `+~` | `a + b` |
| `sah~` | `if (trigger) value = input;` |
| `slide~` / `slew` | `y += (x - y) * coeff;` (1-pole filter) |
| `triangle~` | See triangle generation pattern above |
| `%~` (modulo) | Phase wraps automatically! |
| Bernoulli gate | `if (rand12() & 1)` for 50/50 |
| Clock divider | `if (++counter >= division) counter = 0;` |
| Ring mod | `(sig1 * sig2) >> 9` |
| VCA / AM | `(carrier * modulator) >> 10` |

**Tips:**
- Max/Gen outputs -1 to +1, convert to -512 to +511
- Phase accumulators wrap at 2^32 automatically - no modulo needed!
- Use `volatile` for variables modified in ISR
- Keep ISR code efficient - complex calculations go in `loop()`
- Magic number: `268435.456f = 2^32 / 16000` for Hz to phase increment

<sub>[Back to sections list](#sections)</sub>


## Debugging Tips

### Common Issues
1. **No sound:** Check buttons/pots connections, verify correct bank/algorithm selected
2. **Distortion:** Check algorithm returns -512 to +511 range, check for integer overflow
3. **Parameter not responding:** Verify `updateXParams()` is called in `updateAllParams()`
4. **Buttons frozen/unresponsive:** Expensive math in ISR (float division, `powf()`, `expf()`)
5. **Clicks/pops:** Check for division by zero, ensure phase wrapping is automatic
6. **Algorithm sounds wrong:** Verify phase increment calculation uses correct magic number
7. **Volume issues:** Check quadratic volume calculation, verify pot wiring (3.3V, not 5V!)

### Serial Debugging (optional)
```cpp
// In setup()
Serial.begin(115200);

// In loop() - NOT in ISR!
Serial.print("Bank: "); Serial.print(currentBank);
Serial.print(" Algo: "); Serial.println(currentAlgo);
Serial.print("Pot1: "); Serial.println(pot1_norm);
```

**Warning:** Serial prints are SLOW. Never use in ISR. Use sparingly even in loop.

### CPU Load
If buttons become unresponsive, you've likely added expensive operations to the ISR.

**Symptoms:**
- Button presses don't register
- Pot changes lag
- Audio glitches/dropouts

**Solution:**
- Move expensive calculations to `updateXParams()` in loop
- Use lookup tables for complex math
- Replace float operations with integer math
- Check for hidden float divisions

## Performance Notes

- **ISR Overhead:** Minimal when following efficient patterns
- **RAM Usage:** Moderate - wavetable buffer (8KB), float state variables
- **Headroom:** ~50% storage remaining for 30+ more algorithms

<sub>[Back to sections list](#sections)</sub>

## Code Style Guidelines

- Use `volatile` for ISR-shared variables
- Use `inline` for algorithm functions (performance hint to compiler)
- Keep parameter ranges as `#define` constants
- Comment algorithm behavior, not syntax
- Name state variables descriptively: `bernTrisEnv1` not `e1`
- Update all params every loop (prevents switching glitches)
- Use `const` for truly constant values
- Mark adjustable values with `// ADJUST:` comments
- Group related algorithms in files with clear section headers

## Magic Numbers Reference

```cpp
// Phase increment for 16kHz sample rate
phaseInc = (uint32_t)(frequencyHz * 268435.456f);
// Where 268435.456 = 2^32 / 16000

// Sample period from frequency
samplePeriod = (uint32_t)(SAMPLE_RATE / frequencyHz);
// For sample & hold timing

// Decay coefficient for envelope
decayCoeff = 1.0f - (3.0f / (decayTimeSeconds * SAMPLE_RATE));
// Approximate -60dB decay time

// 1-pole filter coefficient
alpha = 1.0f - expf(-2.0f * PI * cutoffHz / SAMPLE_RATE);
// Calculate in params, apply in ISR: state += alpha * (input - state)

// Extract top 10 bits from 32-bit phase
output = (phase >> 22);  // 0 to 1023
// Or centered: output = (phase >> 22) - 512;  // -512 to +511

// Bitcrush mask
mask = ~((1 << (10 - numBits)) - 1);
// E.g., 8 bits: ~((1 << 2) - 1) = ~3 = 0xFFFC

// Quadratic pot curve (for volume, slow LFOs)
curved = potValue * potValue;  // Or: pot_norm * pot_norm

// Cubic pot curve (even slower taper)
curved = potValue * potValue * potValue;
```

<sub>[Back to sections list](#sections)</sub>

## Future Expansion Ideas

### Potential New Algorithms
- **More Wavetable Variants:** Different chunk patterns, multiple oscillator types
- **Karplus-Strong:** Plucked string synthesis via (short) delay line + lowpass
- **Granular Synthesis:** Tiny overlapping grains with envelopes — no true granular engine, but short crude buffer playback approximating grain density and texture without the scheduling overhead
- **More Bitwise Combinations:** 16 possible 2-input logic operations
- **Probabilistic Sequencers:** Euclidean rhythms, generative melodies
- **Feedback Loops:** Previous sample as modulation source




## Power consumption
Seeed Studio XIAO SAMD21, 25 mA\
TM1637 Display, 12 mA

Total 37 mA

---


## For AI Assistants

When adding new algorithms:
1. **Always read existing algorithms first** to understand patterns
2. **Follow the three-file pattern:** algos.h → params.h → main.ino
3. **Keep ISR lean** - expensive math in params only
4. **All algorithms return signed 10-bit** (-512 to +511)
5. **Parameters always update** (all of them, every loop)
6. **Use 32-bit phase accumulators** with automatic wrapping
7. **Calculate phase increments in params** using magic number 268435.456f
8. **Test with buttons** - if they freeze, you probably have expensive ISR code
9. **Comment adjustable constants** with `// ADJUST:`
10. **Embrace cheap chaos** - bitwise ops, Bernoulli gates, S&H

The synth thrives on **generative unpredictability** and **efficient bit-twiddling**.\
Avoid complex math - embrace the limitations!
<br/>
<br/>

## Licenses
Code → GPL v3\
Hardware →  CC BY-SA 4.0\
Name and logo → All rights reserved 

<sub>[Back to sections list](#sections)</sub>