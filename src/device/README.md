# device/ — every instrument and effect behind one face

| class | what | requirement |
|---|---|---|
| `ParamSpec` | one parameter: name, label, unit, range, default, choices, integer/log hints | REQ-device-1 |
| `DeviceType` | one registry entry: name, label, kind, summary, its `ParamSpec`s, a factory | REQ-device-1 |
| `Device` | the uniform face a host drives: `process`, notes, `setParam` by index or name | REQ-device-2 |
| `DeviceRegistry` | every type, in a stable order: `types()`, `find(name)`, `create(name)` | REQ-device-3 |

## Why this exists

A host (the Solaris DAW) must not know a `Compressor` from a `BasicSynth`, and must not hold a
second description of their parameters. The registry entry is the **only** description of a
parameter anywhere in the suite: Solaris reads its `.slp` keys, its command addresses, its generated
API document and its UI's knobs from it (Solaris R-DSP-2). A range written anywhere else is a range
that will drift from this one.

## The types

`synth` (Basic Synth) · `drums` (Drum Machine) — instruments; `compressor` · `eq` · `reverb` ·
`delay` · `chorus` · `drive` · `filter` — effects. The parameters themselves are not restated here
— this file would become the second copy. Read them from `DeviceRegistry::types()`, or from
Solaris's generated `docs/API.md`.

Each instrument's defaults are **its own** (`BasicSynth::Params{}`, `DrumMachine::defaults`), read
into the registry when the entry is built, so the two cannot disagree. The effects wrap the existing
modules unchanged: `Compressor`, `ParametricEQ`, `Reverb`, `Repeater` (= delay), `Chorus`,
`Overdrive` (= drive); `filter` wraps `StateVariableFilter` with a dry/wet mix.

## Rules

- **Values are engineering units** — Hz, dB, ms, st, ct, oct, or a 0..1 amount; a choice's value
  is its index into `choices`.
- **Every write is clamped** (`ParamSpec::clamp`): into [min, max] (a choice: [0, n−1]); rounded
  if integer or a choice; a non-finite value becomes the default. No input can put a device into a
  state its spec does not describe. An unknown index or name changes nothing and returns false.
- **An effect transforms in place; an instrument ADDS** into the buffers.
- **Channel by channel**, as the library's own block path runs (reverb/README.md).
- **A device reports its latency** (`latency()`, REQ-device-8): the samples its output lags its input
  at its current parameters — the `limiter`'s lookahead L (Limiter's (L1)–(L5) delay the audio by L),
  0 for every other type (a chorus's or a delay's delay is its SOUND, not a lag). A host delays every
  other path by it so all signals meet in time; it changes with `lookahead`, so a host reads it again
  after that write.
- **A kit's keys** (REQ-device-6, REQ-device-9): `noteNames` says what each key is for a person
  ("Closed Hat"), `notePrefixes` which parameters shape it for an address (`chat.*`) — the same keys,
  the same order, both from the kit's own pad table.
- `reset()` clears what the module can clear (instrument voices, the EQ's and the filter's
  memory). `Reverb`, `Repeater` and `Chorus` have no reset, so their tails survive a seek; a host
  that needs silence after a seek builds a fresh device.

## Math — `ParamMapping.cpp` (REQ-device-7)

A spec's normalised value n ∈ [0, 1], used by every plugin host and knob:

```
(N1)  continuous, linear:   n = (v − min) / (max − min)              v = min + n·(max − min)
(N2)  continuous, logScale (min > 0):
                            n = ln(v / min) / ln(max / min)          v = min · (max / min)^n
(N3)  discrete (an integer: steps = max − min, base = min; a choice: steps = choices − 1, base = 0):
                            n = (v − base) / steps                   v = base + min(steps, ⌊n · (steps + 1)⌋)
```

Every result goes through `ParamSpec::clamp`. (N3) is VST3's convention: each step owns the slice
[k/(steps+1), (k+1)/(steps+1)) of the knob, and k/steps lies inside its own slice, so a step is exact
both ways. (N2) puts the geometric mean √(min·max) at n = ½ — equal ratios, equal travel.

Text: a choice's name; else the suite's canonical number — the fewest decimals (at least one) that
`strtod` reads back to the same double, `0.0` for zero, the shortest `%g` that round-trips outside
1e-6 … 1e15. Solaris's `.slp` and a VST3 plugin's state both store exactly this.

## Math — `DeviceRegistry.cpp`

The adapters add no DSP; two conversions and one blend:

```
(R1)  linear = 10^(dB/20)                 ; compressor `makeup`, drive `level` (the modules take linear gain)
(R2)  filter:  y = mix·SVF(x) + (1 − mix)·x
```

Verified: every parameter of every type set to its minimum and to its maximum while the device
renders keeps every sample finite (`Device_every_parameter_at_both_ends_renders_finite`); each
adapter does what the module's setter does, measured on rendered audio (`Device_adapters_…`,
`Device_synth_and_drum_params_…`); written by name through the registry, `eq`'s `peak2.gain = 6`
matches the RBJ formula recomputed in Python to 0.000 dB (`registry_eq_by_name`).
