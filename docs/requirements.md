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
- `REQ-piano-16` — Una corda's real mechanism (hammer shifts to strike fewer of the unison
  strings) is approximated as a reduced hammer excitation gain + reduced contact hardness,
  not literal dynamic unison-count switching mid-performance.
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
