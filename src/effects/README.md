# effects/ — The effects chain

Applied to the mixed voice output, **in this fixed order**:

```
Compressor ▶ Overdrive ▶ Chorus ▶ Repeater(echo) ▶ Reverb
```

Each is a channel-aware `SignalProcessor` (or `Block`). The chain itself is a serial
`Block`, so it composes uniformly. `Reverb` already exists and is reused as-is.

> Status: DESIGN. 🆕 = new class, ♻️ = reuse existing.

---

## 🆕 `Compressor : SignalProcessor`

Dynamic range compression.

```cpp
enum PropertyIndex {
    thresholdID,  // dB
    ratioID,      // e.g. 4 => 4:1
    attackID,     // SAMPLE COUNT (ms → samples at setter)
    releaseID,    // SAMPLE COUNT
    makeupGainID, // linear
    propertyCount
};
```

- Envelope follower (peak/RMS) with attack/release **in samples**; gain computer applies
  threshold/ratio; makeup gain at the end.
- **Per-channel** follower state, but **shared gain reduction** option for stereo-linked
  behavior is an open question (see below).
- Reuse: `Gain` can apply makeup; the detector is new.

## 🆕 `Overdrive : SignalProcessor`

Waveshaping distortion.

```cpp
enum PropertyIndex {
    driveID,  // pre-gain into the shaper
    toneID,   // post low-pass cutoff (reuses LowPassFilter)
    levelID,  // output trim
    propertyCount
};
```

- Stateless waveshaper (e.g. `tanh(drive * x)`) → cheap, good for the benchmark.
- `tone` reuses `equalizer/LowPassFilter` after the shaper.
- ♻️ `equalizer/LowPassFilter`, ♻️ `Gain`.

## 🆕 `Chorus : SignalProcessor`

Modulated short delay for thickening/width.

```cpp
enum PropertyIndex {
    rateID,    // LFO rate — stored as per-sample phase increment (Hz → samples)
    depthID,   // modulation depth in samples
    mixID,     // dry/wet
    baseDelayID, // center delay in samples
    propertyCount
};
```

- ♻️ Reuses `simpleProcessor/Delay` (already does interpolated, modulatable delay — the
  README calls out its Doppler capability). The LFO modulates the delay's read position.
- **Per-channel LFO phase** (channel-aware base) gives true stereo chorus width with one
  instance — exactly the kind of "different chain per channel" the base now supports.
- LFO advances by **sample count**.

## 🆕 `Repeater : SignalProcessor` (echo / "echo echo echo")

Feedback echo where **each repeat is quieter and progressively low-passed**.

```cpp
enum PropertyIndex {
    delayID,        // echo spacing — SAMPLE COUNT (ms → samples)
    feedbackID,     // volume reduction per repeat as a fraction (the "reduce %")
    toneCutoffID,   // low-pass cutoff applied in the feedback path each repeat
    mixID,          // dry/wet
    propertyCount
};
```

- ♻️ Built from `base/FeedbackBlock` + `simpleProcessor/Delay` + `equalizer/LowPassFilter`:
  the feedback path = `Delay` → `LowPassFilter` → gain(`feedback`). Because the low-pass
  sits *inside* the feedback loop, every repeat is cut a little more — the requested
  "cut off every repeat" behavior falls out naturally.
- `feedback` is the per-repeat "reduce % in volume."
- All timing in **samples**.
- This is distinct from `functional/ProcessorRepeater` (a compile-time chain of copies);
  the name overlap is noted — this one is a runtime feedback echo.

## ♻️ `Reverb` (reused from `reverb/`)

Existing `reverb/Reverb` (diffuse + feedback network, low/high cut). Reused, with two
changes during the channel-aware migration: (1) it now holds **one BasicReverb network per
channel** (true stereo, no L/R bleed); (2) a **dry/wet mix** was added (`setMix`, param
`RV_MIX`) so that as the final serial stage it doesn't replace the signal with only the
attenuated wet tail. Sits last in the chain.

---

## Chain assembly

```cpp
Block fx;                 // serial
fx.add(&compressor);
fx.add(&overdrive);
fx.add(&chorus);
fx.add(&repeater);
fx.add(&reverb);
// fx.out(sample, channel) runs the whole chain for that channel
```

