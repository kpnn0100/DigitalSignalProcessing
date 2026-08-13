---
name: arstro.piano.implement
description: Use to advance (or resume, in any session or on a freshly cloned machine) the multi-milestone physics upgrade that turns the Arstro piano model in src/physical/ from a struck-string sound into a real piano — coupled hammer-string interaction, frequency-dependent losses, pitch-scaled partial counts, two-polarisation double decay, soundboard, per-register voicing, longitudinal/phantom partials, tension modulation. Progress is tracked in a committed ledger so the work resumes exactly where it stopped, and the skill carries the build/test/audio setup a new machine needs. Invoke when the user says things like "continue the piano", "work on the piano physics", "make the piano sound more real", "what's next on the piano", or "/arstro.piano.implement". It reads the ledger, does the next milestone, verifies it numerically, updates the ledger, and commits.
---

# arstro.piano.implement

The **resume-driven** workflow for the piano physics upgrade. All state lives in committed
files, so any session can pick up exactly where the last one stopped.

## The three source-of-truth files

1. **The plan** — `docs/piano-physics-plan.md`. Why the model currently sounds wrong, the
   milestone order and the reasoning behind it, and for each milestone: the physics, the
   formulas, the parameterisation, and its **numeric acceptance criteria**. Read the current
   milestone's section before starting it.
2. **The progress ledger** — `docs/piano-physics-progress.md`. What is done, what is next
   (**► NEXT**), per-milestone checklists, the perf log, a decisions log, and verification
   notes. **Read this first, update it last, every session.**
3. **This skill** — the procedure below, plus §"Setting up on a new machine" and §"What
   already exists outside the milestones", which are the two things a fresh clone needs and
   cannot infer from the ledger.

All three live in the `DigitalSignalProcessing` repo (a submodule of the `arstro` umbrella);
the interactive UI this feeds lives at `examples/piano/` in the umbrella repo. See §Routing.

## Setting up on a new machine

The repo is self-contained; nothing below is needed to *implement* a milestone, only to build,
test and **listen**. Milestone work is verified numerically and headless, so a machine with no
audio device can still do every step of the loop except the final listening check.

Only two of the umbrella's core libraries matter here: **`core/DigitalSignalProcessing`**
(all the physics, i.e. THIS repo) and **`core/Artboard`** (only for the playable UI).
`core/ImageProcessing/lib/LibRaw` is a large, unrelated submodule — skip it. Note the umbrella
is laid out as `arstro/core/` (libraries) + `arstro/apps/` (applications) + `arstro/examples/`
(demos, including `examples/piano`); paths below are relative to THIS repo's root unless they
start with `examples/`, and this repo's own `apps/piano_demo` is unrelated to `arstro/apps/`. Work happens on branch **`feature/1.0.0`** in the umbrella *and* in the
submodules; a fresh clone can land on a detached HEAD, which will lose commits.

```bash
git clone <arstro remote> && cd arstro
git submodule update --init DigitalSignalProcessing Artboard   # NOT --recursive
git -C DigitalSignalProcessing checkout feature/1.0.0
git -C Artboard checkout feature/1.0.0

cd DigitalSignalProcessing
bash unittest/buildSynthTests.sh          # unit  — must print "0 failed"
python3 tests/run_integration.py          # integration — must print "0 failed"
cmake -S . -B build && cmake --build build -j$(nproc) && (cd build && ctest)
./build/piano_bench                       # perf gate; record in the ledger
```

Build needs only a C++17 compiler, CMake and Python 3 — **no NumPy, no libsndfile, no gtest**.
The interactive keyboard additionally needs GTK3, Cairo, ALSA and libX11:

```bash
sudo apt install build-essential cmake libgtk-3-dev libasound2-dev libx11-dev
cmake -S . -B build && cmake --build build --target arstro_piano_ui   # from the umbrella root
```

### Audio tuning — expect to redo this per machine

Interactive glitching is almost always **scheduling, not DSP**, and the numbers to justify that
are in the ledger's §"Since M7". Before optimising anything, check the two things that actually
decide it:

```bash
ulimit -r                 # 0  => SCHED_FIFO will be refused; the app logs which it got
pgrep -l pipewire pulseaudio jackd   # a sound server adds its own buffering and scheduling
```

Runtime knobs, all optional — the defaults are chosen to be safe on a stock desktop
(rtprio 0 + PulseAudio), not to be low-latency:

