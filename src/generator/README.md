# generator/ — Sound sources

Oscillators and the unison voice stack. All inherit the new
[`SignalGenerator`](../base/README.md) base.

> Status: DESIGN. 🆕 = new class.

---

## 🆕 `Oscillator : SignalGenerator`

A single waveform oscillator. Saw is required now; the design keeps waveform selection
open (Open/Closed) so sine/square/triangle can be added later without touching the base.

```cpp
enum PropertyIndex {
    frequencyID,   // stored as per-sample phase increment (Hz → samples at the setter)
    phaseOffsetID, // 0..1 turns, used for per-channel spread
    propertyCount
};

void setFrequency(double hz);     // inherited semantics
void setWaveform(Waveform w);     // SAW initially
double process(double in, int channel) override;
```

- **Per-channel phase** (from `SignalGenerator`): channel 1 can carry a small phase/freq
  offset from channel 0 → stereo width with one instance.
- **Saw** generated as a phase ramp; **PolyBLEP** anti-aliasing is the intended approach so
  the feasibility test reflects real (band-limited) cost, not a cheap aliased ramp.
  *(Open question for review — see bottom.)*
- Frequency held as a **phase increment** (sample domain); recomputed on
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