Per-channel state in each effect means stereo image is preserved/created without manual
duplication. If L and R should differ (e.g. ping-pong echo), `add(proc, channel)` lets the
chain diverge.

## Reuse map (summary)

| Effect | Reuses |
|---|---|
| Compressor | `Gain` (makeup); new detector |
| Overdrive | `LowPassFilter` (tone), `Gain` |
| Chorus | `Delay` (modulated), per-channel LFO |
| Repeater | `FeedbackBlock` + `Delay` + `LowPassFilter` + `Gain` |
| Reverb | `reverb/Reverb` as-is |

## Open questions for review

1. **Compressor stereo linking:** independent per-channel gain reduction (simplest) vs.
   linked (max of both channels → stable stereo image). Default: independent for now.
2. **Reverb:** keep the existing `Reverb` as the only reverb, or expose more of its params
   (diffusion, low/high cut) over UART? Default: expose what it already has.
3. **Echo stereo:** plain (same on both channels) vs. ping-pong. Default: plain; ping-pong
   is a later option via per-channel delay times.

## Math — `Compressor.cpp` (with its sidechain, REQ-fx-sidechain-1)

```
(C1)  level[n] = |x[n]|                      ; plain:  the input itself            (`process`)
      level[n] = |k[n]|                      ; keyed:  the KEY, another track      (`processBlock`, sidechain on)
                                             ;         no key handed in → k = 0
(C2)  a = e^(−1/Ta),  r = e^(−1/Tr)          ; Ta, Tr = attack, release in samples (ms · fs / 1000)  (`update`)
      env[n] = a·(env[n−1] − level[n]) + level[n]   if level[n] > env[n−1]     (attack)
             = r·(env[n−1] − level[n]) + level[n]   otherwise                  (release)
(C3)  over  = 20·log10(env + 1e−9) − T       ; T = threshold dB
      cut   = max(0, over) · (1 − 1/R)       ; R = ratio
(C4)  y[n]  = x[n] · 10^(−cut/20) · makeup   ; the gain always applies to the INPUT   (`compress`)
```

| setter | unit | symbol |
|---|---|---|
| `setThresholdDb` | dB | T |
| `setRatio` | :1 | R |
| `setAttackMs` / `setReleaseMs` | ms | Ta, Tr (stored in samples) |
| `setMakeupGain` | linear | makeup |
| `setSidechain`, `setKey` | on/off, buffers | which level (C1) reads |

Per channel; a key with fewer channels than the input repeats its last. Keyed by a steady 0 dBFS key,
−30 dB at 4:1 cuts (0 + 30)·¾ = 22.5 dB however quiet the input (`Compressor_sidechain_ducks_on_the_key_not_the_input`);
a kick every half second dips a bass 20.6 dB within 40 ms (`sidechain_pump`).

## Math — `Limiter.cpp` (REQ-fx-limiter-1)

A brickwall lookahead limiter; L = lookahead in samples, c = ceiling (linear), d = drive (linear).

```
(L1)  r[n] = min(1, c / max_ch |d·x_ch[n]|)             ; the gain frame n needs, channels linked
(L2)  m[n] = min{ r[k] : n − L ≤ k ≤ n }                ; sliding-window minimum (monotonic deque)
(L3)  q[n] = min(m[n], 1 − (1 − q[n−1])·ρ),  ρ = e^(−1/(release·fs))   ; release: back toward 1
(L4)  g[n] = (1/(L+1)) · Σ_{j=0..L} q[n−j]             ; box average (running sum, re-summed each lap)
(L5)  y_ch[n] = d·x_ch[n−L] · g[n]                      ; the audio, L samples late
```

**Why it never exceeds c.** Each q[n−j], 0 ≤ j ≤ L, is ≤ m[n−j], whose window [n−j−L, n−j] contains
n − L; so every term is ≤ r[n−L], so is their average, and |y| ≤ |d·x[n−L]|·r[n−L] ≤ c. A final clamp to
±c only ever meets rounding error — the limiter counts the frames where it did more (`clamped()`), and
the tests require zero. **Why it glides.** g changes by at most 1/(L+1) a frame (one term of the box
replaced), so a peak is met by a ramp of L samples, not a step. Under the ceiling every r = 1, g = 1
exactly, and the output is the input L samples late, bit for bit. Latency = L.

