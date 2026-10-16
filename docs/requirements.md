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
  (not a fixed excitation curve/table): contact dissipates energy, and contact force depends on
  impact velocity.
  **Amended 2026-07-27 (M9.3) — the hysteresis is a fading-memory relaxation, not a
  load/unload branch.** The original wording ("loading and unloading follow different force
  laws") described the M0 stand-in: a hard `v ≥ 0`/`v < 0` stiffness switch, whose energy loss
  is a fixed fraction independent of how the note was struck. Real felt is **rate- and
  history-dependent** (Stulov): a single force law `F = K·(uᵖ − ε·h)` where `h` is a
  relaxed (low-passed) memory of past compression `uᵖ` with time constant `τ`
  (`dh/dt = (uᵖ − h)/τ`). Loading outruns the memory (stiff); unloading is opposed by it
  (softer, dissipative); and because the memory fills on the timescale `τ`, the size and shape
  of the hysteresis loop **depend on the strike speed** — which the branch model cannot do. Still
  a normalised signal-level model (not SI-calibrated felt), still dissipative, still
  velocity-dependent. See README §6.
- `REQ-piano-3` — **String vibration** is represented by a bank of inharmonic partials (stiff
  string: partial frequencies deviate from exact harmonics per an inharmonicity coefficient),
  each partial independently damped (frequency-dependent loss), and each note has 2+ detuned
  unison strings so beating emerges from superposition. **Multi-stage (double) decay must
  likewise emerge from the superposition of physically distinct vibrational modes, never from
  a scripted amplitude envelope.**
  **Amended 2026-07-25 (M4).** The original text named *unison detuning* as the mechanism for
  multi-stage decay. Measured, it is not: two strings 0.6 cents apart produce audible beating
  but essentially a single decay slope, because both unison strings couple to the bridge the
  same way. The real mechanism is the string's **two transverse polarisations** — vertical
  (in the hammer's plane, strongly bridge-coupled, fast decay) and horizontal (weakly coupled,
  slow decay) — whose superposition gives the prompt-sound → aftersound envelope. The
  requirement's *intent* (emergent, not hand-authored) is unchanged and still binding: the
  polarisation split adds a second physical degree of freedom with its own decay, not a
  hand-drawn envelope over the output. Unison detuning keeps its (real) role: beating.
  **Amended again 2026-07-26 (M6).** "Each note has 2+ detuned unison strings" is now false for
  the bottom octave *by design*: `README ## 11.5` grades the unison count the way a real
  stringing scale does — 1 (single wound) for A0…A1, 2 for A#1…F2, 3 above. Requiring 2+ strings
  on every note would require the model to be wrong about the bass, where real pianos are
  single-strung. The requirement now reads: **notes are strung 1–3 per note following the
  instrument's stringing scale, and where 2+ strings are present they are detuned so beating
  emerges from superposition.** The single-strung bass is not left without multi-stage decay —
  the two polarisations above supply it, which is exactly the division of mechanisms this
  requirement's previous amendment established.
- `REQ-piano-4` — **Strike position** (where the hammer hits the string) measurably shapes
  timbre: partials near multiples of `1/strikePositionFraction` are suppressed, matching the
  real comb-filter effect of off-center excitation.
- `REQ-piano-5` — **Bridge/soundboard coupling**: all strings feed a shared resonant
  bridge/soundboard stage (not radiated directly); this stage is the last stage before the
  voice's audible output and is itself a small modal resonator bank, not a hand-tuned
  filter with no physical basis.
  **Note 2026-07-27 (M10).** Through M9 the "not radiated directly / last stage" clause was
  only *partly* met — the raw string was output directly and the board added in parallel. M10
  adds a broadband **radiativity** path to the board and a `body` mix routing the string through
  it in series, so the board can be (at `body = 1`) the actual last stage. `body` is a live
  control; its default is the largest value keeping the M1–M9 acceptance criteria intact, with
  the rest left to the ear (README §8.2).
- `REQ-piano-6` — **Sympathetic resonance**: energy present at the shared bridge/soundboard
  measurably re-excites *other* strings whose partials are near that frequency, without a
  hand-authored per-note-pair coupling table (coupling emerges from shared-bus feedback into
  frequency-selective resonators).
- `REQ-piano-7` — **Damper model**: on `noteOff()` (with sustain not held), string damping
  increases over a short, non-instantaneous engagement window (not an instant mute), audibly
  shortening decay versus sustain-held or actively-playing notes.
  **Amended 2026-07-27 (M9.1) — top-octave exception.** A real piano has no dampers on the top
  ~1.5–2 octaves, so this applies only *below* a cutoff `f_nodamp = √(f(88)·f(89)) ≈ 1357 Hz`
  (geometric mean of MIDI 88/89): a note above it has no damper and **always rings**, its
  `noteOff()` engaging neither the damper ramp nor the damper-release noise. This is a
  refinement of the model, not a reversal — the damped majority still behaves exactly as above.
  See README §7.1.
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
  (**AMENDED (REQ-inst-piano-1, 2026-10-15):** 16 voices in the library's `PianoEngine`, stolen
  longest-released first, never-struck voices seated — see REQ-inst-piano-1.)
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
- `REQ-piano-15` — ~~Longitudinal string vibration and phantom partials are out of scope (a
  documented sub-detail of `REQ-piano-3`'s "string vibration" stage, not a missing anatomy
  stage in itself).~~
  **Amended 2026-07-26 (M7) — now IN scope, and required.** The original text was written
  before the model had been listened to. The user's report after M0–M6 is that bass notes still
  read as a *bass-guitar-like* plucked string rather than a piano: the low register lacks the
  metallic **growl/clang** that is one of the most recognisable things about a real piano's
  bottom octaves. That character is not a refinement of the transverse series — it is a
  physically distinct family of modes, so no amount of tuning `REQ-piano-3`'s stage can produce
  it, and calling it a "sub-detail" of that stage was the mistake.
  The requirement now reads: **each string carries longitudinal modes at
  `f_long,m ≈ m·(1/2L)·√(E/ρ)`, driven by tension modulation — which depends on the *square* of
  transverse displacement, so the coupling generates energy at `2·f_i` and `f_i ± f_j`. The
  resulting partials must be genuinely *phantom*: measurable at frequencies where the
  transverse series `f_n` predicts no partial at all, growing with strike velocity, and absent
  at near-zero velocity.** As with `REQ-piano-3`'s multi-stage decay, the intent is that this
  emerge from a nonlinear coupling between two physical mode families — never from added
  synthetic partials at hand-picked frequencies. Strong in the bass and negligible in the
  treble, as on a real instrument.
- `REQ-piano-16` — ~~Una corda's real mechanism (hammer shifts to strike fewer of the unison
  strings) is approximated as a reduced hammer excitation gain + reduced contact hardness,
  not literal dynamic unison-count switching mid-performance.~~
  **Amended 2026-07-27 (M9.4) — now the real mechanism, not the gain approximation.** The
  gain-reduction stand-in (`kUnaCordaGain`) is replaced by modelling what the soft pedal
  physically does: the hammer shifts laterally and strikes a **subset** of the note's unison
  strings — a trichord's 3→2, a bichord's 2→1, a single-strung bass note unchanged. The
  **un-struck string is then driven only through the shared bridge** (sympathetically,
  `REQ-piano-6`), not by the hammer — silent entirely if there is no bridge. The audible "softer"
  now emerges from coupling the hammer to fewer strings (its energy is shared over the nominal
  unison count but delivered to a subset, so the directly-driven energy is `S/U` of normal —
  measured 0.66× for a trichord); the softer-felt contact (`kUnaCordaStiffness`) is retained as a
  real effect. `kUnaCordaGain` is **deleted**, as `voicingGain`/`registerGain` were once their
  real physics arrived. Applied at note-on (which subset is struck is fixed for the note), not
  switched mid-sustain. **Documented limitation:** the model reproduces "softer" but not the real
  instrument's *mellowing* — dropping a unison string removes low-frequency chorusing, which
  slightly outweighs the softer felt, so the net timbre is marginally brighter. Faithful mellowing
  would need strong inter-unison bridge coupling (out of scope here). `REQ-piano-8`'s
  "softer/mellower" is satisfied by the softer half. See README §9.
