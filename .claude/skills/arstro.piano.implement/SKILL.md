---
name: arstro.piano.implement
description: Use to advance (or resume, in any session) the multi-milestone physics upgrade that turns the Arstro piano model in src/physical/ from a struck-string sound into a real piano — coupled hammer-string interaction, frequency-dependent losses, pitch-scaled partial counts, two-polarisation double decay, soundboard, per-register voicing, longitudinal/phantom partials, tension modulation. Progress is tracked in a committed ledger so the work resumes exactly where it stopped. Invoke when the user says things like "continue the piano", "work on the piano physics", "make the piano sound more real", "what's next on the piano", or "/arstro.piano.implement". It reads the ledger, does the next milestone, verifies it numerically, updates the ledger, and commits.
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
3. **This skill** — the procedure below.

All three live in the `DigitalSignalProcessing` repo (a submodule of the `arstro` umbrella);
the interactive UI this feeds lives at `examples/piano/` in the umbrella repo. See §Routing.

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

## Requirement conflicts — three are known and pre-flagged

M5 (if the commuted/measured-IR route is chosen), M7, and M9.4 each contradict a written
requirement (`REQ-piano-14`, `REQ-piano-15`, `REQ-piano-16` respectively — all three currently
declare that work out of scope). Per `arstro.dsp.implement` rule 5: **amend
`docs/requirements.md` first**, with the reason and the date, then implement against the
amended text. Never implement against a requirement you know is stale, and never quietly
leave the requirement contradicting the code.

## Routing — which repo does a change belong in?

| Change | Repo | Notes |
|---|---|---|
| `src/physical/*`, `docs/*`, tests, `apps/piano_demo/` | `DigitalSignalProcessing` | The default. Almost everything lands here. |
| `examples/piano/*` (the playable GTK/ALSA keyboard UI) | `arstro` umbrella | Only when a milestone changes the app, not the DSP. |
| Submodule pointer bump | `arstro` umbrella | After committing in `DigitalSignalProcessing`, bump the pointer so the umbrella builds the new DSP. |

If a milestone touches the DSP and the app, commit the DSP first, then the umbrella (app
change + pointer bump), and say so in the report.

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
