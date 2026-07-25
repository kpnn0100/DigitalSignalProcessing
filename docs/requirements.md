# Arstro DSP — Requirements

The requirement surface for this repo (`arstro.dsp.implement` skill, rule 5). Every
implementation task traces to an ID here. If code and this file disagree about intent, this
file is wrong and must be corrected — never silently implement something the text doesn't say.

Format: `REQ-<module>-<n>` · one-line statement · **Source**.

---

## Existing modules (seeded, high-level — see `docs/design.md` Layers table and each
`src/*/README.md` for the full per-module contract; not re-derived line-by-line here)

- `REQ-base-1` — One config authority (`AudioConfig::instance()`); no processor stores its own
  sample rate/channel count/buffer size. **Source:** `docs/design.md` SOLID rules.
- `REQ-base-2` — Parameters/targets are shared across channels and smoothed once per frame;
  running state (filter memory, phase, delay lines) is per-channel, resized in
  `onChannelCountChanged()`. **Source:** `docs/design.md`, `src/base/SignalProcessor.h`.
- `REQ-base-3` — New behavior is added by subclassing `SignalProcessor` / `SignalGenerator` /
  `Block`; the base contract is closed for modification (OCP). **Source:** `docs/design.md`.
- `REQ-synth-1` — 8-voice fixed polyphony, oldest-voice stealing by sample-count age (not
  wall-clock). **Source:** `src/synth/VoiceManager.h`.
- `REQ-synth-2` — Effects are applied once to the *summed* voice output (one shared chain:
  Compressor → Overdrive → Chorus → Repeater → Reverb), not per-voice. **Source:**
  `src/synth/README.md`, `src/synth/SynthEngine.cpp`.
- `REQ-effects-1` — New effects compose existing primitives where the behavior is fully
  described by wiring them (e.g. Chorus = per-channel `Delay` + internal LFO), rather than
  reimplementing delay/oscillation math. **Source:** `src/effects/Chorus.cpp`,
  `arstro.dsp.implement` rule 1.

---

## Piano physical-modeling voice (this task)

**Source for all `REQ-piano-*` below:** user request "implement me a piano core engine
inherit from SignalGenerator ... implement all 7 anatomy", referencing the 7-stage piano
anatomy: (1) hammer-string excitation, (2) string vibration, (3) bridge/soundboard, (4)
sympathetic resonance, (5) dampers, (6) pedals, (7) secondary mechanical noises.

- `REQ-piano-1` — A `PianoVoice` class inherits `SignalGenerator` (not `SignalProcessor`
  directly), fits the existing property/channel/envelope contract, and is playable via
  `noteOn(velocity)` / `noteOff()` at a given `setFrequency(hz)`.
- `REQ-piano-2` — **Hammer-string excitation** is a nonlinear, hysteretic felt-contact model
  (not a fixed excitation curve/table): loading and unloading follow different force laws so
  contact dissipates energy, and contact force depends on impact velocity.
- `REQ-piano-3` — **String vibration** is represented by a bank of inharmonic partials (stiff
  string: partial frequencies deviate from exact harmonics per an inharmonicity coefficient),
  each partial independently damped (frequency-dependent loss), and each note has 2+ detuned
  unison strings so beating and multi-stage decay emerge from superposition, not from a
  hand-authored decay envelope.
- `REQ-piano-4` — **Strike position** (where the hammer hits the string) measurably shapes
  timbre: partials near multiples of `1/strikePositionFraction` are suppressed, matching the
  real comb-filter effect of off-center excitation.
- `REQ-piano-5` — **Bridge/soundboard coupling**: all strings feed a shared resonant
  bridge/soundboard stage (not radiated directly); this stage is the last stage before the
  voice's audible output and is itself a small modal resonator bank, not a hand-tuned
  filter with no physical basis.
- `REQ-piano-6` — **Sympathetic resonance**: energy present at the shared bridge/soundboard
  measurably re-excites *other* strings whose partials are near that frequency, without a
  hand-authored per-note-pair coupling table (coupling emerges from shared-bus feedback into
  frequency-selective resonators).
- `REQ-piano-7` — **Damper model**: on `noteOff()` (with sustain not held), string damping
  increases over a short, non-instantaneous engagement window (not an instant mute), audibly
  shortening decay versus sustain-held or actively-playing notes.