- `REQ-piano-18` — **Tension modulation (attack pitch glide).** A struck string's transverse
  motion raises its average tension, and tension sets pitch, so a note must start slightly
  **sharp** and glide down to its nominal pitch as the vibration decays. **Source:**
  `docs/piano-physics-plan.md` §M8. The rise must (a) be driven by the *square* of the
  transverse amplitude — the same slope-square quantity `REQ-piano-15` couples to the
  longitudinal modes, whose static (DC) part is exactly this tension rise — so the glide grows
  with strike velocity and vanishes at near-zero velocity; (b) shift every partial by the same
  fraction (a uniform re-tune of the series, not a detune of the fundamental alone); and (c) be
  strong in the bass and negligible in the treble, as on a real instrument. Like
  `REQ-piano-3`'s multi-stage decay and `REQ-piano-15`'s phantom partials, the intent is that
  the glide *emerge* from the tension–amplitude physics, never from a scripted pitch envelope.
  Added 2026-07-27 (M8) — no requirement previously covered it (rule 5 requires a written
  requirement before the work; there is no conflict, only a gap).
- `REQ-piano-19` — **Duplex / aliquot scale (treble shimmer).** The short, un-struck string
  segments beyond the bridge (front/rear duplex), tuned on the treble so their pitch coincides
  with an upper partial of the speaking string, must ring **sympathetically** — driven by the
  speaking string through the shared bridge, never struck directly — and add a high, sustained
  shimmer that (a) carries energy at the tuned aliquot frequencies, (b) **sustains past** the
  bridge-damped speaking partial at that frequency (higher `Q`), and (c) is present in the treble
  and negligible in the bass. **Source:** `docs/piano-physics-plan.md` §M9.2. Like the
  longitudinal/tension mechanisms it must *emerge* from a driven resonant segment, not be
  painted on as synthetic partials at hand-picked frequencies. Added 2026-07-27 (M9.2) — no
  requirement previously covered it (a gap, not a conflict).

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

## Parallel & accelerated compute

**Source:** user request (2026-07-26) — "suggest me architecture for using multithreading in DSP
lib and gpu computing that if someone want to adapt to their platform only need to implement the
adapter; for now implement for linux", prompted by the interactive app feeling laggy. Design and
the measurements behind it: [`parallel-architecture.md`](parallel-architecture.md).

- `REQ-compute-1` — **Latency and throughput are separate requirements.** Perceived lag is set by
  the output buffer, not by DSP cost; measured, the engine used 17–20 % of the real-time budget at
  every block size while the app still lagged (a 50 ms ALSA buffer). Parallelism is therefore
  specified as *headroom for growth*, never as the remedy for latency, and no parallel work may be
  justified by a latency argument.
- `REQ-compute-2` — **The porting seam is `base/platform/Platform.h` and stays there.** Adapting
  the library to a new platform must require implementing `platform::Thread` (and `Mutex`) and
  nothing else. No engine, module, or the compute layer itself may contain an OS call. A platform
  with no threads at all must remain fully functional via the serial executor.
- `REQ-compute-3` — **Parallel rendering must not change the audio.** Rendering a given input
  through the serial executor and through any parallel executor must produce **bit-identical**
  output. This requires shards over disjoint state and summation in fixed shard order (float
  addition is not associative, so completion-order summation would make output depend on thread
  timing). Verified by a unit test that runs both executors in one binary and compares exactly.