| setter | unit | symbol |
|---|---|---|
| `setGainDb` | dB | d = 10^(dB/20) |
| `setCeilingDb` | dBFS | c = 10^(dB/20) |
| `setReleaseMs` | ms | ρ |
| `setLookaheadMs` | ms | L = round(ms · fs / 1000) |

**(L6) True peak** (`setTruePeak`, REQ-fx-limiter-2). The need is read BETWEEN samples and spread to the
neighbours:

```
(L6)  p[n] = max_ch max( |y_p[n]| for p = 0 … 7,  |d·x_ch[n−7]|,  |d·x_ch[n−8]| )   ; meter README (T1)–(T3), F = 8,
                                                                                     ; onSamples: points every ⅛ in (n−8, n−7]
      r[n] = min(1, c / p[n])                                                        ; replaces (L1)
      W = L + 16:  (L2) takes the minimum over [n − W, n];  (L5) reads x[n − W]       ; latency L + 16
```

**Why it holds between samples.** r[n] is the need of the interval (n−8, n−7]. By (L2)–(L4) as before, the gain
applied at n is ≤ every r[k] with n − W ≤ k ≤ n − L — 17 values, the needs of the intervals from 8 before the frame
being output (x[n − W]) to 8 after it. A point between two output samples is reconstructed from the 16 samples
around it, each of whose gains is then ≤ the need of that point's own interval: the whole kernel is turned down
for it. What remains is second order — the gain's slope (≤ 1/(L+1) a frame) across the kernel's negative lobes —
and measures under 10⁻⁵ dB. Detecting at 8× rather than BS.1770's 4× is deliberate: 4× meters disagree on where
their points fall (on the quarters, or between them), and a 12 kHz sine 45° out reads 0.17 dB apart on the two;
every ⅛ sample covers both. **Verified:** a quarter-rate sine 45° out, a sweep to 20 kHz, square bursts, noise;
1/64/256 blocks; the output through the TruePeakMeter never more than 10⁻⁵ dB over the ceiling, the sample-peak
mode ≈ 3 dB over it on the sine (`Limiter_true_peak_mode_holds_the_ceiling_between_samples`); an interpolator of
the integration test's own (Blackman, 64 taps a phase) at 4× and 8× reads −1.001 dBTP for a −1 ceiling
(`limiter_true_peak`). A test signal must be band-limited: a switch in one sample rings between samples by what
each kernel's length makes of it.

| setter | unit | symbol |
|---|---|---|
| `setTruePeak` | on/off | (L6) in place of (L1); W = L + 16 |

Not composed from the library's `Delay` (fractional, modulated) — this needs an integer delay, a
window minimum and a box average, none of which existed. Verified: every lookahead 0…10 ms and block
size 1/37/128, a mix with 3.0 spikes driven 12 dB into −1 dBFS: never above, clamp idle, step ≤ 1/(L+1)
(`Limiter_never_exceeds_its_ceiling_and_glides`); through the registry, 18 dB into −1 dBFS peaks at
−1.000 dBFS (`limiter_ceiling`).


## Math — `MultibandCompressor.cpp` (REQ-fx-multiband-1)

Three bands, each its own `Compressor`, split where the user says. **A composition (rule 1):** the crossovers are
`Biquad::design`'s Butterworth low- and high-passes run in pairs (a Linkwitz-Riley 4th order), the bands' dynamics
the library's `Compressor` ((C1)–(C4)); its own: the split and the all-pass that keeps the sum flat.

```
(MB1) Linkwitz-Riley 4th order at f:  LP_f = B_lp(f)·B_lp(f),  HP_f = B_hp(f)·B_hp(f)   (B a Butterworth biquad, Q 0.7071)
      LP_f + HP_f = AP_f  (an all-pass: |AP| = 1)                                       `MultibandCompressor::process`
(MB2) The bands:  low = AP_f2(LP_f1 x),  mid = LP_f2(HP_f1 x),  high = HP_f2(HP_f1 x)
      low + mid + high = AP_f2(LP_f1 x) + AP_f2(HP_f1 x) = AP_f2(AP_f1 x)  — flat in magnitude, f1 < f2
      (AP_f2 on the low band is LP_f2 + HP_f2, run on it: the same phase the others took)
(MB3) Each band through its own Compressor (threshold, ratio, attack, release, and its gain as makeup)
(MB4) y = (low' + mid' + high') · output
```

