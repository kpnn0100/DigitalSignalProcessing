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
- `reset()` clears what the module can clear (instrument voices, the EQ's and the filter's
  memory). `Reverb`, `Repeater` and `Chorus` have no reset, so their tails survive a seek; a host
  that needs silence after a seek builds a fresh device.

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