- `REQ-compute-4` — **Nothing on the audio thread allocates or blocks unboundedly.** The
  executor's dispatch path takes a plain function pointer plus a `void*` context (not
  `std::function`, which heap-allocates when it captures) and performs no allocation.
  **Amended 2026-07-26 during implementation.** The original text also said the dispatch path
  "takes no mutex", extending `docs/design.md`'s "audio thread never locks" rule to fan-out/join.
  That was written before the handshake was built, and it is the wrong rule for this path. An
  all-atomic handshake was implemented first and proved subtly racy: a worker waking late for job
  N would claim against job N+1's cursor with a stale snapshot, and its final failed claim still
  advanced the cursor, skipping a shard of the new job so `run()` waited forever. The requirement
  now reads: **shard claiming — the hot path, executed once per shard — must be lock-free; the
  dispatch/join handshake may take a short mutex, provided it is never held while DSP work runs
  and never held by a thread that can be descheduled mid-computation.** The design rule it
  extends is unchanged in intent — the audio thread must never wait on a lock held by a slow
  or lower-priority thread — and this handshake cannot, because every critical section is a
  handful of integer operations.
- `REQ-compute-5` — **Parallelism is opt-in and bounded by the dependency graph.** The default
  executor is serial. Workers are clamped to `min(shardCount, hardware_concurrency)` because
  Amdahl's law yields nothing beyond the shard count (measured serial fraction `s = 0.133` for the
  piano graph → a 7.5× ceiling however many cores are added). Below a documented block-size floor
  (~64 frames) fan-out overhead exceeds the gain and the executor must run serially.
- `REQ-compute-6` — **GPU/accelerator support is specified as a separate, narrower interface, and
  is not claimed until it is testable.** A GPU cannot execute a C++ function pointer, so it is not
  served by `ParallelExecutor`; it requires a fixed kernel contract (advance M resonators over B
  samples). No accelerator backend ships without a toolchain to compile and test it — this machine
  has an integrated GPU but no OpenCL/CUDA/Vulkan SDK, so §4 of the design doc records the
  analysis, the attack/decay decomposition that would make it viable, and the verdict that this
  workload is latency-bound rather than throughput-bound. An untested backend is worse than none,
  because it looks like a feature.

---

## Solaris instruments & effects — D1: filters and primitives

**Source for every entry below:** the user's request of 2026-10-08 for the Solaris DAW — *"make me
some basic instruments: Drum Machine, Basic Synth with 2 OSC; and some basic filters: Compressor,
EQ, Reverb, …; make sure all the core of those instruments and basic filters is in dsp"*
(Solaris `R-DSP-1`, `R-INST`, `R-FX`, in `apps/solaris/REQUIREMENTS.md` of the umbrella).

- `REQ-eq-1` — A `Biquad` implements the RBJ cookbook's seven shapes (low-pass, high-pass,
  band-pass with 0 dB peak, notch, peaking, low shelf, high shelf) as one TDF-II section with
  per-channel state; frequency, Q and gain are smoothed properties so a sweep does not click; its
  closed-form magnitude response is available for curves and tests. Math: `src/equalizer/README.md`
  (B1)–(B10).
- `REQ-eq-2` — A `ParametricEQ` is seven switchable Biquad bands in series (low cut, low shelf,
  three peaks, high shelf, high cut), **transparent when fresh** (every gain 0 dB, cuts off).
  Composition only — no new math.
- `REQ-svf-1` — A `StateVariableFilter` gives low-, band-, high-pass and notch from one TPT state,
  resonance 0..1 mapped to Q 0.707..32, stable at full resonance; its response equals the
  bilinear-prewarped analog prototype. Math: (S1)–(S6).
- `REQ-svf-2` — The SVF can be swept every sample with a caller-supplied cutoff (`tick`), with no
  property smoothing in the way — the synth's filter envelope and the drum voices need it.
  (**AMENDED (Solaris D-4, 2026-10-15):** "no smoothing" holds for the resonance too — `tick` rings with the
  resonance written LAST, at once. It read the smoothed value, which only the block path advances, so a
  resonance set after the filter was made never arrived: the Basic Synth's Resonance did nothing (two renders
  at 0 and 0.9 were identical). Guarded by `SVF_tick_follows_a_resonance_set_after_it_was_made` and
  `BasicSynth_resonance_is_heard`.)
- `REQ-noise-1` — `Noise` is seeded, deterministic (same seed → same samples everywhere), uniform
  on [−1, 1); no clock, no global state.
- `REQ-decay-1` — `DecayEnvelope`: an optional linear attack from the current level, then an
  exponential fall whose `decay` is the time to −60 dB; finished below −80 dB; can be choked.

## Solaris instruments & effects — D2: instruments

**Source:** the same request (Solaris `R-INST-1/2`).

- `REQ-inst-1` — An `Instrument` interface for a host: `noteOn(note, velocity)`, `noteOff`,
  `allNotesOff`, `reset`, `render` that ADDS into the host's buffers, `activeVoices`. No time stamps:
  the host splits blocks at note events for sample accuracy. Deterministic from a `reset()`.
- `REQ-synth2-1` — `BasicSynth`: two oscillators (waveform, octave, semitone, cents, level, unison
  voices + detune) and noise → one resonant filter (LP/BP/HP/notch) → an amplitude ADSR, in that
  order; 16 voices. A composition of `Oscillator`, `StateVariableFilter`, `ADSREnvelope`, `Noise`.
- `REQ-synth2-2` — Pitch is equal-tempered from A4 = 440 Hz, moved by each oscillator's octave,
  semitones and cents (instrument/README.md (Y1), (Y2)).
- `REQ-synth2-3` — The filter has its own ADSR; the envelope and key tracking move the cutoff in
  octaves (Y3), clamped to 20 Hz…20 kHz.
