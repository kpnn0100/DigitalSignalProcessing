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
