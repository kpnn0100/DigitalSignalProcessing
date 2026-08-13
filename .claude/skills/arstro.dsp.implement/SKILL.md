---
name: arstro.dsp.implement
description: Use when implementing, changing, or fixing a DSP module in the Arstro DSP library (this repo, namespace `arstro`, buildable subset `src/synth_dsp.h`). Enforces six rules in order — read existing modules first and compose instead of duplicating (e.g. chorus is a delay line driven by an LFO, not a new algorithm), derive the math before writing code, document every equation in the module README, test after implementing, trace the implementation back to a written requirement and update the requirement on conflict rather than silently deviating, then commit. Invoke for tasks like "add an effect", "implement a filter", "add a generator", "change the envelope math".
---

# arstro.dsp.implement

Six rules, applied in order, every time a DSP module is added or changed in this repo.
Skipping a rule is the bug — do not reorder or shortcut them.

## 0. Orient (facts about this repo)

- **Where this repo sits.** This is `arstro/core/DigitalSignalProcessing/` in the umbrella
  repo. The Arstro core libraries live under `arstro/core/` (`core/Artboard/`,
  `core/DigitalSignalProcessing/`, `core/ImageProcessing/`); the applications that consume them
  live under `arstro/apps/` (`apps/cosmo`, `apps/genesis`, `apps/pulsar`, `apps/launcher`, plus
  spec-stage `apps/solaris`, `apps/interstellar`); small demos stay at `arstro/examples/`. Every
  path below is relative to THIS repo's root, not the umbrella's — this repo's own `apps/`
  (`apps/wav_demo`, `apps/piano_demo`) is a DSP demo folder, unrelated to `arstro/apps/`.
- **Layout.** Sources under `src/` (`base`, `simpleProcessor`, `functional`, `generator`,
  `envelope`, `equalizer`, `effects`, `reverb`, `synth`, `output`). Aggregate header
  `src/synth_dsp.h` is the buildable, tested surface — `src/spatial/`, `src/util/`, and
  `src/arstro_dsp.h` are legacy/excluded, do not build against them. Docs: `docs/design.md`
  (overview), `docs/architecture.puml` (design), per-module `README.md` (contract).
- **Namespace** `arstro`. Scalar type `Sample` (`src/base/Sample.h`) — never hardcode
  `double`/`float`. Config authority: `arstro::AudioConfig::instance()` — nothing stores its
  own sample rate/channel count/buffer size.
- **Base classes.** `SignalProcessor` (effects/filters — `process(Sample in, int channel)`),
  `SignalGenerator` (sound sources), `Block`/`FeedbackBlock` (composites — per-channel serial
  or parallel chains, with feedback). Params are shared across channels and smoothed via
  `ParameterSet`/`SmoothedParameter`; **running state is per-channel**, resized in
  `onChannelCountChanged()`.
- **Build/test:** `bash unittest/buildSynthTests.sh` (C++ unit, `MiniTest.h`, must be
  `0 failed`); `python3 tests/run_integration.py` (Python, renders WAV via
  `tests/render_harness.cpp`, asserts behavior numerically); or both via
  `cmake -S . -B build && cmake --build build && (cd build && ctest)`.
- This skill is deliberately stricter than the general `implement_with_sync_and_test` skill
  in this repo: it adds mandatory composition-search, math derivation, equation
  documentation, and requirement traceability on top of that skill's doc/design/code/test
  sync loop. Follow both; where they overlap, this one's checklist is the one to satisfy.

## 1. Read existing first — inherit, don't duplicate

Before writing a single line of new DSP math, search for it:

```bash
grep -ril "<concept>" src/ docs/          # e.g. "delay", "envelope", "lfo", "filter"
find src -iname "README.md" | xargs grep -l "<concept>"
```

Read every module `README.md` under `src/*/` whose domain overlaps the new feature, and read
`docs/design.md`'s layer table. A new processor is only justified if none of the following are
true:

- **It already exists** (same behavior, maybe under a different property name) — reuse it.
- **It is a composition of existing primitives.** Many "effects" are just a well-known
  arrangement of already-implemented processors:
  - *Chorus* = `simpleProcessor/Delay` (one per channel) modulated by an internal LFO writing
    to the delay-time. This repo's `src/effects/Chorus.cpp` is the canonical example — read it
    before implementing anything that resembles it.
  - *Flanger* is the same shape as chorus with a shorter base delay and feedback.
  - *Echo/Repeater* is `functional/ProcessorRepeater<Delay>`.
  - *Vibrato* is oscillator-modulated pitch/delay with 100% wet, no dry mix.
  - Ask: "is this new module's behavior fully described by wiring N existing
    `SignalProcessor`/`SignalGenerator` instances through a `Block`?" If yes, implement it as
    a thin composite (own the wiring + its own parameter surface), not as a hand-rolled
    reimplementation of delay lines, oscillators, or filters that already exist.