| parameter | symbol | unit / range | default |
|---|---|---|---|
| `low.split`, `high.split` | f1, f2 | Hz (f2 kept above f1 by a fifth) | 200, 2500 |
| `<band>.threshold`, `.ratio`, `.gain` | T, R, makeup | dB, :1, dB | −20, 3, 0 |
| `attack`, `release` | Ta, Tr | ms (all bands) | 10, 150 |
| `output` | — | dB | 0 |

Ratio 1 everywhere: the bands sum to an all-pass — the input's magnitude to 0.01 dB at every frequency.

## Math — `Gate.cpp` (REQ-fx-gate-1)

A gate and a downward expander: what falls below the threshold is turned down — by the ratio, never by more than the
range. **Why not composed (rule 1):** `Compressor` turns down what rises ABOVE its threshold, and its gain follows its
level each sample; a gate's curve is the mirror, it HOLDS open before it closes, and it smooths its gain (a gate
chatters if its gain follows a level). It keeps the compressor's detector form ((C2)) and its key (a keyed gate opens
on another track).

```
(GT1) Level — linked across channels (the image never moves):  ℓ[n] = max_c |x_c[n]|  (or the key's, keyed)
      env[n] = a·(env[n−1] − ℓ) + ℓ  if ℓ > env[n−1]  else  r·(env[n−1] − ℓ) + ℓ   (C2's form; a, r from 1 ms, 50 ms)
(GT2) The curve:  L = 20·log10(env + 1e−9);  g* = 0 dB when L ≥ T;  else max(−range, (L − T)·(R − 1))
      R = ratio (1 = nothing; 2 = 2:1 downward; 100 = a gate)
(GT3) Hold — below T, g* is held at 0 dB for `hold` ms after the level last stood at T
(GT4) Its gain, smoothed in dB:  g ← g* + α·(g − g*),  α = e^(−1/(attack·fs/1000)) opening (g* > g),
      e^(−1/(release·fs/1000)) closing
(GT5) y_c = x_c · 10^(g/20)
```

| parameter | symbol | unit / range | default |
|---|---|---|---|
| `threshold` | T | dB −80 … 0 | −40 |
| `ratio` | R | 1 … 100 (100: a gate) | 100 |
| `range` | — | dB 0 … 100 | 80 |
| `attack`, `hold`, `release` | — | ms | 1, 20, 100 |
| `sidechain` | — | off · on (the key opens it) | off |

(GT1)–(GT5) are `Gate::process`; the per-sample constants (a, r, the two α, the hold in samples) are `Gate::coeffs`,
from the sample rate. A 2:1 expander 10 dB under its threshold turns down 10.000 dB (`Gate_passes_the_loud_and_shuts_the_quiet`,
`gate_curve`); held 20 ms past a fall from a tone, then closed by 80·(1 − e⁻⁴) = 78.5 dB four release constants on
(`Gate_holds_open_before_it_closes`).

## Math — `TransientShaper.cpp` (REQ-fx-transient-1)

More or less attack, more or less sustain — whatever the level. **Why not composed (rule 1):** the `Compressor` and the
`Gate` turn a sound toward a curve OF its level, so a loud hit and a quiet one are treated differently; a transient
shaper compares the envelope with a slower copy of itself, so it acts on how the level MOVES, at any level. It keeps
(C2)'s follower form, three times over.