- `REQ-synth2-4` — Level = volume × velocity scaled by a sensitivity (Y4); the release tail is never
  cut (the oscillators' own envelopes are gates closed only when the amp envelope ends).
  (**AMENDED (Solaris D-8, 2026-10-15):** the velocity is the note's, set at its start; the VOLUME is applied as
  each block renders, ramped linearly across the block from where the last ended — set or automated while a note
  sounds, it moves that note, without a step (it was baked into the note at its start: a riser's swell, one long
  note, was silently ignored). The Sampler's level likewise (REQ-inst-sampler-1). Guarded by
  `Volume_and_level_move_a_sounding_note` — a stepped volume seen red.)
- `REQ-synth2-5` — Voice allocation: same note retriggers its voice, else an idle voice, else the
  oldest note is stolen (Y5).
- `REQ-inst-expr-1` — **The wheels** (source: Solaris R-MIDI-3, 2026-10-16): `Instrument` / `Device` take
  `pitchBend(−1…+1)` and `modWheel(0…1)`, between blocks; each instrument its own range and meaning — the Basic
  Synth bends ± `voice.bend` semitones (default 2) and the wheel is its vibrato; the Sampler bends its rate ± `bend`;
  a kit and the piano ignore both. Guarded by `Synth_pitch_bend_moves_every_note_by_its_range`,
  `Synth_mod_wheel_is_vibrato`, `Sampler_bends_its_rate`, `Expression_reaches_the_instruments_through_the_registry`,
  `glide_and_bend`.
- `REQ-synth2-7` — **Bend and vibrato in the Basic Synth** (Solaris R-MIDI-3): (Y9) on an absolute control grid
  (Y7), so a render is the same however a host chops time (`Synth_expression_is_the_same_however_time_is_chopped`);
  no allocation (`Expression_does_not_allocate`). Parameters appended (`voice.bend`, `vibrato.depth`, `.rate`).
- `REQ-synth2-8` — **Mono / legato and glide** (Solaris R-INST-6): `voice.mode` poly · mono · legato, `voice.glide`
  ms — one voice, the keys held as a stack, a constant-time slide linear in semitones; legato does not strike again
  while a key is held (Y8), (Y10). Guarded by `Synth_legato_glides_without_a_new_attack` (the midpoint to a control
  step; no new attack in legato, a new one in mono; back to the key held) and `glide_and_bend` (an independent
  zero-crossing measure: 220.0 → 309.8 (Eb4 311.1) → 440.0 Hz; bent 493.9).
- `REQ-synth2-6` — **Stereo unison** (source: Solaris R-INST-1 amended, user request 2026-10-15: *"make an edm
  song, edm require more advance sound"*). Each oscillator has a WIDTH in cents (`osc1/2.width`, 0…50, default
  0): the `Oscillator`'s per-channel detune offset, the channels detuned apart by it, so a unison spreads across
  the stereo field. Unison voices start at spread phases — voice v at frac(v/φ), the same on every channel —
  not all at zero (aligned detuned saws spike at the attack: 7.8× the sustained level, measured; spread 0.9×).
  No width is mono-identical; one voice starts at 0 as before. Guarded by
  `BasicSynth_unison_is_wide_and_does_not_flam` (the aligned start seen red).
- `REQ-drum-6` — **Crash 49 and ride 51** (source: Solaris R-INST-2 amended, user request 2026-10-15, the EDM
  song: the kit had no cymbal for a drop). Each with tune, decay, tone, level and pan; (K12) the crash, the hats'
  six squares and more noise through a 3.5 kHz band and a 5 kHz sheen, a burst over a long wash (1800 ms); (K13)
  the ride, a two-square bell in a 3 kHz band (decay/3) over a metal-and-noise wash above 8 kHz (1200 ms). Last in
  the pad order, so every older pad keeps its index and its keys. Guarded by
  `DrumMachine_crash_and_ride_ring_as_cymbals` (level, T60 within 15 %, the crash bright, the ride's bell) and the
  every-pad test.
- `REQ-drum-1` — `DrumMachine`: ten pads on the GM drum notes (kick 36, rim 37, snare 38, clap 39,
  low tom 41, closed hat 42, mid tom 45, open hat 46, high tom 48, cowbell 56), each synthesized
  from `Phasor`, `Noise`, `StateVariableFilter`, `DecayEnvelope` — no sample files. A `Phasor`
  primitive is added because a per-sample pitch sweep through `Oscillator` costs ten `pow`s a sample.
- `REQ-drum-2` — The kick and toms sweep their pitch down exponentially to a fundamental set by
  `tune` (kick: 48·2^(tune/12) Hz) (K1), (K2).
- `REQ-drum-3` — Each pad has tune (semitones), decay (T60, ms), tone (0..1, per-pad meaning), level
  (dB), pan (balance law, unity at centre).
- `REQ-drum-4` — The closed hat chokes the open hat (−60 dB in 8 ms) (K6).
- `REQ-drum-5` — Deterministic: every pad's noise is seeded from its index and `reset()` reseeds,
  so a pattern from a reset renders the same bytes.

## Solaris instruments & effects — D3: the device registry

**Source:** the same request (Solaris `R-DSP-2`, `R-FX-1…5`).

- `REQ-device-1` — Every instrument and effect has ONE description, a `DeviceType` in the
  `DeviceRegistry`: name, label, kind, summary, and per parameter its name, label, unit, range,
  default, choices and integer/log hints. Instruments' defaults are read from the instruments.
- `REQ-device-2` — A `Device` is the uniform face: `process` (effect in place, instrument adds),
  notes, `reset`, `activeVoices`, parameters by index or name in engineering units. Every write is
  clamped/rounded per its spec, a non-finite value becomes the default, an unknown index or name
  changes nothing.
- `REQ-device-3` — The registry holds: `synth`, `drums`, `compressor`, `eq`, `reverb`, `delay`,
  `chorus`, `drive`, `filter`. The effects wrap the existing modules unchanged.
- `REQ-device-4` — Robust at every corner: every parameter of every type at its minimum and at its
  maximum renders finite samples.
- `REQ-device-5` — **No allocation on the audio path.** Once a device has processed one block (a
  host warms it), `process`, `noteOn` and `noteOff` allocate nothing — a host's audio thread must
  not (Solaris R-PLAY-2). `BasicSynth` sizes its per-channel noise generators and a 4096-frame
  scratch block in its constructor. Guarded by a counting `operator new` in the unit-test binary
  (`BasicSynth_render_does_not_allocate…`, `Device_process_does_not_allocate_once_warm`).
- `REQ-device-6` — **A kit names its keys.** `DeviceType::noteNames` lists, ascending, the notes of an
  instrument whose keys mean something and what each is — the Drum Machine's ten pads ("Kick" 36 …
  "Cowbell" 56), taken from its own pad table, so a host's piano roll can label them; empty for a
  melodic instrument and for effects. Guarded by `DeviceRegistry_names_the_drum_kits_keys`.
- `REQ-device-7` — **A parameter's normalised and text faces are the library's, once** (source: Solaris
  R-VST-3/4, 2026-10-09). `normalizedFromValue` / `valueFromNormalized` map a spec's value to 0…1 and
  back — linear; logarithmic for a `logScale` spec with min > 0; DISCRETE for an integer or a choice by
  VST3's convention (steps = max − min or choices − 1; k / steps; back min(steps, ⌊n·(steps+1)⌋)) —
  and `paramToText` / `paramFromText` give its text (a choice's name, else the suite's canonical
  number: shortest round trip, at least one decimal). A VST3 plugin, Solaris's service and its `.slp`
  all use these, so they agree to the last digit. Guarded by `ParamSpec_normalised_and_text_faces_round_trip`
  (every registry parameter: discrete steps exact both ways and owning equal slices, continuous round
  trip < 1e-12, a log taper's middle at the geometric mean, the default's text exact, nonsense refused).

- `REQ-device-8` — **A device reports its latency** (source: Solaris R-MIX-17, the audit of
  2026-10-09: the master limiter's lookahead put a whole render 96 samples late). `Device::latency()` is
  the number of samples its output lags its input at its CURRENT parameters — the `limiter`'s lookahead
  L (`Limiter::latency()`, 96 at its 2 ms default and 48 kHz), 0 for every other type — so a host can
  delay every other path by it (plugin delay compensation). A write that moves it (`lookahead`) is seen
  by the next read. Guarded by `Device_reports_its_latency_and_lags_by_exactly_it` (every type's default,
  the limiter at 0 / 1 / 5 / 10 ms, and a click through the Device face arriving exactly `latency()`
  samples late).
- `REQ-device-9` — **A kit's keys are joined to their parameters** (source: Solaris R-VST-7, the Drum
  Machine editor's pads, 2026-10-09; and Solaris D-3: a pad was `closed-hat` to a note and `chat` to an
  address). `DeviceType::notePrefixes` lists, for the same keys as `noteNames` and in the same order, the
  prefix of the parameters that shape each (42 → `chat`: `chat.tune` … `chat.pan`), from the kit's own
  pad table (`DrumMachine::padName`); empty for a melodic instrument. A name is for a person, a prefix for
  an address — this joins them. Guarded by `DeviceRegistry_joins_a_kits_keys_to_their_parameters`.

- `REQ-device-10` — **An instrument's parameter groups are titled** (source: Solaris R-UI-12, user request
  2026-10-15: an analog face with sections). A group is the prefix before a parameter's dot, "" the
  parameters with none; `DeviceType::groupLabels` titles each (Basic Synth: Oscillator 1, Oscillator 2,
  Filter, Filter Envelope, Amplifier, Mix & Output; Drum Machine: each pad's name — the same join as
  REQ-device-9 — and Output; Sampler: Sampler). Every group of every instrument is titled. Guarded by
  `DeviceRegistry_titles_every_instruments_parameter_groups`.

