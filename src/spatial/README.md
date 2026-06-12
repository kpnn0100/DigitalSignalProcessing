# spatial/ — 3D source placement & room simulation (LEGACY)

> **Status: legacy, excluded from the build.** These modules still use the pre-1.0 mono
> `process(Sample in)` signature and a hard-wired 2-channel model, so they do **not** compile
> against the channel-aware `SignalProcessor` (`process(in, channel)`). They are excluded from
> the synth build (`synth_dsp.h`) and from the unit/coverage suites. This README is the
> analysis + migration plan; no behaviour has been changed.

## What's here

- **`Positioner`** — places one source at a distance/angle: distance → propagation delay
  (speed of sound) and → inverse-distance gain. Owns an internal `Block[Delay → Gain]`.
- **`PositionSimulator`** — binaural-ish placement: two `Positioner`s (one per ear), an
  "acoustic shadow" low-pass per ear, and gain normalisation. Hard-coded stereo.
- **`RoomSimulation`** — image-source room model: enumerates wall reflections, drives a
  `PositionSimulator` per reflection, sums them, and adds a per-channel `Reverb`.
- **`util/Coordinate`** — a 3D point value type (distance/angle helpers).

## SOLID analysis

### 1. Liskov / contract drift (the blocker)
`Positioner::process(Sample in)` overrides the **old mono** virtual. The 1.0 base declares
`process(Sample in, int channel) = 0`, so `Positioner` no longer satisfies the base contract
and cannot be used polymorphically with any channel-aware graph. **Every override must move to
`process(Sample in, int channel)`** with per-channel running state (its `Delay`/`Gain` already
support channels). This is the prerequisite for everything else.

### 2. Single Responsibility (SRP) — these are god classes
- **`Positioner`** mixes four jobs: (a) geometry/physics (distance, speed-of-sound delay,
  inverse-square gain law), (b) parameter storage, (c) owning a DSP graph, (d) gain
  normalisation policy (`mKeepGain`). Split the **physics** (a pure `SourceGeometry`:
  position → {delaySamples, gain}) from the **DSP node** (applies delay+gain). The node then
  depends on a geometry value, not on trig.
- **`PositionSimulator`** does binaural HRTF-shadow modelling, per-ear graph wiring, gain
  normalisation, *and* an ad-hoc observer (`addGainListener`/`onCurrentGainChanged`). At least
  three responsibilities → three collaborators.
- **`RoomSimulation`** enumerates image sources, builds dozens of sub-blocks by hand, wires
  reverb, and mixes. The reflection **enumeration** (a list of image-source offsets) is pure
  data and should be a separate `ImageSourceModel`; the **graph assembly** is a separate
  concern from the **room geometry**.

### 3. Open/Closed (OCP) & Dependency Inversion (DIP)
- **Hard-coded magic constants** bake the model into the class: `SPEED_OF_SOUND`,
  `STANDARD_DISTANCE`, `EAR_DISTANCE`, `CUTOFF_FREQUENCY`, `MAX_RATIO_DEGREE`,
  `MIN/MAX_SHADOW_RATIO`. Changing the acoustic model means editing the class. Lift these into
  an injected config/strategy (e.g. an `AcousticModel` with the speed of sound and shadow
  curve) so new models are new objects, not edits.
- **`CHANNEL_COUNT = 2`** and `[2]` arrays everywhere hard-wire stereo. This contradicts the
  library's "one config authority" rule — channel count must come from
  `AudioConfig::channelCount()` and resize in `onChannelCountChanged()`, exactly like every
  migrated module. As written these classes break for mono or >2 channels.
- The simulators **`new`-wire concrete `Block`/`Gain`/`LowPassFilter`/`Positioner` graphs in
  their constructors** — they depend on concretes, not abstractions, and the topology is fixed
  at construction (can't be reconfigured or tested in isolation).

### 4. Interface Segregation (ISP)
`PositionSimulator` and `RoomSimulation` implement `IPropertyChangeListener::onPropertyChange()`
with an **empty body** — they're forced to depend on an interface method they don't use. Either
they genuinely need change notifications (then implement them) or they shouldn't implement the
interface.

### 5. Inconsistent abstraction (Liskov, again)
`PositionSimulator` and `RoomSimulation` are **not** `SignalProcessor`s — they expose
`getFilter(channel) -> SignalProcessor&` instead of *being* processors. Every other module in
the library is a `SignalProcessor`, so these can't drop into a `Block` chain. Make them
`SignalProcessor` (or `Block`) subclasses so they compose like everything else.

## Migration plan (in order)

1. **Re-channel the base node.** `Positioner::process(in)` → `process(in, channel)`; per-channel
   delay/gain state sized from `AudioConfig::channelCount()` in `onChannelCountChanged()`.
2. **Extract physics.** Pull distance→delay/gain and the shadow curve into pure, unit-testable
   helpers (`SourceGeometry`, `AcousticModel`) that hold the (now injected) constants.
3. **Make the simulators `SignalProcessor`s** (or `Block`s) and drive channel count from
   `AudioConfig` instead of `[2]`.
4. **Drop the empty `IPropertyChangeListener`** or implement it for real.
5. **Re-enable in the build + add tests.** Add `src/spatial/*` back to `buildSynthTests.sh` /
   `CMakeLists.txt`, write unit tests (geometry math, per-channel independence) to 100%, and a
   Python integration scenario (e.g. a source panned hard-left has earlier/louder L than R).

Until step 1 lands, `spatial/` stays out of the build.