```
(TS1) Level — linked across channels:  ℓ[n] = max_c |x_c[n]|
(TS2) The fast envelope (C2's form):  e_f[n] = a_f·(e_f[n−1] − ℓ) + ℓ   if ℓ > e_f[n−1]
                                             = r_f·(e_f[n−1] − ℓ) + ℓ   otherwise
      a_f, r_f = e^(−1/(τ·fs/1000)), τ = 1 ms, 100 ms
(TS3) Two slow envelopes that follow e_f — one only up, one only down:
      e_r[n] = a_s·(e_r[n−1] − e_f[n]) + e_f[n]   if e_f[n] > e_r[n−1],  else e_f[n]      (up at 30 ms, down at once)
      e_s[n] = r_s·(e_s[n−1] − e_f[n]) + e_f[n]   if e_f[n] < e_s[n−1],  else e_f[n]      (down at 1 s, up at once)
      so e_r ≤ e_f ≤ e_s always, and on a steady level the three meet
(TS4) The rise and the fall, in dB:  D_a = 20·log10((e_f + ε)/(e_r + ε)) ≥ 0,  D_s = 20·log10((e_s + ε)/(e_f + ε)) ≥ 0
      ε = 10^−5 (−100 dB: silence reads as no movement)
      g = clamp(A·D_a + S·D_s, −24, +24) dB,   A = attack / 100,  S = sustain / 100
(TS5) y_c = x_c · 10^((g + out)/20);   A = S = 0 and out = 0: y = x, to the bit
(TS6) After a step — the closed forms its tests hold it to:
      from silence to a steady ℓ at n = 0:
        e_f[n] = ℓ·(1 − a_f^(n+1))
        e_r[n] = ℓ·[(1 − a_s^(n+1)) − (1 − a_s)·a_f·(a_s^(n+1) − a_f^(n+1)) / (a_s − a_f)]
      a fall from ℓ1 to ℓ2 (Δ = ℓ1 − ℓ2), k samples on:
        e_f = ℓ2 + Δ·r_f^(k+1)
        e_s = ℓ2 + Δ·[r_s^(k+1) + r_f·(1 − r_s)·(r_s^(k+1) − r_f^(k+1)) / (r_s − r_f)]
```

| parameter | symbol | unit / range | default |
|---|---|---|---|
| `attack` | A·100 | % −100 … +100 (−100 takes the rise away, +100 doubles it in dB) | 0 |
| `sustain` | S·100 | % −100 … +100 (−100 takes the tail away, +100 holds it toward the 1 s fall) | 0 |
| `output` | out | dB −24 … +24 | 0 |

The time constants are chosen, not derived (`kFastAttackMs` … `kSlowReleaseMs`): 1 ms catches a drum's front; 30 ms is
how long a rise counts as an attack; 1 s is the fall a tail is measured against; 100 ms for the fast envelope's fall
halves the steady ripple 50 ms gives while still following a decay. The ±24 dB limit (`kLimitDb`) keeps a sound
starting from silence — an infinite rise — finite. A steady sine's |x| ripples, which the envelopes read as a small
rise and fall: 0.07 dB each at 100 Hz, 0.2 at 30 Hz (`Transient_steady_is_untouched`); a square, its |x| steady,
is untouched exactly.

At +100 % attack a sound from silence is 11.70 dB louder 10 ms in and 1.89 dB at 50 ms; at +100 % sustain a 20 dB fall
is 7.03 dB less 100 ms on (`Transient_attack_shapes_the_rise`, `Transient_sustain_shapes_the_fall`, both to (TS6) within
1e−9 dB; `transient_follows_its_closed_forms` from the WAV). (TS1)–(TS5) are `TransientShaper::process`; the
per-sample constants are the constructor's, from the sample rate.

## Math — `DeEsser.cpp` (REQ-fx-deesser-1)

Sibilance held down. **Composed of:** `Biquad::design` (RBJ) for both of its filters, and (C2)'s follower form. **Why
not a keyed `Compressor`:** the Compressor's gain multiplies its input, per channel; split, a de-esser turns down
only the band it hears, linked across channels, and holds it at the threshold (a ratio of ∞ with a range) — so it
owns its gain law and borrows the rest.

```
(DE1) The band:  h_c = HP(x_c),  HP = Biquad::design(HighPass, f, Q = 1/√2) — a Butterworth high-pass at f
(DE2) Its level, linked:  ℓ = max_c |h_c|;  env = C2's follower, 1 ms up, 50 ms down
(DE3) The cut — the band held at the threshold, never by more than the range:
      cut = clamp(20·log10(env + 1e−9) − T, 0, range)  dB
(DE4) split:  y_c = SH(x_c),  SH = Biquad::design(HighShelf, f, Q = 1/√2, gain = −cut) — redesigned when the cut or f
      moves; at 0 dB the RBJ shelf is the identity (b = a) and its state is zero, so it is not run while nothing is
      cut, its state held at zero, and a cut growing from 0 starts it from its own past
(DE5) wide:   y_c = x_c · 10^(−cut/20)
      cut = 0: y = x, to the bit (either mode)
```