- `REQ-piano-8` — **Pedals**: sustain (holds dampers off for all voices), sostenuto (holds
  dampers off only for voices already sounding at the moment it's engaged), and una corda
  (audibly softer/mellower tone via reduced hammer excitation) are all controllable and each
  produces a measurable change in the rendered signal.
- `REQ-piano-9` — **Secondary mechanical noises**: a hammer-strike transient (broadband
  "thump") and a damper-engagement transient are present in the output, distinct from the
  tonal partials, at low amplitude relative to the note.
- `REQ-piano-10` — A test harness (CLI + WAV render, following the existing
  `apps/kitchen_sink` pattern) lets a human audition the voice's sound without needing the
  full `SynthEngine`/ParamId wiring.
- `REQ-piano-11` — Every equation used by the implementation (hammer contact ODE, partial
  frequency/damping formulas, resonator discretization, damper ramp, pedal effect) is written
  down in `src/physical/README.md` **before** being coded, per `arstro.dsp.implement` rule 2/3.

### Explicit scope boundaries (not omissions — decided and written down so they can't be
silently reintroduced as bugs or silently promised as done)

- `REQ-piano-12` — Polyphony for the piano voice is bounded (matches `REQ-synth-1`'s existing
  8-voice convention in the demo engine); simultaneous full 88-key coupling is out of scope.
  **Consequence, found during implementation:** a bounded pool means voices *will* be reused
  for new notes. `StringResonator`/`StringPartialBank` expose `reset()` (zeroes resonator
  history), which `PianoVoice::noteOn()` always calls before retuning — reusing a voice
  without resetting first feeds stale history through newly-set coefficients and produces a
  large spurious transient (measured and fixed during `apps/piano_demo` integration; covered
  by the existing unit tests since `noteOn()` is exercised everywhere). `PianoEngine`'s voice
  allocator also prefers a never-used voice over reusing an active one before falling back to
  age-based stealing, for the same reason.
  **Second consequence, found via `examples/piano` interactive play (user report: "without
  sustain on, the sound is broken... right now all notes just have the same sound"):**
  resetting history isn't sufficient on its own — `reset()` must also recompute each
  partial's *decay coefficients* from the cleared damper value. `PianoEngine::noteOnMidi()`
  calls `setFrequency()` (recomputes coefficients from the *current* damper state) before
  `noteOn()` (where `reset()` runs), so a voice reused from a previously fully-damped note
  briefly carried that note's short-decay/high-input-gain coefficients into the new strike —
  measured up to a ~20–40× amplitude spike, reproducible without sustain (damper engaged)
  and absent with sustain held (damper never engages) or with the shared `PianoBridge`
  disabled (confirms this is single-voice, not a sympathetic-coupling instability). Fixed:
  `reset()` now ends by recomputing effective decay from the just-zeroed damper value; see
  `src/physical/README.md` §7's addendum for the full mechanism. Regression coverage:
  `PianoVoice_reused_voice_after_damper_engaged_stays_bounded` (synthTests.cpp),
  `StringPartialBank_reset_recomputes_decay_coefficients` (coverageTests.cpp),
  `piano_voice_reuse_bounded` (tests/run_integration.py). Separately, sympathetic feedback
  (`REQ-piano-6`) is now also gated by `(1 − damper)` in `PianoVoice` — a real damper mutes
  a string's response to any driving, not only its own decay — see `README.md` §8's addendum;
  this alone did not fix the reported bug but is a genuine physical-correctness improvement
  found during the same investigation.
- `REQ-piano-13` — The physical simulation (hammer + string + bridge state) runs once per
  output frame (computed on channel 0, replicated to other channels) — the voice is a mono
  physical source. Stereo width, if added later, belongs at the bridge/output stage (as
  `Reverb` already does for its own width), not by duplicating hammer/string physics per
  channel.
- `REQ-piano-14` — Room acoustics / radiation directivity / convolution with a measured
  impulse response are out of scope; the bridge/soundboard stage (`REQ-piano-5`) is the final
  physical stage modeled.
- `REQ-piano-15` — Longitudinal string vibration and phantom partials are out of scope (a
  documented sub-detail of `REQ-piano-3`'s "string vibration" stage, not a missing anatomy
  stage in itself).
- `REQ-piano-16` — Una corda's real mechanism (hammer shifts to strike fewer of the unison
  strings) is approximated as a reduced hammer excitation gain + reduced contact hardness,
  not literal dynamic unison-count switching mid-performance.

### Performance

- `REQ-piano-17` — The piano voice must render **faster than real time by a factor of ≥ 4**
  with the full `PianoEngine` voice pool (8 voices) sounding at 48 kHz, measured offline on
  the development machine. **Source:** `docs/piano-physics-plan.md` §M0 — the interactive
  `examples/piano` app drives a live ALSA thread, so anything approaching 1× real time risks
  audible underruns, and the planned physics upgrades (M2 raises the partial count ~5×, M4
  doubles resonators on low partials) multiply the cost. This is a *gate*, not an aspiration:
  a milestone that breaches it is not done until it is optimised or its scope is cut, with
  the decision recorded in `docs/piano-physics-progress.md`. Measured by `tools/piano_bench.cpp`
  (target `piano_bench`); each milestone re-runs it and records the figure in the ledger.
