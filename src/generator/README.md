# generator/ — Sound sources

Oscillators and the unison voice stack. All inherit the new
[`SignalGenerator`](../base/README.md) base.

> Status: DESIGN. 🆕 = new class.

---

## 🆕 `Oscillator : SignalGenerator`

A unison oscillator with **selectable waveform**, chosen with `setWaveform(Waveform)` (or the
`OSC_WAVEFORM` synth parameter — see `synth/ParamId.h`):

| `Waveform` | value | synthesis | aliasing |
|---|---|---|---|
| `Sine`     | 0 | `sin(2π·phase)` | none (single partial) |
| `Saw`      | 1 | phase ramp + **PolyBLEP** | band-limited |
| `Square`   | 2 | ±1 + PolyBLEP at both edges | band-limited |
| `Triangle` | 3 | `(2/π)·asin(sin(2π·phase))` | naive (harmonics fall ~1/n²) |

```cpp
enum Waveform { Sine = 0, Saw = 1, Square = 2, Triangle = 3 };
enum PropertyIndex { voiceCountID, detuneID, spreadID, propertyCount };

void   setWaveform(Waveform w);   // default Saw
void   setFrequency(Sample hz);
Sample generate(int channel) override;
```

- **Default `Saw`.** An unknown waveform value produces silence (`0`).
- The waveform applies to **every unison voice**; the per-voice detune/spread is unchanged.
- **Saw / Square** are band-limited with **PolyBLEP**; **Sine** is exact; **Triangle** is the
  naive `asin(sin)` shape (its harmonics roll off ~1/n², so audible aliasing is minimal).
- The fast fixed-point **SIMD block path** renders `Saw` only; any other waveform falls back
  to the scalar per-sample `generate()` path so the shape is honoured.
- **Per-channel phase** (from `SignalGenerator`): a per-channel cents offset gives stereo
  width from one instance. Frequency is held as a phase increment, recomputed on
  `onSampleRateChanged()`.

## 🆕 `UnisonOscillator` (voice stack) — *reuse first*

Up to **5 detuned saw voices** summed into one oscillator output, per channel.

**Reuse decision:** evaluate `functional/ProcessorRepeater<Oscillator>` first — it already
chains N copies. But unison needs the copies in **parallel with per-voice detune**, not in
series, so the likely shape is a thin class that **owns N `Oscillator`s** (or composes a
parallel `Block`) and spreads detune across them. The README will record the final reuse
decision when implemented.

```cpp
enum PropertyIndex {
    frequencyID,
    voiceCountID,   // 1..5
    detuneID,       // total spread in cents; distributed symmetrically across voices
    stereoSpreadID, // how much detune/phase differs between channels
    propertyCount
};
```

- Voices detuned symmetrically around the base note (e.g. ±detune/2 in cents → per-voice
  phase increments).
- Output normalized by voice count to avoid clipping as voices are added.
- `stereoSpread` widens per-channel detune/phase → classic supersaw width.

## How the 3-oscillator layer uses these

The synth `Voice` (see [`synth/`](../synth/README.md)) holds **3** `UnisonOscillator`s
summed. Each can have its own waveform/detune/level. That sum feeds the ADSR.

```
Voice (per note):
   UnisonOsc 1 ┐
   UnisonOsc 2 ┼─ sum ─▶ ADSR ─▶ (effects, shared)
   UnisonOsc 3 ┘
```

## Reuse map

| Need | Reuse |
|---|---|
| Generator base, per-channel phase | 🆕 `SignalGenerator` (base/) |
| N copies of a processor | `functional/ProcessorRepeater` (evaluate) |
| Parallel sum | `base/Block` (parallel mode) |

## Open questions for review

1. **Anti-aliasing:** PolyBLEP saw (realistic CPU cost, recommended for a *feasibility*
   test) vs. naive ramp (cheapest, but aliases and understates real load). Which do you
   want for the S3 benchmark?
2. **Unison shape:** dedicated `UnisonOscillator` class vs. a parallel `Block` of
   `Oscillator`s assembled by the `Voice`. (Leaning: dedicated class for a clean public
   parameter surface.)