| parameter | symbol | unit / range | default |
|---|---|---|---|
| `frequency` | f | Hz 2000 … 16000 (log) — the band's corner and the shelf's | 6000 |
| `threshold` | T | dB −60 … 0 | −30 |
| `range` | — | dB 0 … 24 | 12 |
| `mode` | — | split · wide | split |

The detector is a peak follower with a 1 ms attack, so on a steady sine it sits a little under the peak (0.43 dB at
10 kHz): the cut is the threshold's distance from what it reads, never more. Split, a cut of g dB takes a sound
far above f down g dB and one at f g/2 (the shelf's midpoint) — a 10 kHz sibilance under a 6 dB cut at 6 kHz: −5.50 dB;
a 1 kHz voice: −0.004 dB (`DeEsser_split_spares_the_rest_wide_does_not`, `deesser_shelves_the_band`). (DE1)–(DE5) are
`DeEsser::process`; the follower's constants are the constructor's, the band's `design()`.

## Math — `Saturator.cpp` (REQ-fx-saturator-1)

A curve driven, four times oversampled. **Composed of:** the true-peak meter's interpolator (meter README (T1)–(T2),
its `onSamples` design at F = 4) for the way up; its own kernel formula, at another length, for the way down.
**Why not `Overdrive`:** Overdrive is tanh at the base rate — every harmonic above Nyquist folds back into the band
(a 7 kHz sine driven 12 dB: aliases at −21 dB). Overdrive stays as it is (its sound is in songs); this is the clean one.

```
(SA1) Up:  v = drive·x  (drive = 10^(dB/20));  u[4n + p] = Σ_k h_p[k]·v[n − k],  p = 0 … 3
      h_p = TruePeakInterpolator(4, onSamples).taps — 63 taps, centred on an upsampled point: u[4n + p] lies at
      n − 7.75 + p/4 (p = 3: the sample x[n − 7] itself)
(SA2) The curve, at each point:
      soft  s = tanh(u)
      hard  s = clamp(u, −1, 1)
      tube  s = (tanh(u + b) − tanh b) / (1 − tanh² b),  b = 0.25  — at rest at 0, slope 1 there, not odd: even harmonics
      fold  s = sin(π·u / 2)  — 1 at u = 1, folded back beyond
(SA3) Every harmonic the curve makes up to 96 kHz is represented at 192 kHz; what lies between 24 and 96 kHz is
      the way down's to remove; only what lies above 96 kHz still folds (and lands in the stopband unless near 192)
(SA4) Down:  d[n] = Σ_m g[m]·s[4n + 3 − m],  m = 0 … 64
      g[m] = sinc((m − 32)/4)·w[m] / Σ g,   w = Kaiser, β = 6 (T1's window)  — cut at the original Nyquist
      centred 32 points (8 samples) back: with the way up's 7.75 − 0.75, d[n] lies at n − 15 exactly
      (the up kernel's centre and the down kernel's are chosen so the sum is whole: latency() = 15)
(SA5) tube only — its DC blocked:  y[n] = d[n] − d[n−1] + R·y[n−1],  R = e^(−2π·5/fs)
(SA6) y = out · (mix·d + (1 − mix)·x[n − 15])  — the dry path delayed to match: no comb at any mix
```

| parameter | symbol | unit / range | default |
|---|---|---|---|
| `drive` | — | dB 0 … 36 | 6 |
| `curve` | — | soft · hard · tube · fold | soft |
| `mix` | mix | 0 … 1 | 1 |
| `output` | out | dB −24 … +12 | 0 |

Each curve's harmonics are its Fourier series to 0.031 dB (hard's 3rd; the others' within 0.008 — `Saturator_harmonics_are_the_curves`; both kernels flat to
±0.003 dB under 16 kHz — above it they roll off, as the meter's does). A 7 kHz sine at 0.8 driven 12 dB folds back
soft −81 dB, hard −46, tube −56, fold −62, against −21, −18, −22, −2 for the same curve at the base rate
(`Saturator_keeps_its_aliases_down`) — hard clipping's corners make harmonics that fall only as 1/h², the rest fall
faster. (SA1)–(SA6) are `Saturator::process`; (SA2) is `Saturator::shape`; the down kernel is the constructor's.

