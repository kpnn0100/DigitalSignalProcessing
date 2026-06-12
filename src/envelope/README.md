# envelope/ — Amplitude shaping

> Status: DESIGN. 🆕 = new class.

> **The envelope is NOT an effect.** It is a building block **owned by
> [`SignalGenerator`](../base/README.md)** and applied to the source signal there. It is
> never inserted into the effects chain. `ADSREnvelope` lives in its own folder only because
> it is a reusable component; its single consumer is the generator base.

## 🆕 `ADSREnvelope`

A classic Attack–Decay–Sustain–Release amplitude envelope. It produces a per-sample gain
value (0..1) that `SignalGenerator` multiplies into the generated waveform. It derives
`SignalProcessor` purely to reuse the property-smoothing and sample-rate infrastructure —
not to sit in any processing chain.

```cpp
enum PropertyIndex {
    attackID,   // attack length  — stored as SAMPLE COUNT (ms → samples at setter)
    decayID,    // decay length   — sample count
    sustainID,  // sustain LEVEL  — linear gain 0..1 (not a duration)
    releaseID,  // release length — sample count
    propertyCount
};

void setAttackMs(double ms);   // ms → samples via AudioConfig::sampleRate()
void setDecayMs(double ms);
void setSustain(double level); // 0..1
void setReleaseMs(double ms);

void noteOn(double velocity);  // velocity 0..1 scales peak level; enters Attack
void noteOff();                // enters Release from current level
bool isFinished() const;       // true once Release reaches 0 — lets VoiceManager free it
double process(double in, int channel) override; // multiplies in by current env value
```

## Sample-count state machine (no time, no clocks)

```
   level
    1 ┤      ___
      │     /   \____________        ← sustain level
      │    /                 \
      │   /                   \
    0 ┼──┴───────────────────────\____▶ samples
       Attack  Decay   Sustain    Release
       (N_a)   (N_d)  (held)      (N_r)
```

- Each stage advances by **one sample per `process()` call** via an internal sample
  counter — **never** `millis()` / `chrono`.
- Stage lengths (`N_a`, `N_d`, `N_r`) are sample counts derived from ms at the setter.
- **Per channel:** the running counter/level/stage are stored per channel (the base is
  channel-aware), so both channels track identically but independently — and could diverge
  if ever driven differently.
- `noteOn(velocity)` sets the target peak = velocity (linear); `noteOff()` jumps to Release
  regardless of current stage.
- Curves: linear first (cheapest, clearest for the benchmark); exponential/analog-style
  curve is a later, OCP-friendly subclass or a `curveID` property.

## Reuse map

| Need | Reuse |
|---|---|
| Property smoothing, sample-rate hooks | `base/SignalProcessor` |
| Final gain application | multiply inside `process` (no need for `Gain`) |

## Where it lives in the graph

Inside the generator — not the effects chain:

```
SignalGenerator.process(ch):   generate(ch)  ×  ADSREnvelope value(ch)
                               └ raw waveform ┘   └ this module ┘
```

`noteOn(velocity)` / `noteOff()` are exposed on `SignalGenerator`, which forwards to its
owned `ADSREnvelope`. The synth `Voice` triggers its generators; it does not hold a
separate envelope stage.

## Open question for review

- Should `noteOff` during Attack/Decay release **from the current instantaneous level**
  (smoothest, recommended) or snap to sustain first? Default: release from current level.
