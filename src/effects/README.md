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

Not composed from the library's `Delay` (fractional, modulated) — this needs an integer delay, a
window minimum and a box average, none of which existed. Verified: every lookahead 0…10 ms and block
size 1/37/128, a mix with 3.0 spikes driven 12 dB into −1 dBFS: never above, clamp idle, step ≤ 1/(L+1)
(`Limiter_never_exceeds_its_ceiling_and_glides`); through the registry, 18 dB into −1 dBFS peaks at
−1.000 dBFS (`limiter_ceiling`).