| variable | default | use |
|---|---|---|
| `ARSTRO_PIANO_LATENCY_US` | 30000 | raise if it glitches, lower once RT priority works |
| `ARSTRO_PIANO_FRAMES` | 256 | period size |
| `ARSTRO_PIANO_DEVICE` | `default` | `plughw:0` bypasses PulseAudio |
| `ARSTRO_PIANO_UI_MS` | 33 | UI repaint interval (Cairo software raster competes for CPU) |

To get genuinely low latency, grant real-time priority — this is the fix, the buffer size is
only a workaround:

```bash
echo "@audio - rtprio 95" | sudo tee /etc/security/limits.d/audio.conf
sudo usermod -aG audio $USER      # log out and back in
```

The app counts underruns and warns once a second, so a glitch is always visible rather than
inferred. **A latency complaint is never evidence of a throughput problem** (`REQ-compute-1`).

## What already exists outside the milestones

Read the ledger's §"Since M7" for the current list. The standing points a milestone must not
trip over:

- **`src/compute/`** — a parallel executor with a platform-adapter seam
  (`docs/parallel-architecture.md`, `REQ-compute-1..6`). **Off by default and measured not to
  pay yet.** `PianoEngine` is deliberately *not* wired to it. Do not enable it to "fix" a
  perf problem without measuring first; the Amdahl ceiling for this graph is 4.14× at 8 workers.
- **`PianoEngine::kMasterGain`** — the output limiter is a safety catch, not a gain stage. Any
  milestone that changes per-voice level **must re-measure how often `tanh` engages**; it has
  silently rotted once already (91 % peak squash at eight voices).
- **`PianoVoice::isSilent()`** (README `## 13`) — voices are frozen only when fully damped
  *and* inaudible. The damper half is a safety property protecting `REQ-piano-6`, not a tuning
  choice. Do not loosen it.

## The loop (every invocation)

1. **Orient.** Read `docs/piano-physics-progress.md` — the **► NEXT** line and the current
   milestone's checklist — then that milestone's section in `docs/piano-physics-plan.md`. If
   the ledger looks stale versus the actual code (a task marked `[ ]` whose code already
   exists, or vice-versa), **reconcile the ledger to reality first and say so**.
2. **Scope one milestone.** Take the milestone **► NEXT** points at, and work its checklist
   top to bottom. Mark it `[~]` in the ledger when you start. One milestone per session is the
   intended size — M1 and M2 are small enough to pair if the user asks. If the user names a
   specific milestone, do that instead.
3. **Implement it under `arstro.dsp.implement`.** That skill governs *how* — math derived and
   written into `src/physical/README.md`'s `## Math` **before** code, unit tests to 100% line
   coverage on `physical/`, a numeric integration assertion, requirement traceability,
   commit. This skill only decides *what and in what order*. Follow both; where they overlap,
   `arstro.dsp.implement`'s checklist is the one to satisfy.
4. **Verify against the plan's acceptance criteria — numerically.** Every milestone in the
   plan has measurable criteria precisely so this project can be verified headless. Then
   **re-run the M0 benchmark** and check the ≥ 4× real-time budget.
   - Never tick a milestone on "it compiles" or "it sounds better." Nobody in this
     environment has heard it.
   - If a criterion genuinely cannot be checked here, mark the task `[!]`, and add a line to
     the ledger's **Verification notes** stating exactly what is unverified and why.
   - A milestone that breaches the perf budget is **not done** — optimise, or cut its scope
     and record that in the decisions log.
5. **Update the ledger. This is not optional** — it is the entire reason the next session
   knows where to resume:
   - Tick the tasks `[x]` (or `[!]`), and the milestone in the at-a-glance table if it just
     completed.
   - Rewrite **► NEXT** to point at the next unchecked task, or the next milestone's first
     task, or — if M0–M9 are all done — the §"When every milestone is done" review below.
   - Update "Last updated", "Last commit", and the **M0 perf number** for this milestone.
   - Add any plan deviation or resolved open choice to the **Decisions & deviations log**
     (newest first), so it is never re-litigated.
6. **Commit** (see §Commit), *including the ledger update*, then stop and report what you did
   and what **► NEXT** now points at — unless the user asked you to keep going, in which case
   loop to step 1.

**Do not skip step 5.** A finished milestone whose ledger was not updated is the one failure
mode that breaks resuming.

## Requirement conflicts — one is still pending