- If the feature genuinely needs new core math (a new filter topology, a new envelope shape,
  a new nonlinearity) — that's a new module. State in the module README *why* composition
  wasn't sufficient, so the next person doesn't redo this search.

## 2. Math base — derive before you code

Every module's DSP behavior must be defined by an explicit mathematical model *before*
implementation, not reverse-engineered from tuned constants after:

- State the **continuous-time model** if there is one (e.g. an analog filter prototype, a
  physical system) and its **discretization** (bilinear transform, forward/backward Euler,
  impulse-invariant, etc.), or state the **discrete-time difference equation** directly if
  there's no continuous analog (e.g. an LFO phase accumulator).
- Name every coefficient and give the formula that produces it from the module's parameters
  (in real units — Hz, ms, dB — plus the internal per-sample form actually used in `process()`).
- If a constant is empirical/tuned-by-ear (not derived), say so explicitly and why (e.g. the
  `kStereoSpread = 0.15` in `reverb/README.md` — an example of a documented empirical
  constant, not a silently magic number).
- No coefficient in `update()`/`process()` should exist that isn't traceable to this section.

## 3. Document every equation in the module README

The module's `README.md` (create if it doesn't exist) must contain a `## Math` section with:

- The derivation/model from step 2, in the same plain-text formula style already used in this
  repo (see `src/reverb/README.md`'s delay-spread formula for the house style — fenced code
  block, one relation per line, symbols defined inline or in a table).
- A parameter table mapping each public setter (unit: ms/Hz/dB/linear) to the symbol used in
  the formulas.
- Where the equation is *used* in code (function name), so drift between doc and
  implementation is easy to spot on review.

A module with tuned coefficients and no `## Math` section is undocumented, full stop — this is
stricter than the general skill's README requirement.

## 4. Test after implementing

- **Unit tests** (`unittest/synthTests.cpp` behavioral, `unittest/coverageTests.cpp` edge
  cases) covering every new branch/param/edge — register new `.cpp` files in both
  `unittest/buildSynthTests.sh` and the `synth_tests` CMake target. Run
  `bash unittest/buildSynthTests.sh`, must be `0 failed`.
- **Integration test** (`tests/`) that renders audio through the new/changed module and
  asserts the *math from step 2* numerically holds (correct frequency via FFT, correct decay
  time, correct filter attenuation slope, etc. — not just "it doesn't crash"). Run
  `python3 tests/run_integration.py`, must pass.
- If a formula in step 2 can't be verified by a test you know how to write, that's a sign the
  math isn't specified precisely enough yet — go back to step 2, don't ship an untested
  formula.

## 5. Trace back to the requirement — update it on conflict

- `docs/requirements.md` is the requirement surface. If it doesn't exist yet, create it the
  first time this skill runs, seeded from the current module contracts in `docs/design.md`
  and each module README's stated behavior — one entry per requirement, with an ID
  (`REQ-<module>-<n>`), a one-line statement, and its source (user request, design doc,
  hardware constraint).
- Before implementing, identify which requirement ID(s) the change satisfies. If none exists
  for this change, add one first (with source = the request that prompted it).
- If, during math derivation (step 2) or testing (step 4), the implementation turns out to
  conflict with the written requirement (the requirement is ambiguous, physically
  inconsistent, or contradicts another requirement) — **do not silently implement something
  different from what's written.** Update `docs/requirements.md` to reflect the resolved,
  correct requirement, note what changed and why (one line), then implement against the
  updated text. The requirement doc and the code must never disagree about intent.

## 6. Commit

Once steps 1–5 are all satisfied (module README `## Math` section written, `docs/design.md`
and `docs/architecture.puml` updated if the class surface changed, unit tests green, integration
tests green, `docs/requirements.md` entry added/updated with no unresolved conflict) — commit
the change with a message describing what module was added/changed and which requirement ID it
satisfies. Do not commit with failing tests, missing `## Math` docs, or an unresolved
requirement conflict.

## Definition of done

- [ ] Searched existing modules/READMEs for the concept; composed from existing processors if
      that fully covered the behavior (documented why, if not).
- [ ] Math model derived and written down (continuous model + discretization, or discrete
      difference equation) before/alongside implementation.
- [ ] Module `README.md` has a `## Math` section: formulas, symbol/parameter table, code
      cross-reference.
- [ ] Unit tests (`bash unittest/buildSynthTests.sh`) — `0 failed`, new branches covered.
- [ ] Integration test (`python3 tests/run_integration.py`) numerically verifies the math from
      step 2, not just "runs without crashing."
- [ ] `docs/requirements.md` entry exists for this change, and matches what was actually
      implemented (updated if a conflict was found during implementation).
- [ ] `docs/design.md` / `docs/architecture.puml` reflect any new/changed classes.
- [ ] Committed.
