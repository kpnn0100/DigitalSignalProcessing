# base/ — Core abstractions (channel-aware)

The foundation layer. Everything else inherits from here. This README documents both the
**existing** pieces and the **planned changes** for ESP32DigitalSynth (marked 🆕 / ✏️).

> Status: DESIGN. ✏️ = existing class to be modified, 🆕 = new class.

---

## Existing pieces (kept)

- **`SignalProcessor`** — abstract per-sample processor. Property system with smoothed
  interpolation over a buffer (`enum PropertyIndex{…,propertyCount}`, `initProperty`,
  `setProperty`, `getProperty`, `update()`).
- **`Block`** — composite of processors, serial or parallel (parallel can average).
- **`FeedbackBlock`** — forward + feedback processor with feedback gain.
- **`CircularList<T>`** — ring buffer used by delay lines.
- **`functional/ProcessorRepeater<T>`** — chains N copies of a processor (used for unison
  and echo repeats).

---

## ✏️ Change 1 — Channel-aware `SignalProcessor`

**Why:** the synth needs 2 channels, and within one block each channel may need a
*different* chain (e.g. different detune/delay on L vs R). Duplicating whole chains by hand
violates DRY and can't share parameter smoothing; widening the base is the SOLID move.

**New contract:**

```cpp
// pure virtual core — now takes the channel index
virtual double process(double in, int channel) = 0;

// public entry — applies smoothing, then dispatches to the channel
double out(double in, int channel);
```

**State rules:**

- **Parameters/targets are shared across channels.** A cutoff, a delay time, a gain — one
  value, smoothed once. (Smoothing/buffer-counter logic stays channel-independent.)
- **Running state is per channel.** Filter memory, delay lines, oscillator phase, envelope
  position become arrays indexed by channel, sized to `AudioConfig::channelCount()` and
  resized in `onChannelCountChanged()`.
- Migration: every existing override `process(double in)` becomes
  `process(double in, int channel)`. A convenience `out(double in)` may default to
  channel 0 so simple/legacy callers and single-channel unit tests stay terse.

## ✏️ Change 2 — `Block` holds a chain per channel

```cpp
void add(SignalProcessor* p, int channel); // append to ONE channel's chain
void add(SignalProcessor* p);              // append to ALL channels' chains
double process(double in, int channel) override; // runs THAT channel's chain
```

So a single `Block` can route L through `[oscA → lpf]` and R through `[oscB → delay]`.
Serial/parallel mode and the delay-sync machine apply per channel.

## 🆕 `AudioConfig` — global config singleton

Single source of truth for global audio settings. Meyers singleton:

```cpp
class AudioConfig {
public:
    static AudioConfig& instance();
    double sampleRate() const;            // default 48000.0
    int    bufferSize() const;            // default 128
    int    channelCount() const;          // default 2
    int    outputBitDepth() const;        // default 16  (see output/)
    void   setSampleRate(double sr);      // notifies all processors
    void   setChannelCount(int n);        // notifies all processors
    // ...
private:
    AudioConfig() = default;
};
```

- `SignalProcessor`'s current `static mSampleRate` / `mBufferSize` become **views fed from
  `AudioConfig`** — exactly one authority. No other class stores its own copy of the sample
  rate or channel count.
- Changing sample rate / channel count routes through `AudioConfig`, which fans out
  `onSampleRateChanged()` / `onChannelCountChanged()` to every live processor.

## 🆕 `SignalGenerator : SignalProcessor`

Base for things that **produce** signal rather than transform input. A generator is a
*voiceable sound source*: it owns its own **ADSR amplitude envelope** — the envelope is part
of the generator, **not** a separate effect-chain stage.

```cpp
class SignalGenerator : public SignalProcessor {
public:
    void   setFrequency(double hz);   // Hz → per-sample phase increment via AudioConfig
    // voicing — the envelope lives here, on the source
    void   noteOn(double velocity);   // triggers the owned envelope (Attack), scales peak
    void   noteOff();                 // envelope -> Release
    bool   isFinished() const;        // envelope reached 0 after release
    void   setAttackMs(double ms);    // forwards to the owned ADSREnvelope
    void   setDecayMs(double ms);
    void   setSustain(double level);
    void   setReleaseMs(double ms);
protected:
    ADSREnvelope mEnvelope;           // envelope/ — owned, applied to the generated signal
    virtual double generate(int channel) = 0;          // subclass: raw waveform sample
    double process(double in, int channel) override;   // = generate(ch) * mEnvelope value
    // per-channel phase[channel], advanced by sample count each call
    // waveform left open for subclasses (Open/Closed) — see generator/
};
```