## Solaris instruments & effects — D4: VST3 (Solaris R-VST, 2026-10-09)

- `REQ-vst-1` — **The SDK is outside the repo.** Steinberg's `vst3sdk` v3.8.1_build_84 (MIT licence,
  read at install) at `VST3_SDK_ROOT` (default `~/sdk/vst3sdk`); with none, `apps/vst3` says so and
  builds nothing, and `arstro_dsp` never depends on it. Its sources are compiled with this build's
  CMake (the SDK's own needs 3.25), from the SDK's own source lists.
- `REQ-vst-2` — **Basic Synth and Drum Machine are VST3 instruments wrapping the library's own
  classes** (`apps/vst3`: one generic processor + controller, built per registry type; bundles
  `ArstroBasicSynth.vst3`, `ArstroDrumMachine.vst3`; class ids frozen in `factory.cpp`).
- `REQ-vst-3` — **Their parameters are the registry's**: id = index in the type, title / unit / steps /
  default from the spec, mapped by REQ-device-7; a block is split at every note and parameter point at
  its sample offset; a note-on at velocity 0 is a note-off.
- `REQ-vst-4` — **State is text**: `arstro-device 1`, `type=<t>`, `<name>=<paramToText>` per parameter;
  another type's state is refused, an unknown name skipped.
- `REQ-vst-5` — **Verified**: the SDK's validator passes both (47/47 each, `vst3_validate_synth|drums`);
  `vst3_equivalence` hosts each bundle offline and equals the device rendered Solaris's way (warm-up
  included) sample for sample — every synth parameter moved, notes mid-block, the cutoff automated
  mid-block, 100-sample blocks — not silence, and its state reads back as the registry's text.