## Math — `Bitcrusher.cpp` (REQ-fx-bitcrusher-1)

Fewer bits, a lower rate — on purpose, the images and the steps are the sound. **Why not composed:** nothing in the
library rounds or holds; this is two lines of arithmetic.

```
(BC1) The grid:  q = 2^(bits − 1);  y = round(x·q) / q    — mid-tread, a step 1/q; |y − x| ≤ 1/(2q)
      a sine at amplitude a: SNR = 6.02·bits + 1.76 + 20·log10 a dB (the error uniform over a step)
(BC2) The hold:  φ ← φ + min(1, rate/fs) each sample; when φ ≥ 1, φ ← φ − 1 and a new value is taken (BC1 of
      the input then), held until the next — the phase carried, so a rate that does not divide fs holds for
      ⌊fs/rate⌋ or ⌈fs/rate⌉ samples and takes exactly `rate` values a second; at or above fs every sample its own
(BC3) y = out · (mix·held + (1 − mix)·x)
```

| parameter | symbol | unit / range | default |
|---|---|---|---|
| `bits` | bits | 1 … 24 (whole) | 8 |
| `rate` | rate | Hz 100 … 96000 (log; at or above fs: no hold) | 96000 |
| `mix` | mix | 0 … 1 | 1 |
| `output` | out | dB −24 … +12 | 0 |

8 bits on a sine at 0.9: SNR 49.15 dB against the formula's 49.01; 12 bits 73.13 against 73.09
(`Bitcrusher_rounds_to_its_grid`); at 7 kHz held 6 or 7 samples, 7000 taken a second (`Bitcrusher_holds_at_its_rate`).
(BC1)–(BC3) are `Bitcrusher::process`.

## Math — `Phaser.cpp` (REQ-fx-phaser-1)

Notches swept by an LFO. **Why not composed:** the library has no first-order all-pass (the multiband's are
Linkwitz-Riley biquads, the reverb's diffusion an FDN); the stage is one line, so it is written here.

```
(PH1) A stage — a first-order all-pass, its break frequency f_b (the bilinear transform of (1 − s/ω_b)/(1 + s/ω_b)):
      A(z) = (a + z⁻¹) / (1 + a·z⁻¹),   a = (t − 1)/(t + 1),   t = tan(π·f_b/fs)
      |A| = 1 everywhere;  its phase θ(f) = −2·atan(tan(πf/fs) / t):  0 at DC, −π/2 at f_b, −π at Nyquist
      y = a·v + z;  z ← v − a·y                    (`Phaser::process`; `Phaser::coefficient`)
(PH2) The sweep — a sine in log frequency, the odd channels `stereo` ahead:
      f_b = clamp(f_c · 2^(depth · sin(2π(φ + offset))), 20 Hz, 0.45·fs),  φ ← φ + rate/fs each sample
(PH3) Fed back, a sample on:  v = x + fb·w[n − 1],  w = A^N(v)
(PH4) Mixed:  y = (1 − mix)·x + mix·w
(PH5) Still (depth 0): H(e^jω) = (1 − mix) + mix·A^N / (1 − fb·A^N·e^(−jω));  with fb = 0 and mix ½ it is
      |cos(N·θ/2)| — N/2 notches, where N·θ = −π(2k + 1):
      f_k = (fs/π)·atan(t·tan(π(2k + 1)/(2N))),  k = 0 … N/2 − 1;  0 dB where N·θ is a whole turn (N = 4: at f_b)
```

| parameter | symbol | unit / range | default |
|---|---|---|---|
| `rate` | rate | Hz 0.01 … 10 (log) | 0.5 |
| `center` | f_c | Hz 100 … 8000 (log) | 800 |
| `depth` | depth | octaves 0 … 4 | 2 |
| `stages` | N | 2 … 12, even | 4 |
| `feedback` | fb | −0.9 … 0.9 | 0.3 |
| `stereo` | offset·360 | degrees 0 … 180 | 90 |
| `mix` | mix | 0 … 1 | 0.5 |

Still, a sine at each f_k cancels to the float floor (4 and 8 stages); with feedback ½ and mix 0.7 every frequency
is (PH5) to 0.0000 dB (`Phaser_notches_where_the_phase_says`); the sweep is (PH2) to 1e−9 (`Phaser_sweeps_its_break`).
