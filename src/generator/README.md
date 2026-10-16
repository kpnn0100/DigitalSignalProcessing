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
- **Starting phases (REQ-synth2-6):** voice v of the stack starts at φ_v = frac(v·0.6180339887…) (voice 0 at 0),
  the same on every channel. All at zero, a detuned unison's voices are aligned at the attack — a spike of N
  saws (measured 7.8× the sustained RMS for five) — before they drift apart; spread, the sum starts as it goes
  on (0.9×). Set when the per-channel state is sized (`ensureChannels`); free-running after. Width (`spreadID`)
  is a per-channel detune, c_off = (c − (C−1)/2)·spread cents, so phases equal per channel keep width 0 mono.
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

---

## Noise (REQ-noise-1) — `generator/Noise.h`

Seeded white noise for the drum voices and the synth's noise source. A **primitive**, not a
`SignalGenerator`: a value type with `next()`, like a phase accumulator, so a voice owns one without
inheriting an ADSR it would have to neutralise. Not composed from existing modules because none
produces a random sequence.

### Math

```
(N1)  x ← x ⊕ (x ≪ 13);  x ← x ⊕ (x ≫ 17);  x ← x ⊕ (x ≪ 5)     ; xorshift32 (Marsaglia), period 2³² − 1
(N2)  out = (x ≫ 8) · 2/2²⁴ − 1                                  ; top 24 bits → uniform on [−1, 1)
```

| parameter | symbol | |
|---|---|---|
| `setSeed(s)` | x₀ | `0` is replaced by `0x9E3779B9` — a zero state would stay zero forever |

Uniform on [−1, 1): mean 0, RMS 1/√3 ≈ 0.577 (asserted to ±0.005 over 200 000 samples in
`Noise_is_deterministic_uniform_and_bounded`). **The same seed gives the same samples on every
run and machine** — there is no clock and no shared generator, which is what lets a render that
contains a snare be a pure function of its project.

---

## Phasor (REQ-drum-1) — `generator/Phasor.h`

A bare phase accumulator, the primitive under the drum voices. Not `Oscillator`, because a drum's
pitch moves every sample and `Oscillator::setFrequency` recomputes every unison voice's increment on
every channel with a `pow` each; a phasor takes its increment from the caller.

### Math

```
(P1)  φ[n+1] = φ[n] + inc − ⌊φ[n] + inc⌋,   inc = f/fs          ; wrapped to [0,1) for any inc, negative too
      sine   = sin(2π·φ)      square = (φ < ½) ? +1 : −1        ; NOT band-limited (see instrument/README.md)
```

## Wavetable (REQ-wavetable-1, REQ-wavetable-2) — `generator/Wavetable.h`

A sound made of FRAMES — single cycles, 2048 samples each — read at any pitch with nothing above Nyquist, and moved
through by a POSITION. Played as one more waveform of `Oscillator` (`Table`), so unison, detune, stereo width and
every BasicSynth voice feature come with it (Solaris R-INST-7).

**Why not composed (rule 1):** nothing in the library holds a cycle at several bandwidths or reads one between its
samples. What it IS composed of: the library's `FFT` (REQ-fft-1) band-limits every frame; the oscillator's phase
accumulator, unison and spread carry it; BasicSynth's voices, envelopes, filter and expression play it.

### Math

```
(W1) Levels — Wavetable::Wavetable, from frames x_f[n], n < 2048
     X_f = FFT(x_f);  level l keeps harmonics 1 … H_l,  H_l = 256 / 2^l,  l = 0 … 8  (H_8 = 1: a sine)
     its table: N_l = max(256, 16·H_l) points  =  IFFT_{N_l}(X_f[1 … H_l], mirrored)   — the DC dropped
     (16 points a harmonic at worst: at 8 a hard-synced saw's reading images reached −44 dB at 1.8 kHz — measured;
     at 16, under −76 dB everywhere; 8 960 points a frame, 1.1 MB a 32-frame table)
     each frame scaled so its level-0 table peaks at 1 (the same gain on every level: a frame's levels agree)
(W2) Which levels — Wavetable::read, dt = the phase step of the oscillator's highest voice (f/fs)
     λ = log2(2·H_0·dt)       (λ = l exactly when level l's top harmonic reaches Nyquist)
     λ ≤ −1: level 0 alone;  otherwise, k = ⌈λ⌉ (so λ ∈ (k−1, k]):  y = (k − λ)·L_k + (λ − k + 1)·L_{k+1}
     both levels mixed are below Nyquist over the whole span (L_k is, for λ ≤ k) — a crossfade that never aliases;
     λ ≥ 8: level 8 (a sine)
(W3) Between samples — Catmull-Rom (4-point Hermite) on the circular table at x = φ·N_l, i = ⌊x⌋, t = x − i:
     y = y_i + ½t(y_{i+1} − y_{i−1}) + t²(y_{i−1} − 2.5y_i + 2y_{i+1} − ½y_{i+2}) + ½t³(3(y_i − y_{i+1}) + y_{i+2} − y_{i−1})
     16 points a harmonic at worst: its images lie far down (measured: Wavetable_reads_with_nothing_above_nyquist)
(W4) Position — p ∈ [0, 1]:  q = p·(F − 1),  f = ⌊q⌋,  w = q − f;   y = (1 − w)·y_f + w·y_{f+1}   (F frames)
(W5) Built-ins — Wavetable::builtIn, 32 frames each, from formulas (deterministic, made once, shared, immutable):
     Basic     sine → triangle → saw → square, harmonic amplitudes interpolated across three spans
     Pulse     a pulse from 50 % to 4 % wide
     Harmonics harmonics 1 … j+1 at 1/h (frame j): a saw growing from a sine
     Formant   1/h under two formant peaks sweeping a → e → i → o → u (a reference fundamental of 130 Hz)
     Sync      a saw hard-synced at 1 … 8 times the cycle
     Fold      sin(g·sin 2πx), g from 1 to 12
     each formula sampled at 2048 points a cycle, then band-limited by (W1) like any frame
(W6) From a sound — Wavetable::fromSample: a mono sound cut into ⌊frames / 2048⌋ cycles (at most 256); shorter than
     one cycle, it is stretched to one (linear)
(W7) In the oscillator — Oscillator::setPosition(p, ramp): the position moves to p over `ramp` samples, a straight
     line per channel — a moved position never steps (a zipper)
```

| parameter | symbol | unit / range | used in |
|---|---|---|---|
| frame size | — | 2048 points | (W1) |
| `kMaxHarmonics` | H_0 | 256 | (W1), (W2) |
| `kLevels` | — | 9 | (W1) |
| `read` `phase`, `position`, `dt` | φ, p, dt | 0…1, 0…1, f/fs | (W2)–(W4) |
| `Oscillator::setPosition` `ramp` | — | samples | (W7) |

**Limits, said:** 256 harmonics at most — below about 94 Hz a table's top lies under Nyquist (a 55 Hz note reaches
14 kHz); between octaves the crossfade keeps the content between half and all of what Nyquist allows. A position
moved by a BasicSynth envelope is set every 32 samples (its control grid) and ramped between.