- `REQ-vst-6` — **An editor seam** (source: Solaris R-VST-7, user request 2026-10-09: *"make the UI follow
  Arstro theme"*). The plugins' own editor is drawn by Artboard in the Arstro look, which this repo does
  not carry, so it is the umbrella's (`apps/solaris/plugins`) and is compiled INTO the plugins there,
  with `ARSTRO_VST3_EDITOR`: the controller's `createView("editor")` returns `createEditor(*this)`
  (`apps/vst3/Editor.h`). Built from this repo alone there is no editor — `createView` returns nothing
  and a host draws its generic view — so this repo still needs no window system. The editor reaches
  the plugin only through `Controller`: `type()`, `plainValue(i)`, and `editBegin` / `editPerform` /
  `editEnd` in engineering units, `editPerform` normalising by REQ-device-7 before telling the host.
  `vst3_editorhost` (the SDK's editor host, X11) is built beside the validator to open an editor in a real
  host window. Verified there: the validator passes both plugins with the editor linked in, and the
  umbrella's `solaris_plugin_editor` drives the editor over this controller (a drag → beginEdit once,
  performEdit = `normalizedFromValue` of the value chosen, endEdit at release).
- `REQ-vst-7` — **The editor plays notes through the processor** (source: Solaris R-VST-7, the Drum
  Machine's pads). `Controller::playNote(pitch, velocity)` sends the message `arstro.note` (ints `pitch`,
  `velocity`; 0 = its note-off) through the host's connection; `Processor::notify` queues it — a
  single-producer, single-consumer ring of 64, no lock, a full ring drops it, a pitch outside 0–127 is
  refused — and `process` plays it at the next block's start, in the order played. Without a host to carry
  the message nothing is heard and nothing breaks. Guarded by `vst3_equivalence` (the hosted Drum Machine:
  silence without the message, the kick with it; a pitch of 300 refused) and the umbrella's
  `solaris_plugin_editor` (a pad clicked in the editor, the processor and the controller connected by the
  SDK's host classes: heard at the next block).
- `REQ-fx-sidechain-1` — **The Compressor can listen to a key** (source: Solaris R-MIX-15 / R-EDM-3,
  2026-10-09). With `sidechain` on (a registry parameter, APPENDED so existing ids hold) its detector
  follows a key handed in before each block (`Device::setKey`; `DeviceType::takesKey`), the gain still
  on its input; no key = silence = no reduction. (C1)–(C4) in `src/effects/README.md`. Guarded by
  `Compressor_sidechain_ducks_on_the_key_not_the_input` (22.5 dB exactly as (C3) says) and
  `sidechain_pump` (a kick dips a bass 20.6 dB; 0.09 dB with no key).
- `REQ-fx-limiter-1` — **A brickwall limiter** (source: Solaris R-EDM-4): gain, ceiling, release,
  lookahead (0…10 ms, its latency); channels linked; the output never above the ceiling BY
  CONSTRUCTION ((L1)–(L5)); a glide of at most 1/(L+1) a frame; transparent under the ceiling. Registry
  type `limiter`. Guarded by `Limiter_never_exceeds_its_ceiling_and_glides` and `limiter_ceiling`.
- `REQ-out-dither-1` — **TPDF dither** (source: Solaris R-EXPORT-1, 2026-10-16): `Dither` quantises float to a
  bit depth (2…24) with triangular dither of ±1 step — the error's mean 0 and power Δ²/4 whatever the signal; an exact
  zero passes (digital silence stays silence, (D0)); seeded;
  composed from `Noise`. (D1)–(D3), output/README. Guarded by the five `Dither_*` tests and `dither_triangular`.
- `REQ-fx-limiter-2` — **True-peak mode** (source: Solaris R-EDM-4a, 2026-10-16): `truePeak` off|on (off by
  default: a song's sound does not change). On, the ceiling holds BETWEEN samples: detected through the meter's
  interpolator at 8× (every ⅛ sample — both grids a 4× BS.1770 meter may use), each frame's gain also meeting
  its ±8 neighbours' needs; latency L + 16. Measured: the TruePeakMeter never more than 10⁻⁵ dB over, an
  independent 8× interpolator −1.001 dBTP for a −1 ceiling. (L6). Guarded by
  `Limiter_true_peak_mode_holds_the_ceiling_between_samples` and `limiter_true_peak`.
- `REQ-inst-piano-1` — **The piano, a library instrument** (source: Solaris R-INST-5, user request 2026-10-15:
  *"if the instrument is not enough, make that instrument"* — a song for the piano). `PianoEngine` — the
  `PianoVoice` pool sharing one `PianoBridge` (REQ-piano-8/12) — moves from `apps/piano_demo` into
  `src/physical`, so the demo, the bench and a host share ONE engine; `Piano` (`src/instrument`) is it as an
  `Instrument` (REQ-inst-1), and registry type `piano` gives a host its voicing bank (hammer, strings, body —
  the engine's `Tune` rows, same ranges and defaults), the sustain and soft pedals and a volume. **AMENDS
  REQ-piano-12:** the pool is 16 voices (silent ones cost nothing, README ## 13, so the bench's 8-sounding
  budget, REQ-piano-17, is unchanged), and a voice is stolen in this order — never used, finished, the
  longest-RELEASED, and only then the oldest held — so a held note is never taken while a released one
  still rings (an arpeggio over a held bass keeps its bass). `reset()` silences every voice without
  allocating; velocity is `v / 127`. Guarded by `Piano_*` unit tests and the registry's every-entry tests.

- `REQ-inst-sampler-1` — **A sampler** (source: Solaris R-EDM-8, 2026-10-09): registry instrument
  `sampler` playing a host-decoded recording (`DeviceType::takesSample`, `Device::setSample` — the library
  never opens a file); chromatic (pitched by the key from its root, and by the file's rate against
  ours) or one-shot (as recorded, to its end); a span, reverse, an ADSR, level, velocity sensitivity;
  16 voices. (S1)–(S5) in `src/instrument/README.md`. Guarded by
  `Sampler_plays_its_sound_pitched_spanned_and_reversed` and `sampler_pitch`.
- `REQ-inst-sampler-2` — **A sampler's slices** (source: Solaris R-EDIT-1 "a Sampler kit", 2026-10-16): mode
  `slices` cuts the span into `slices` equal parts (1..64, appended to the registry); the key root + k plays part k
  as recorded, to its end, a note-off ignored; a key outside them is silent. (S7). Guarded by
  `Sampler_slices_play_by_the_key` and `sampler_slices`.
- `REQ-onset-1` — **Onsets** (source: Solaris R-EDIT-1 `clip slice --transients`, 2026-10-16): `detectOnsets` finds
  where sounds begin in a whole recording — energy flux over a local median, a floor and a minimum gap, refined to
  the sample. (O1)–(O5) in `src/meter/README.md`. Guarded by `Onsets_are_found_where_sounds_begin` and
  `onsets_find_the_drums`.

- `REQ-tempo-1` — **A sound's tempo** (source: Solaris R-WARP-1 "its own BPM detected on import", 2026-10-16):
  `estimateTempo(mono, frames, rate, {minBpm, maxBpm, snap})` — a log-compressed, smoothed energy flux, its
  autocorrelation weighted toward 120 bpm, the peak between frames by a parabola; a loop within 0.5 % of a whole
  number of beats takes the tempo that makes it so; 0 when nothing rises. (B1)–(B5) in `src/meter/README.md`. Guarded
  by `Tempo_is_found` (133 → 132.95, 90 not 45 or 180, a 4-bar loop at 97 to its sample, a tone none) and
  `tempo_of_a_drum_groove` (the Drum Machine at 124 and 87 bpm, within 0.5).
- `REQ-fft-1` — **An FFT** (source: Solaris R-WARP-1, the time-stretcher's need, 2026-10-16): `FFT` — the DFT of 2^m
  points in place, forward and inverse, radix 2, twiddles computed once each; a size not a power of two is refused.
  (F1)–(F3) in `src/spectral/README.md`. Guarded by `FFT_is_the_DFT` (64 points against the sum to 1e-9, back to 1e-12).
- `REQ-stretch-1` — **A sound stretched, its pitch kept** (source: Solaris R-WARP-1, user request "implement all",
  2026-10-16): `timeStretch(channels, rate, {ratio})` — a phase vocoder (periodic Hann, overlap 4 or 8, the peak
  frequencies from the phases' travel over the actual hop, identity phase locking) making round(ratio·L) frames,
  ratio 0.25 … 4; at ratio 1 the sound itself. Deterministic. (P1)–(P5), (P7). Guarded by
  `Stretch_at_one_gives_the_sound_back` (to 1e-5), `Stretch_keeps_the_pitch` (440 Hz at ×1.5 and ×0.6 to 0.2 %, its level
  to 0.5 dB), `Stretch_is_the_same_twice`, `stretch_keeps_the_pitch`.
- `REQ-stretch-2` — **Its pitch moved, its length kept** (same source): `{semitones}` −24 … +24 — stretched 2^(s/12)
  further and read back that much faster through REQ-resample-1 (P8). Guarded by `Pitch_moves_and_the_length_stays`
  (+7 and −12 semitones to 0.2 %, the same length, the level to 0.5 dB), `pitch_moves_the_length_stays`.
- `REQ-stretch-3` — **Attacks kept** (same source — "with transient handling"): around each onset (REQ-onset-1, its
  sensitivity `transients`) the time map runs at slope 1 for up to half a window either side — the attack copied to
  ratio·onset, phases reset, no frame before it holding it. (P6). Guarded by `Transients_stay_where_they_begin` (bursts at
  twice their time within 1 ms, nothing of them in the 10 ms before: below −60 dB; a plain vocoder above −30 dB).
- `REQ-stretch-4` — **A time map** (source: Solaris R-TEMPO-1 — a warped clip under a tempo that changes, 2026-10-16):
  `StretchParams::map`, (output, input) frame anchors in place of a ratio; the attacks' zones placed where the map hears
  them, the stretch between them following the map's shape. (P9). Guarded by `Stretch_follows_a_time_map` (half a
  second as it was then the rest twice as long: 440 Hz in both; a swell heard at its mapped level, 0.25 and 0.875; bursts
  at 0.25 s and 1.0 s within 1 ms).
- `REQ-resample-1` — **A band-limited resampler** (same source): `resample(in, step, frames)` — a Kaiser-windowed sinc
  (16 zero crossings, β 8.6), its cutoff 1/step when reading faster. (R1)–(R3). Guarded by `Resample_is_band_limited`
  (a sine at half speed to 1e-4 in frequency and 0.05 dB; 5 kHz at double speed to 0.1 dB; 15 kHz, which would fold,
  below −60 dB).
- `REQ-wavetable-1` — **A wavetable read with nothing above Nyquist** (source: Solaris R-INST-7, the user's "implement
  all", 2026-10-16): `generator/Wavetable` — frames of 2048-point cycles, each band-limited by the FFT at nine levels
  of halving bandwidth (256 … 1 harmonics, 16 points a harmonic at least), read between two levels that both lie
  below Nyquist (never an alias), between points by Catmull-Rom and between frames linearly; one more waveform of
  `Oscillator` (`Table`), its position ramped a straight line per sample. (W1)–(W4), (W7). Guarded by
  `Wavetable_levels_keep_their_harmonics` (each level exactly its harmonics, one gain on all), `…_reads_with_nothing_
  above_nyquist` (a square, 32 harmonics, a hard-synced saw from 55 Hz to 14 kHz: under −76 dB off the harmonics;
  the fundamental the same at every pitch), `…_position_mixes_two_frames`, `…_position_ramps_a_straight_line`.
- `REQ-wavetable-2` — **Built-in tables, and tables from a sound** (same source): Basic, Pulse, Harmonics, Formant,
  Sync, Fold (32 frames each, from formulas, made once and shared); a mono sound cut into 2048-point cycles (at most
  256). (W5)–(W6). Guarded by `Wavetable_from_a_sound`.
- `REQ-wavetable-3` — **A wavetable synth** (same source): BasicSynth's oscillators may play a table (`Osc::table`,
  `position`, `posEnv` — appended, the Basic Synth plugin's ids unchanged), the position moved by the filter
  envelope on the 32-sample grid, a new note's from where its envelope starts; the registry's "wavetable" (two table
  oscillators, the built-ins or a sound given, `takesSample`). Guarded by `BasicSynth_plays_a_table_moved_by_its_
  envelope` (sine → square over a second: the third harmonic 0.008 → 0.333), `BasicSynth_plays_the_table_of_a_sound`;
  the registry's tests (every range, no allocation after warm-up); integration `wavetable_plays_its_note`.
- `REQ-fm-1` — **Four operators by phase modulation** (source: Solaris R-INST-8, "implement all", 2026-10-16):
  `FMSynth` — each operator a sine at its ratio of the note (and fine cents), its phase moved by the operators above it
  (index 8 rad at level 1), its own ADSR and level, velocity-sensed; every operator from phase 0 at a note (key sync);
  pitch on the 32-sample grid (a render the same however chopped). (M1), (M2), (M5), (M6). Guarded by
  `FM_sidebands_are_bessel` (a carrier under one modulator of index 1: the sidebands J_n(1)/J_0(1) to 0.0003, nothing
  else above −140 dB), `FM_envelopes_and_velocity_shape_each_operator`, `FM_renders_the_same_however_chopped`,
  `FM_does_not_allocate`.
- `REQ-fm-2` — **Eight algorithms and feedback** (same source): the TX81Z's eight (serial, branched, stacked, parallel),
  the carriers summed and shared (an algorithm does not change the loudness); operator 4 fed back on itself through
  the mean of its last two outputs (it does not chatter). (M2)–(M4). Guarded by `FM_algorithms_route_as_drawn`,
  `FM_feedback_brightens_operator_four` (no feedback: a sine; full: still a tone, 230 dB between its harmonics).
- `REQ-fm-3` — **The registry's "fm"** (same source): the algorithm, the feedback, each operator's ratio, fine, level,
  envelope and velocity sense, the volume, bend and vibrato. Guarded by the registry's tests (every range, no
  allocation after warm-up); integration `fm_plays_bessel_sidebands` (1:1 at index 1: its harmonics the Bessel sums,
  to 0.01 %).
- `REQ-resample-2` — **A stream held to another clock** (source: Solaris R-DEV-2, user request 2026-10-08: *"handle
  multiple audio devices"*; discussion Round 11, 2026-10-17): `AdaptiveResampler` — interleaved, any channel count,
  read `step` input frames an output frame, the step free to change every block with no click (the read position
  carried, its phase continuous); REQ-resample-1's kernel (now `SincKernel`, shared — `resample` gives the same
  samples) at c = 1, 32 taps, its weights normalised; `inputNeeded` exact; a constant R-frame lookahead; no
  allocation once built. src/resample/README.md (A1)–(A5). Guarded by `AdaptiveResampler_at_step_one_is_the_input`
  (to 1e-17, any blocks), `…_reads_a_sine_at_its_step` (93 dB clean — the table's linear reading the bound),
  `…_bends_with_no_click` (a step changed every block, blocks of 1…512: 3e-6 from the ideal), `…_does_not_allocate`.
- `REQ-resample-3` — **The step that holds a buffer's fill** (same source): `FillController` — a PI loop on the fill
  (smoothed over 4 s; a minute's natural period, critically damped — gains from those, not tuned), its integral the
  drift in ppm (clamped at ±2000 with the step: no wind-up), a new target approached at 200 ppm (no kick), a run
  started from a known drift locked at once. (F1)–(F6). Guarded by `FillController_holds_two_crystals_together`
  (48 000 v 48 003 Hz and ±1000 ppm, unlike blocks, five simulated minutes: no underrun, the fill within 2 frames,
  the drift to 0.5 ppm, no waver), `…_rides_out_a_late_scheduler` (±1 ms in the fill's time: the drift to 3 ppm, a
  waver under 2 ppm RMS), `…_travels_to_a_new_target`, `…_starts_locked_with_a_known_drift`, `…_clamps_and_recovers`,
  `…_never_winds_up`; integration `follower_holds_the_clock` (a consumer 1000 ppm fast: 1 kHz at 1e-6 of where the
  producer's second puts it, at its level, 91 dB clean in 16 bits).

- `REQ-meter-1` — **Loudness by ITU-R BS.1770-4** (source: Solaris R-METER-1, user request 2026-10-16): K-weighting
  (the standard's pre-filter and RLB high-pass, designed at any rate so that 48 kHz is its table to 1e-9), momentary
  (400 ms), short-term (3 s) and integrated loudness in LUFS, gated absolutely (−70 LUFS) and relatively (−10 LU)
  over 400 ms blocks at 75 % overlap; a 0 dBFS 1 kHz sine in one channel reads −3.01 LUFS. Allocation-free after
  construction. (L1)–(L6) in `src/meter/README.md`.
- `REQ-meter-2` — **True peak**: 4× oversampled through a 64-tap Kaiser-windowed sinc interpolator in four
  normalised phases; never under the sample peak. (T1)–(T3). The interpolator is its own type
  (`TruePeakInterpolator`), shared with the Limiter's true-peak mode at 8× (REQ-fx-limiter-2).
- `REQ-meter-3` — **Stereo correlation**, −1 … +1, smoothed over 300 ms. (C1).
- `REQ-meter-4` — **A 1/6-octave spectrum**, 61 bands from 19.7 Hz to 20.2 kHz, each a `Biquad` band-pass (a
  composition), in dB re a full-scale sine. (S1), (S2).
