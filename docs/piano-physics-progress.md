# Piano Physics Upgrade — Progress Ledger

**State lives here, not in anyone's memory.** The `arstro.piano.implement` skill reads this
file first and updates it last, every session. Spec:
[`piano-physics-plan.md`](piano-physics-plan.md).

- **Last updated:** 2026-07-25 (plan created; no milestones started)
- **Last commit:** _(none yet — this ledger's own commit)_
- **M0 perf baseline:** _not measured_
- **Perf budget:** ≥ 4× real-time, 8 voices @ 48 kHz (see plan §M0)

---

## ► NEXT

**M0 — Performance baseline & budget.** Build the offline 8-voice benchmark, record the
pre-upgrade × real-time number in this ledger. Small task; it exists so M2/M4 have a gate to
be measured against.

_(After M0: M1 loss model → M2 partial count. Those two are the ones that should audibly stop
it sounding like a plucked string.)_

---

## At a glance

| M | Milestone | Status |
|---|---|---|
| M0 | Perf baseline & budget | `[ ]` |
| M1 | Frequency-dependent loss model (spectral evolution) | `[ ]` |
| M2 | Pitch-dependent partial count (bandwidth) | `[ ]` |
| M3 | Coupled hammer↔string interaction | `[ ]` |
| M4 | Two transverse polarisations (double decay) | `[ ]` |
| M5 | Soundboard / bridge | `[ ]` |
| M6 | Per-register voicing | `[ ]` |
| M7 | Longitudinal modes & phantom partials | `[ ]` |
| M8 | Tension modulation (attack pitch glide) | `[ ]` |
| M9 | Tier-3 detail | `[ ]` |

Status key: `[ ]` not started · `[~]` in progress · `[x]` done & verified · `[!]` done but
some acceptance criterion could not be verified here (see Verification notes).

---

## Task checklists

### M0 — Perf baseline & budget `[ ]`
- [ ] Offline benchmark: 8-voice chord through `PianoEngine`, reports × real-time
- [ ] Record baseline number in this ledger's header
- [ ] Confirm the 4× RT budget is met *before* any change (if not, say so — it changes M2/M4 scope)

### M1 — Frequency-dependent loss model `[ ]`
- [ ] Derive α(f) = c1 + c3(2πf)² parameterisation + the c1/c3 solve into README `## Math`
- [ ] Derive & document the per-note T60_fundamental(f0) fitted curve (anchors in plan §M1)
- [ ] Replace `dampingExponentID` with `brightnessDecayID` (API change — update all callers)
- [ ] `PianoVoice::setFrequency()` sets a pitch-derived default T60
- [ ] Unit tests: partial-20 vs partial-1 T60 ratio ≥ 50; A0 vs C7 fundamental T60 ratio ≥ 10
- [ ] Integration test: high/low band energy ratio drops ≥ 20 dB from attack to t = 1 s
- [ ] `docs/requirements.md` updated (new/changed param surface)
- [ ] Re-run M0 benchmark, record

### M2 — Pitch-dependent partial count `[ ]`
- [ ] Fixed-capacity `kMaxPartials = 64` + `mActiveCount`; **no audio-thread allocation**
- [ ] Cutoff evaluated on inharmonic `f_n`, not `n·f0`
- [ ] `kExciteNorm` → `2.0/mActiveCount`
- [ ] Unit tests: active count at C2/C4/C8; **every** `f_n < f_s/2` across the keyboard
- [ ] Integration test: C4 has energy above 3 kHz (previously exactly none)
- [ ] Re-run M0 benchmark, record

### M3 — Coupled hammer↔string `[ ]`
- [ ] Derive modal-mass unit consistency into README `## Math` **before coding** (plan §M3)
- [ ] `StringResonator::lastValue(channel)` + `StringPartialBank::displacementAtStrike()`
- [ ] `HammerExciter::process()` uses `in` as string displacement; `c = x_h − y_string`
- [ ] **Delete `kHammerToStringGain` and `registerGain(f0)^0.6`** (replaced by modal mass)
- [ ] Unit tests: contact duration varies with pitch; still monotonic in velocity
- [ ] Integration test: bass force pulse shows a post-peak local max (reflection ripple)
- [ ] Integration test: treble contact duration > one period of f0
- [ ] Output bounded across keyboard with `registerGain` gone
- [ ] Re-run M0 benchmark, record

### M4 — Two polarisations `[ ]`
- [ ] Derive polarisation split + `kPolarizedPartials` approximation into README `## Math`
- [ ] Correct the README's existing double-decay claim (currently credits unison detune)
- [ ] Unit/integration test: late decay slope ≥ 3× slower than early slope
- [ ] Re-run M0 benchmark, record — **most likely milestone to breach the budget**

### M5 — Soundboard / bridge `[ ]`
- [ ] Choose approach; if commuted/measured IR → **amend `REQ-piano-14` first**
- [ ] Extend modal set to ~30–40 modes, 50 Hz – 5 kHz, realistic density + radiation rolloff
- [ ] Test: bridge transfer function shows ≥ 6 dB variation above 700 Hz
- [ ] Re-run M0 benchmark, record

### M6 — Per-register voicing `[ ]`
- [ ] Hammer mass(f0), felt stiffness(f0), strike position β(f0), strings-per-note(f0)
- [ ] Test: spectral centroid vs pitch trend; unison count at register boundaries
- [ ] Re-run M0 benchmark, record

### M7 — Longitudinal modes & phantom partials `[ ]`
- [ ] **Amend `REQ-piano-15` first** (currently declares this out of scope)
- [ ] Longitudinal resonator bank driven by squared transverse signal
- [ ] Test: phantom energy where `f_n` predicts no transverse partial; scales with velocity
- [ ] Re-run M0 benchmark, record

### M8 — Tension modulation `[ ]`
- [ ] f0(t) = f0(1 + κ·E_transverse(t))
- [ ] Test: hard bass blow ≥ 2 cents sharp at 50 ms vs t = 1 s; soft blow < 0.5 cents
- [ ] Re-run M0 benchmark, record

### M9 — Tier-3 detail `[ ]`
- [ ] No dampers above ~MIDI 88
- [ ] Duplex / aliquot scale
- [ ] Stulov felt hysteresis (replaces README §6's load/unload simplification)
- [ ] Una corda proper — **amends `REQ-piano-16`**
- [ ] Re-run M0 benchmark, record

---

## Decisions & deviations log

_(newest first — record anything that departs from the plan, or resolves an open choice, so
it is never re-litigated)_

- **2026-07-25** — Plan created from a code audit after the user reported the model "sounds
  like a string instrument, like hitting a violin string." Root findings: the hammer discards
  its string input (open-loop excitation), 12 fixed partials, uniform 3 s decay for every
  note, and a near-uniform per-partial decay law. Ordered M1/M2 before the deeper M3 because
  they are small, local, and should carry most of the immediate audible improvement.

## Verification notes

_(anything marked `[!]` — what could not be checked here and why)_

- Nothing yet. Note that this project is developed headless: no one has *listened* to the
  output in this environment. Every acceptance criterion in the plan is deliberately
  numeric so progress is provable without ears — but final timbre judgement is the user's,
  and "sounds right" is never a criterion this skill may tick on its own.
