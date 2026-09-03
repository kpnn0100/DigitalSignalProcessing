---
name: arstro.dsp.implement.experimental
description: Same six-rule workflow as arstro.dsp.implement (read-first/inherit-first composition, derive math before coding, document every equation in the module README, test after implementing, trace to a written requirement and update it on conflict, then commit) for implementing/changing a DSP module in this repo — but all work happens on and is committed to the `experimental` branch instead of the current branch, never to `main`. Invoke for the same tasks as arstro.dsp.implement ("add an effect", "implement a filter", "add a generator") whenever the user wants the work kept off main/mainline history, e.g. "try this as an experiment", "prototype this on experimental".
---

# arstro.dsp.implement.experimental

> **Invoke `arstro.rule` first.** It carries the suite-wide rules this skill builds on:
> requirements-first and the conflict rule, the V-model doc-sync loop, the agent-drivable surface,
> and the ledger/defect/commit conventions. **Follow both; where they overlap, this file's
> checklist is the one to satisfy.** If `arstro.rule` is not in your skill list you are in a
> standalone checkout of this submodule — the rules still apply, and the copy of record is
> `.claude/skills/arstro.rule/SKILL.md` in the arstro umbrella.

Identical rules to `arstro.dsp.implement`, with one difference: **the destination branch is
`experimental`, never the branch you started on.** If you were invoked while on `main` (or any
other branch), do not commit there under this skill — switch first.

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
- This skill is the experimental-branch twin of `arstro.dsp.implement`. Same rigor, same six
  rules — only the commit target changes.

## 0.5 Branch handling (the only difference from `arstro.dsp.implement`)

Before step 1, make sure work lands on `experimental`:

```bash
git status                                 # confirm no uncommitted work is about to be dragged along
git fetch origin experimental 2>/dev/null  # in case it exists remotely
git switch experimental 2>/dev/null || git switch -c experimental   # reuse local, else branch off current HEAD
```

- If `experimental` already exists locally, switch to it as-is (don't reset/rebase it
  automatically — surface any divergence to the user instead of resolving it silently).
- If it doesn't exist, create it from the current branch's HEAD.
- If `git status` shows uncommitted changes that are **not** part of this task, stop and ask
  before switching branches out from under them (switching carries them along, but confirm
  that's intended rather than assuming).
- Everything from here on (implementation, tests, commit) happens on `experimental`. Do not
  cherry-pick or merge back to `main`/the original branch as part of this skill — that's a
  separate, explicit decision for the user to make later.

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

A module with tuned coefficients and no `## Math` section is undocumented, full stop.

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
  formula. Experimental doesn't mean untested — it means "not yet on main."

## 5. Trace back to the requirement — update it on conflict

- `docs/requirements.md` is the requirement surface (shared with `main` — experimental work
  still needs a stated requirement, it's just not merged yet). If it doesn't exist yet, create
  it the first time this skill runs, seeded from the current module contracts in
  `docs/design.md` and each module README's stated behavior — one entry per requirement, with
  an ID (`REQ-<module>-<n>`), a one-line statement, and its source.
- Before implementing, identify which requirement ID(s) the change satisfies. If none exists
  for this change, add one first (with source = the request that prompted it), and mark it
  clearly as experimental (e.g. a `(experimental)` suffix on the entry) so it's obvious on
  `main` this isn't a committed requirement yet.
- If, during math derivation (step 2) or testing (step 4), the implementation turns out to
  conflict with the written requirement — **do not silently implement something different
  from what's written.** Update `docs/requirements.md` on the `experimental` branch to reflect
  the resolved, correct requirement, note what changed and why (one line), then implement
  against the updated text.

## 6. Commit — to `experimental`, never to the starting branch

Once steps 1–5 are all satisfied (module README `## Math` section written, `docs/design.md`
and `docs/architecture.puml` updated if the class surface changed, unit tests green, integration
tests green, `docs/requirements.md` entry added/updated with no unresolved conflict):

```bash
git branch --show-current   # must print "experimental" — if not, stop, do not commit
```

- Confirm you're on `experimental` before committing (§0.5). If somehow not, switch first —
  never let this skill's commit land on `main` or any other branch.
- Commit with a message describing what module was added/changed and which requirement ID it
  satisfies, same bar as `arstro.dsp.implement`. Do not commit with failing tests, missing
  `## Math` docs, or an unresolved requirement conflict.
- Do not push, merge, or fast-forward `main` from `experimental` as part of this skill —
  promoting experimental work to `main` is a separate, explicit decision for the user.

## Definition of done

- [ ] On the `experimental` branch (created off the starting branch if it didn't exist).
- [ ] Searched existing modules/READMEs for the concept; composed from existing processors if
      that fully covered the behavior (documented why, if not).
- [ ] Math model derived and written down (continuous model + discretization, or discrete
      difference equation) before/alongside implementation.
- [ ] Module `README.md` has a `## Math` section: formulas, symbol/parameter table, code
      cross-reference.
- [ ] Unit tests (`bash unittest/buildSynthTests.sh`) — `0 failed`, new branches covered.
- [ ] Integration test (`python3 tests/run_integration.py`) numerically verifies the math from
      step 2, not just "runs without crashing."
- [ ] `docs/requirements.md` entry exists for this change (marked experimental), and matches
      what was actually implemented.
- [ ] `docs/design.md` / `docs/architecture.puml` reflect any new/changed classes.
- [ ] Committed on `experimental`, `main` untouched.
