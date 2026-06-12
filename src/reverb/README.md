# reverb/ — algorithmic reverb (Feedback Delay Network)

`Reverb` is a channel-aware `SignalProcessor` wrapping a Feedback Delay Network (FDN) per the
Signalsmith design (`ClassForReverb.h`: `MultiChannelMixedFeedback` + Householder mixing).

## Parameters

| Setter | Unit | Meaning |
|--------|------|---------|
| `setDelayInMs(ms)` | ms | room size (base loop time) |
| `setDecayInMs(ms)` | ms | RT60 decay time |
| `setMix(0..1)` | — | dry/wet (0 = dry, 1 = wet) |
| `setDiffusion(int)` | — | diffusion amount |
| `setWidth(0..1)` | — | **stereo width** (0 = mono/identical channels, 1 = max decorrelation) |
| `setLowCutFrequency / setHighCutFrequency` | Hz | tone of the wet tail |

## Why the old reverb was "not natural for stereo"

The wet field is produced by one `BasicReverb` (8-channel FDN) **per output channel**, held in
`bsReverb[channel]`. Before 1.0.0 every channel's network was configured **identically** — same
base delay, same delay distribution, same mixing matrix. Consequences:

- Fed a mono source (the same `in` on L and R), the L and R tails came out **bit-identical** →
  the reverb image collapsed to the centre. That is mono, not stereo.
- Two sealed, identical mono reverbs side-by-side give no sense of width or envelopment, which
  is the whole point of a stereo reverb.

## The fix — decorrelated per-channel networks + a width control

Each output channel's FDN is now seeded with a **different base delay** so identical input
produces **decorrelated** tails (the standard way to get natural stereo width):

```
offsetNorm(c) = (channels <= 1) ? 0 : (2*c/(channels-1) - 1)   // spread c across [-1, +1]
delay(c)      = baseDelayMs * (1 + width * kStereoSpread * offsetNorm)   // kStereoSpread = 0.15
```

- `width = 0` → every channel gets `baseDelayMs` → identical networks → **mono** (back-compat).
- `width = 1` → channels span ±15 % around the base delay → **decorrelated, wide** tail.
- The default width gives a natural, moderately wide tail.

**Correctness contract:** with `width > 0` and the same mono input on every channel, the channel
outputs **differ** (decorrelated). With `width == 0` they are **identical**. The tail is always
finite and decays.

### Why decorrelation rather than true cross-feed

A physically truer stereo reverb would cross-feed energy between channels each sample. The
library processes one channel at a time (the block path renders a whole block per channel before
moving to the next), so a processor never sees both channels of a frame simultaneously — true
per-sample cross-feed can't be expressed without breaking the per-channel `process(in, channel)`
contract (and would make block vs per-sample behaviour diverge, an LSP hazard). Per-channel delay
decorrelation gives a genuinely wide stereo image **within** that contract, and is what most
production reverbs do for their output stage anyway.
