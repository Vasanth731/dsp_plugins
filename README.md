# Pedalboard DSP — Audio Plugins in C++ / JUCE

A collection of guitar-pedal–style audio plugins written in modern C++ with the [JUCE](https://juce.com) framework. Each plugin is a self-contained processor with its own custom UI, and together they show the core building blocks of real-time digital signal processing: **modulated delay lines, dynamics processing, filtering/EQ and nonlinear waveshaping**.

| Plugin | Type | Core DSP |
|---|---|---|
| **Velvet** | Chorus / Vibrato | LFO-modulated fractional delay line, feedback, stereo width |
| **Squeeze** | Compressor | Envelope follower, soft-knee gain computer, attack/release ballistics, parallel blend |
| **Clean** | Clean Boost | Gain staging, 3-band EQ, tone filter, soft-clipping drive, voicings |

<!-- Add screenshots here, e.g.
![Velvet](docs/images/velvet.png)
![Squeeze](docs/images/squeeze.png)
![Clean](docs/images/clean.png)
-->

---

## Velvet — Chorus

A modulated-delay effect with selectable **Type** (e.g. *Chorus*, *Vibrato*).

### Controls

| Parameter | Default | Description |
|---|---|---|
| Type | Vibrato | Selects the effect mode (chorus = dry + wet, vibrato = wet only pitch modulation) |
| Rate | 0.70 Hz | LFO frequency |
| Depth | 35.0 % | LFO modulation depth (delay-time sweep) |
| Delay | 12.00 ms | Base delay time around which the LFO sweeps |
| Feedback | 0.0 % | Amount of delayed signal fed back into the delay line (flanger-like resonance) |
| Width | 60.0 % | Stereo spread via LFO phase offset between channels |
| Tone | 9000 Hz | Low-pass cutoff on the wet signal |
| Mix | 50.0 % | Dry / wet balance |
| Level | 0.00 dB | Output gain |

### DSP Approach
- **Circular delay buffer** per channel, sized for the maximum delay + modulation depth.
- **Fractional delay with interpolation** — the LFO moves the read head between samples, so the buffer is read with interpolation (linear / cubic / all-pass) to avoid zipper noise and artefacts.
- **LFO** generates the modulation signal; the right channel is phase-offset by an amount set by **Width** to create a wide stereo image.
- **Feedback path** is gain-limited to keep the loop stable.
- **Tone** is a low-pass filter on the wet path to tame high-frequency modulation artefacts and give a warmer, "analog BBD"-style sound.
- **Vibrato mode** outputs only the modulated signal, producing pure pitch modulation (Doppler effect) instead of the chorus "doubling" effect.

---

## Squeeze — Compressor

A feed-forward compressor with a soft knee, tone control and parallel (New York style) blending.

### Controls

| Parameter | Default | Description |
|---|---|---|
| Sustain | 4.00 | Amount of compression / sustain character |
| Threshold | -18.0 dB | Level above which gain reduction begins |
| Ratio | 4.00:1 | Compression ratio above threshold |
| Attack | 15.0 ms | Time for gain reduction to engage |
| Release | 150.0 ms | Time for gain reduction to recover |
| Knee | 6.00 dB | Width of the soft-knee transition region |
| Tone | 5.00 | Brightness control on the processed signal |
| Blend | 100.0 % | Parallel compression — mix of compressed and dry signal |
| Level | 0.00 dB | Output / make-up gain |

### DSP Approach
- **Level detection** — an envelope follower converts the input to a level estimate in the dB domain.
- **Gain computer with soft knee** — static curve of the form:
  - below `T − W/2`: no gain reduction
  - inside the knee: `y = x + (1/R − 1)(x − T + W/2)² / (2W)`
  - above `T + W/2`: `y = T + (x − T) / R`
- **Ballistics** — separate attack and release time constants smooth the gain-reduction signal (one-pole smoothing with coefficients `exp(-1 / (t · fs))`).
- **Parallel compression** — **Blend** mixes the compressed and unprocessed signals, preserving transients while adding sustain.
- **Tone** shapes the frequency balance of the compressed signal with a filter stage.

---

## Clean — Boost

A transparent clean boost with a full EQ section, optional drive and selectable **Voicing**.

### Controls

| Parameter | Default | Description |
|---|---|---|
| Voicing | Clean | Selects the boost character / EQ voicing |
| Boost | 6.00 dB | Gain applied to the signal |
| Tone | 5.00 | Overall brightness (tilt / high-cut) |
| Bass | 5.00 | Low-frequency shelf |
| Mid | 5.00 | Mid-range peaking band |
| Treble | 5.00 | High-frequency shelf |
| Drive | 2.45 | Amount of soft saturation |

### DSP Approach
- **Gain staging** — boost applied in the linear domain from a dB parameter.
- **3-band EQ** built from biquad filters (low shelf, peaking mid, high shelf) using the RBJ Audio EQ Cookbook formulas, with coefficients recalculated only when parameters change.
- **Drive** — a smooth, memoryless waveshaper (e.g. `tanh`-style soft clipping) that adds harmonics progressively as the level rises.
- **Oversampling** around the nonlinear stage to reduce aliasing from the harmonics the waveshaper generates.
- **Voicings** switch between preset filter / gain characteristics for different boost flavours.

---

## Shared Engineering Practices

All three plugins follow the same real-time audio practices:

- **Real-time safety** — no memory allocation, locks or I/O in `processBlock()`; buffers are allocated in `prepareToPlay()`.
- **Parameter management** via `juce::AudioProcessorValueTreeState`, giving host automation and preset save/restore.
- **Parameter smoothing** (`juce::SmoothedValue`) to avoid clicks and zipper noise when knobs move.
- **Sample-rate independence** — all time constants, filter coefficients and delay lengths are derived from the current sample rate.
- **Denormal protection** (`juce::ScopedNoDenormals`) to avoid CPU spikes on decaying signals.
- **True bypass** footswitch with an LED indicator on each pedal.
- **Custom UI** — a `LookAndFeel` subclass draws the rotary knobs, value readouts and pedal enclosure, with a separate colour theme per plugin.

---

## Repository Structure

```
.
├── Velvet/
│   └── Source/
│       ├── PluginProcessor.h / .cpp
│       └── PluginEditor.h / .cpp
├── Squeeze/
│   └── Source/
│       ├── PluginProcessor.h / .cpp
│       └── PluginEditor.h / .cpp
├── Clean/
│   └── Source/
│       ├── PluginProcessor.h / .cpp
│       └── PluginEditor.h / .cpp
└── README.md
```
*(Update to match your actual layout.)*

---

## Building

### Requirements
- JUCE 7 or 8
- A C++17 compiler (Visual Studio 2022, Xcode, or GCC/Clang)
- CMake 3.22+ *(if using the CMake build)*

### With CMake
```bash
git clone <repo-url>
cd <repo>
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### With Projucer
1. Open the `.jucer` file for the plugin in Projucer.
2. Set the path to your JUCE modules.
3. Export to your IDE and build.

Plugin formats: VST3 / AU / Standalone *(depending on your configuration)*.

---

## Skills Demonstrated

- Real-time audio programming in C++
- Delay-line based effects and fractional-delay interpolation
- LFO design and stereo modulation
- Dynamic range compression: level detection, gain computers, knee design and time-constant ballistics
- IIR filter design (biquads, shelving and peaking EQ)
- Nonlinear processing and anti-aliasing via oversampling
- Plugin architecture, parameter automation and state management with JUCE
- Custom GUI design and component rendering

## License
*Add your license here (e.g. MIT).*