Per `arstro.dsp.implement` rule 5: **amend `docs/requirements.md` first**, with the reason and
the date, then implement against the amended text. Never implement against a requirement you
know is stale, and never quietly leave the requirement contradicting the code.

| | requirement | status |
|---|---|---|
| M5 | `REQ-piano-14` (no measured IR) | **no conflict** — the modal-extension route was taken instead |
| M7 | `REQ-piano-15` (longitudinal out of scope) | **amended** 2026-07-26; it now *requires* phantom partials |
| M9.4 | `REQ-piano-16` (una corda approximated) | ⚠️ **still pending** — amend before implementing real una corda |

Also amended along the way, and worth knowing before touching either area: `REQ-piano-3` twice
(M4 — polarisations, not unison detuning, produce double decay; M6 — the bass is single-strung
by design, so "2+ unison strings on every note" is false).

## Routing — which repo does a change belong in?

| Change | Repo | Notes |
|---|---|---|
| `src/physical/*`, `docs/*`, tests, `apps/piano_demo/` | `DigitalSignalProcessing` | The default. Almost everything lands here. |
| `examples/piano/*` (the playable GTK/ALSA keyboard UI) | `arstro` umbrella | Only when a milestone changes the app, not the DSP. |
| Submodule pointer bump | `arstro` umbrella | After committing in `DigitalSignalProcessing`, bump the pointer so the umbrella builds the new DSP. |

If a milestone touches the DSP and the app, commit the DSP first, then the umbrella (app
change + pointer bump), and say so in the report.

## Measurement lessons that keep paying

Earned in M1–M7, each one after it had already cost a milestone. Apply them before inventing a
new way to be fooled:

- **Put a magnitude floor beside every ratio.** A ratio between two silent signals is
  arithmetic, not evidence. Two milestones shipped a vacuous pass this way (M1's NaN hidden by
  WAV clamping, M3's "ratio 0.00 PASS").
- **To test an added stage, difference two renders** — one with it, one with it disabled. The
  remainder *is* that stage's output, exactly. M7's first implementation was completely silent
  and still looked plausible in a single spectral bin, because the attack transient is broadband.
- **When a coupled model looks wrong, measure a conserved quantity, not an output.** M6's real
  bug was a 201× *energy gain* in the contact loop; peak, RMS, contact duration and spectrum all
  looked merely odd, while `E_out/E_in` said "impossible" at once.
- **A physics error only becomes visible once the surrounding physics gets more correct.** Three
  normalisation bugs in M1–M2 were each invisible until a different parameter started varying.
- **A milestone can silently regress an earlier one's criterion** — M4 and M6 both broke M1's
  spectral evolution. Every past criterion stays in the suite for exactly this reason.
- **Measure before optimising.** The interactive "lag" was a 50 ms buffer while the DSP used
  under 20 % of the budget; the reported "glitch" was a saturating limiter, not starvation.

## Test levels

- **Unit** (`unittest/synthTests.cpp` behavioural, `unittest/coverageTests.cpp` edges) —
  `bash unittest/buildSynthTests.sh`, must be `0 failed`, 100% line coverage on `physical/`.
- **Integration** (`tests/render_harness.cpp` scenario + `tests/run_integration.py` numeric
  check) — `python3 tests/run_integration.py`. Every milestone adds at least one assertion
  that its *own* acceptance criterion holds, not just that audio came out.
- **Perf** — the M0 benchmark, every milestone, recorded in the ledger.
- **Everything** — `cmake -S . -B build && cmake --build build && (cd build && ctest)`.

## Commit

One focused commit per milestone (or per checklist task if a milestone runs long), in
whichever repo(s) it touched, **always including the ledger update**. State in the message:
which milestone, the physics changed, the acceptance numbers measured, and the perf figure.
Do not commit with failing tests, a missing `## Math` section, an unresolved requirement
conflict, or a stale ledger.

## When every milestone is done

Do a whole-project review rather than declaring victory:
1. Re-read `src/physical/README.md` end to end — does the `## Math` still describe the code
   after nine milestones of change? Fix any drift.
2. Re-read `docs/requirements.md` — do all `REQ-piano-*` entries match what was actually
   built (especially the three that were amended)?
3. Run the full `ctest` suite and the M0 benchmark; record final numbers.
4. Render a fresh `build/piano_demo.wav` and the `examples/piano` app, and **hand it to the
   user to listen to** — the final timbre judgement is theirs, and was never this skill's to
   make.