- The base's `process()` calls the subclass `generate(channel)` for the raw waveform, then
  multiplies by the owned envelope's per-sample value — so **every generator is enveloped by
  construction**.
- **Per-channel phase** is what lets one generator spread across L/R (stereo width, per-side
  detune) without a second instance.
- Phase and envelope both advance by **sample count** only — never seconds.
- `Oscillator` (in [`generator/`](../generator/README.md)) inherits this and implements
  `generate()`.

---

## Class relationships (after changes)

```
              SignalProcessor (channel-aware: process(in, ch))
              /        |          \              \
          Block   FeedbackBlock   SignalGenerator  …existing modules
        (per-ch        |               |
         chains)       |           Oscillator (generator/)
                       |
                 (reverb, etc.)
```

## Concurrency model (multithreading-friendly, platform-independent)

The library must run with **one audio thread** (render) and a **separate control thread**
(UART/comms) on ESP32, Linux, and Windows — with only a *small platform adapter*. Design:

- **The audio thread never locks.** Control changes (param sets, note on/off) are pushed
  onto a **lock-free single-producer/single-consumer queue** (`base/LockFreeQueue.h`,
  `std::atomic` only — portable to Xtensa/x86/x64). The audio thread **drains the queue at
  each block boundary** and applies the commands, so synthesis stays wait-free.
- **No shared mutable global touched in the audio loop.** Graph construction (registering
  processors, building blocks) happens **before** the audio thread starts — a documented
  contract. `AudioConfig` runtime changes (sample rate, channel count) go through the same
  command queue and are applied on the audio thread.
- **`base/platform/` is the only platform-specific code** — a thin adapter:
  - `Thread` — wraps `std::thread` (Linux/Windows) or `xTaskCreatePinnedToCore` (ESP32),
    selected by `#if defined(ESP_PLATFORM)`.
  - `Mutex` / `ScopedLock` — `std::mutex` or FreeRTOS mutex. Used only for **setup/non-RT**
    paths, never in `process()`.
  The DSP core (`SignalProcessor`, modules) includes **none** of this — it's pure
  computation and therefore inherently portable. Apps (harness/firmware) use the adapter to
  spawn threads; the library stays platform-agnostic.

So: the core is platform-independent by containing no OS calls; multithreading is enabled by
the lock-free queue + a ~100-line adapter per platform.

## SOLID notes

- **SRP:** `AudioConfig` owns global settings; `SignalProcessor` owns per-sample dispatch +
  smoothing; generators own synthesis.
- **OCP:** new waveforms/effects are new subclasses; the base contract doesn't change.
- **LSP:** every processor honors `out(in, channel)` identically.
- **DIP:** engine/effects depend on `SignalProcessor` / `SignalGenerator`, not concretes.

## Migration status

Migrated to the channel-aware base and **verified** (compiles + unit-tested):
`SignalProcessor`, `Block`, `FeedbackBlock`, `Delay`, `Gain`, `LowPassFilter`,
`HighPassFilter`, `ProcessorRepeater`, `Reverb` (now per-channel + dry/wet mix), plus the
new `SignalGenerator`, `Oscillator`, `ADSREnvelope`, effects, and synth engine.

**Not yet migrated:** the `spatial/` modules (`Positioner`, `RoomSimulator`,
`PositionSimulator`) still use the old mono `process(double in)` signature and are excluded
from the synth build (`synth_dsp.h` instead of the legacy `gyrus_space_dsp.h`). They need
the same mechanical signature change before the full legacy aggregate compiles again.

## Build / test

- Headers registered in `../gyrus_space_dsp.h`.
- Unit tests under `../unittest/` via `buildAndRunUT.sh` (gtest). The channel refactor adds
  tests asserting per-channel state independence and shared-parameter behavior.
