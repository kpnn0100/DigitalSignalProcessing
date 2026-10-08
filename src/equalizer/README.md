# equalizer/ — filters

| class | what | requirement |
|---|---|---|
| `LowPassFilter`, `HighPassFilter` | one-pole smoothing filters (the existing ones; tone controls inside effects) | — |
| `Biquad` | one RBJ second-order section: LP, HP, BP, notch, peak, low/high shelf | REQ-eq-1 |
| `ParametricEQ` | seven switchable Biquad bands in series | REQ-eq-2 |
| `StateVariableFilter` | resonant TPT 2-pole: LP/BP/HP/notch from one state, sweepable per sample | REQ-svf-1, REQ-svf-2 |

**Why both a Biquad and an SVF.** The Biquad is the right primitive for a static EQ band: every
cookbook shape (shelves and peaks included) from five coefficients. It is the wrong primitive for a
synth filter swept by an envelope every sample: direct forms misbehave when coefficients move fast,
and a Biquad gives one response per state. The SVF is the opposite — three responses from one state
and stable under per-sample modulation — but has no shelf or peak. Neither composes the other.

## Math

### Biquad (REQ-eq-1) — `Biquad::design`, `Biquad::magnitudeDb`, `Biquad::process`

RBJ "Audio EQ Cookbook" (R. Bristow-Johnson). Inputs: `f0` (Hz, clamped to [10, 0.49·fs]), `Q`
(clamped to [0.1, 40]), `gain` (dB), `fs`.

```
(B1)  w0    = 2π·f0/fs        cw = cos w0        sw = sin w0
(B2)  α     = sw / (2Q)
(B3)  A     = 10^(gain/40)                      ; amplitude, so a peak of `gain` dB is A² in power

(B4)  LowPass    b = [(1−cw)/2, 1−cw, (1−cw)/2]              a = [1+α, −2cw, 1−α]
      HighPass   b = [(1+cw)/2, −(1+cw), (1+cw)/2]           a = [1+α, −2cw, 1−α]
(B5)  BandPass   b = [α, 0, −α]                               a = [1+α, −2cw, 1−α]   ; 0 dB at f0
      Notch      b = [1, −2cw, 1]                             a = [1+α, −2cw, 1−α]
(B6)  Peak       b = [1+αA, −2cw, 1−αA]                       a = [1+α/A, −2cw, 1−α/A]
(B7)  LowShelf   b = [A((A+1)−(A−1)cw+2√Aα), 2A((A−1)−(A+1)cw), A((A+1)−(A−1)cw−2√Aα)]
                 a = [(A+1)+(A−1)cw+2√Aα, −2((A−1)+(A+1)cw), (A+1)+(A−1)cw−2√Aα]
      HighShelf  b = [A((A+1)+(A−1)cw+2√Aα), −2A((A−1)+(A+1)cw), A((A+1)+(A−1)cw−2√Aα)]
                 a = [(A+1)−(A−1)cw+2√Aα, 2((A−1)−(A+1)cw), (A+1)−(A−1)cw−2√Aα]
(B8)  normalise: every coefficient ÷ a0, so a0 = 1 in what is stored

(B9)  |H(e^{jω})|² = |b0 + b1 e^{−jω} + b2 e^{−2jω}|² / |1 + a1 e^{−jω} + a2 e^{−2jω}|²,  ω = 2π f/fs

(B10) transposed direct form II, per channel:
        y  = b0·x + z1
        z1 = b1·x − a1·y + z2
        z2 = b2·x − a2·y
```

The shelves use `Q` directly in (B2) (Q = 0.7071 is the cookbook's shelf slope S = 1). With
`gain = 0` dB, A = 1 makes (B6) and (B7) the identity (b = a) — which is why a fresh EQ is
transparent. `frequency`, `Q` and `gain` are smoothed properties, so `update()` re-runs (B1)–(B8)
on each sample of a ramp; a frequency step therefore moves in `bufferSize` samples instead of
clicking (measured: worst sample step 0.034 vs 0.55 un-smoothed — `Biquad_frequency_change_…`).

### ParametricEQ (REQ-eq-2) — composition, no new math

A serial `Block` of seven Biquads: `LowCut` (HighPass 30 Hz, off), `LowShelf` (100 Hz), `Peak1..3`
(250 Hz, 1 kHz, 4 kHz, Q 1), `HighShelf` (8 kHz), `HighCut` (LowPass 18 kHz, off). Cascaded sections
multiply, so `magnitudeDbAt(f) = Σ enabled bands' (B9) in dB`.

### StateVariableFilter (REQ-svf-1, REQ-svf-2) — `StateVariableFilter::run`, `qFor`

Zavalishin's topology-preserving transform of the analog SVF, in Andrew Simper's (Cytomic) solved
form. Analog prototype: `H_LP(s) = 1/(s² + k s + 1)` at a normalised cutoff, `k = 1/Q`.

```
(S1)  g  = tan(π·fc/fs)          ; prewarped integrator gain; fc clamped to [10, 0.45·fs]
(S2)  a1 = 1/(1 + g(g + k))     a2 = g·a1     a3 = g·a2
(S3)  v3 = x − ic2
      v1 = a1·ic1 + a2·v3                  ; band-pass
      v2 = ic2 + a2·ic1 + a3·v3            ; low-pass
      ic1 ← 2·v1 − ic1     ic2 ← 2·v2 − ic2   ; trapezoidal integrator states, per channel
(S4)  LP = v2    BP = v1    HP = x − k·v1 − v2    Notch = x − k·v1
(S5)  Q(r) = 0.7071 · 2^(5.5·r),  r ∈ [0,1]   ; 0.707 (Butterworth, no peak) … 32 ; k = 1/Q
(S6)  digital response = the analog one at  ω_a = tan(π f/fs)/tan(π fc/fs)   (bilinear, prewarped)
        |H_LP|² = 1 / ((1 − ω_a²)² + (k·ω_a)²)        ; peak at fc = Q, i.e. 20·log10 Q dB
```

The band output's peak gain is `Q` (= `1/k`), not 0 dB: `k·v1` is the normalised band-pass, so a
user who wants a constant-peak band-pass multiplies by `k`. (S5) is exponential so equal knob travel
is an equal ratio of Q; the top (Q = 32) rings but is stable for any `k > 0` — a full-scale impulse
at r = 1 decays below 1e-6 within two seconds (`SVF_resonance_peaks_and_stays_stable_at_full`).
`tick(x, ch, fc)` runs (S1)–(S4) with a caller-supplied cutoff and no property smoothing: the
modulator (an envelope, an LFO) is the smoothing.

**Verified against the formulas, not against itself:** `tests/run_integration.py`
`parametric_eq_matches_rbj` recomputes (B1)–(B9) in Python and matches the rendered gain at five
frequencies to 0.000 dB; `svf_matches_bilinear_prototype` computes (S6) in Python and matches at
four frequencies to 0.042 dB.
