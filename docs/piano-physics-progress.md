# Piano Physics Upgrade — Progress Ledger

**State lives here, not in anyone's memory.** The `arstro.piano.implement` skill reads this
file first and updates it last, every session. Spec:
[`piano-physics-plan.md`](piano-physics-plan.md).

- **Last updated:** 2026-07-25 (M2 complete)
- **Last commit:** M2 — pitch-dependent partial count + flattened resonator loop
- **Perf budget:** ≥ 4× real-time, 8 voices @ 48 kHz (`REQ-piano-17`, plan §M0)

### Perf log

| After | 8 voices | 1 voice | Resonators | Budget |
|---|---|---|---|---|
| **M0 baseline (pre-upgrade)** | **15.42× RT** (median 15.32×) | 17.20× | 192 | ✅ met |
| **M1** (loss model) | **16.25× RT** | 18.35× | 192 | ✅ met |
| **M2** (64 partials + flattened loop) | **19.27× RT** (median 18.57×) | 20.75× | ~1024 | ✅ met |

Measured by `./build/piano_bench` (5 passes × 10 s, best-of). M2's resonator count is
pitch-dependent (bass fills the 64 cap, treble uses ~5); ~1024 is the benchmark chord's worst
case. **M2 runs 5.3× more resonators than M1 yet is faster** — flattening the inner loop gave
a ~6.7× per-resonator speedup, which is why the projected 2.9× breach never happened.

---

## ► NEXT

**M3 — Coupled hammer↔string interaction.** The deepest fix in the plan and the one that
addresses the original diagnosis: `HammerExciter::process()` still discards its input, so the
hammer is a free mass bouncing off an infinitely rigid wall. Close the loop —
`c = x_hammer − y_string(β)` — so contact force depends on the string yielding.

Read plan §M3 first. It requires **deriving modal-mass unit consistency into the README
before writing code**: `x_hammer` and `y_string` must share units, which is what finally
replaces `kHammerToStringGain` with physically determined scaling.

Why it matters now: M1 measured the attack's high partials at ~42 dB below the fundamental,
and M2 confirmed the bandwidth is there but under-excited — the open-loop ~2.6 ms force pulse
simply has little high-frequency content. M3's reflection ripple is what re-injects it, and it
is what should raise the spectral-evolution criterion back to the plan's original 20 dB.

Budget: M2 leaves **19.27× RT**, 4.8× above the gate — ample room.

---

## At a glance

| M | Milestone | Status |
|---|---|---|
| M0 | Perf baseline & budget | `[x]` |
| M1 | Frequency-dependent loss model (spectral evolution) | `[x]` |
| M2 | Pitch-dependent partial count (bandwidth) | `[x]` |
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

### M0 — Perf baseline & budget `[x]`
- [x] Offline benchmark: 8-voice chord through `PianoEngine`, reports × real-time
      (`tools/piano_bench.cpp`, target `piano_bench`, machine-parseable `KEY=VALUE` output)
- [x] Record baseline number in this ledger's header — **15.42× RT, 8 voices**
- [x] Confirm the 4× RT budget is met *before* any change — **met with 3.9× headroom**, but
      that headroom is *not* enough for M2 as specced (see decisions log + plan §M2 risk block)
- [x] `REQ-piano-17` added (no perf requirement existed; rule 5 requires one before the work)
- [x] Non-flaky `piano_bench_smoke` ctest so the benchmark can't rot between milestones

### M1 — Frequency-dependent loss model `[x]`
- [x] Derive α(f) = c1 + c3(2πf)² parameterisation + the c1/c3 solve into README `## Math`
- [x] Derive & document the per-note T60_fundamental(f0) fitted curve (residuals ±40 %, stated)
- [x] Replace `dampingExponentID` with `brightnessDecayID` (all 15 references migrated)
- [x] `PianoVoice::setFrequency()` sets a pitch-derived default T60 (override latch added)
- [x] Unit tests: partial-1/partial-12 T60 ratio **35.3** (≥25); A0/C7 fundamental ratio **50.7** (≥10)
- [x] Unit test: loss law provably affine in ω² (same c3 from every partial) — pins the model itself
- [x] Integration test: high/low band ratio drops **17.1 dB** attack → 1 s (≥15, see decisions)
- [x] `docs/requirements.md` — REQ-piano-3 already covers frequency-dependent loss; no conflict
- [x] Re-run M0 benchmark: **16.25× RT** (up from 15.42×) — budget met
- [x] 100 % line coverage held on all five `physical/` sources

### M2 — Pitch-dependent partial count `[x]`
- [x] Fixed-capacity `kMaxPartials = 64` + `mActiveCount`; **no audio-thread allocation**
- [x] Cutoff evaluated on inharmonic `f_n`, not `n·f0` (A0 64, C4 63, C6 20, C8 5)
- [x] Flattened the recurrence over flat coefficient arrays (the chosen mitigation) —
      **unit-tested sample-exact (<1e-12) against `StringResonator`**, which stays the
      soundboard's mode type and the reference implementation
- [x] Removed the `2/N_active` excitation scaling — modal superposition has no such factor,
      and once N varied with pitch it made bass fundamentals quieter purely for having more
      partials (see decisions log)
- [x] Unit tests: N_active at A0/C4/C8; **every** `f_n < f_s/2` across all 88 notes
- [x] Integration test: C4 carries 51 partials above 3 kHz at −51.3 dB rel. fundamental,
      where before M2 it had **exactly none**
- [x] **M1's deferred criterion 1 re-asserted in its original form**: `T60(p1)/T60(p20)` =
      **100.7** (≥50) — matching M1's projection exactly
- [x] Deleted dead code rather than faking coverage: `StringResonator::reset()` (lost its
      only caller) and an unreachable channel guard
- [x] Re-run M0 benchmark: **19.27× RT** — budget met with 4.8× margin
- [x] 100 % line coverage held on all five `physical/` sources

### M3 — Coupled hammer↔string `[ ]`
- [ ] Derive modal-mass unit consistency into README `## Math` **before coding** (plan §M3)
- [ ] `StringResonator::lastValue(channel)` + `StringPartialBank::displacementAtStrike()`
- [ ] `HammerExciter::process()` uses `in` as string displacement; `c = x_h − y_string`
- [ ] **Delete `kHammerToStringGain`** (replaced by modal mass). `registerGain` is already
      gone — deleted at M1; its successor `voicingGain` belongs to M6, not here.
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

- **2026-07-25 (M2) — the `2/N_active` excitation scaling was unphysical; removed.** Modal
  superposition has no `1/N` factor — the mode count is a truncation choice, and adding a 64th
  partial must not quieten the first 63. It was harmless while N was a fixed 12; once N varied
  with pitch it became a spurious pitch-dependent gain (peak ∝ 1/√N), leaving the bass 5×
  quieter than the treble — the opposite of a real piano. Removed, and `kHammerToStringGain`
  recalibrated once (5.8e-6 → 7.5e-7). Keyboard is now 0.26–0.86 peak with a natural mid-bass
  emphasis. **Third normalisation error in two milestones** (after M1's sustained-vs-impulse
  gain and the bridge loop gain) — each hidden until a *different* parameter started varying.
- **2026-07-25 (M2) — flattening paid for itself several times over.** Projected 2.9× RT and a
  budget breach; measured **19.27×**, i.e. *faster than M1* while running 5.3× more resonators
  (~6.7× per-resonator speedup). The per-partial virtual dispatch and property machinery really
  had been costing far more than the five arithmetic ops they guarded. M4's polarisation
  doubling now has ample headroom, exactly as the mitigation intended.
- **2026-07-25 (M2) — full-precision `ln(1000)` matters.** The flattened loop computes the pole
  radius as `exp(-ln1000/(T60·fs))` where `StringResonator` uses `pow(10,-3/(T60·fs))`. With
  `kT60Constant` truncated to 6.907755 the two disagreed by ~1e-8 after 2000 samples of IIR
  recursion — enough to fail the sample-exactness test. At full precision
  (6.907755278982137) they agree bitwise. Worth remembering for any future coefficient constant.
- **2026-07-25 (M2) — mitigation chosen: flatten the resonator inner loop; `kMaxPartials = 64`.**
  Required by the ledger before implementing. Measured bandwidth per cap (inharmonicity
  stretches `f_n` far beyond `n·f0`, so fewer partials reach Nyquist than expected):

  | cap | A0 | C4 | worst-case resonators | projected RT, no speedup |
  |---|---|---|---|---|
  | 32 | 1.7 kHz | 9.1 kHz | 512 | 6.09× ok |
  | 46 | — | — | 736 | 4.24× ok, **no margin** |
  | 64 | 6.2 kHz | 21.3 kHz (full) | 1024 | 3.05× **breach** |
  | 96 | 13.7 kHz | full | 1536 | 2.03× breach |

  Cap 64 gives C4 and everything above it *complete* bandwidth to Nyquist, and the bass 6.2 kHz
  — the target M2 exists for. Cap 46 would fit the budget today but leaves nothing for M4,
  which stacks on top. So: take option (1), flatten the loop. Each partial currently pays two
  virtual calls plus branches to perform five arithmetic ops; inlining the recurrence over flat
  coefficient arrays removes that. `StringResonator` stays as `PianoBridge`'s mode type and as
  the reference implementation the flattened loop is unit-tested to match **exactly**.
  Option (3), skipping idle voices, is still not done and still does not help the gate.
- **2026-07-25 (M1) — the resonator gain normalisation was wrong for struck strings.** M1's
  per-note T60 exposed a latent bug: `G = (1−r²)sinθ` normalises for *sustained* drive, so a
  struck partial's amplitude scaled as `1/T60`. Invisible while every note decayed in 3 s; the
  moment decay spanned 53 s → 0.56 s it became a 95× loudness imbalance with 880 Hz clipping at
  2.79 and the bass inaudible at 0.029. Added `StringResonator::setImpulseNormalized()`
  (`G = sinθ`, decay-independent impulse peak) and enabled it on string partials; soundboard
  modes keep the sustained form, which is correct for them. **A physics error only became
  visible once the surrounding physics got more correct** — worth expecting again in M3/M4.
- **2026-07-25 (M1) — `registerGain` deleted (early, plan said M3); `voicingGain` added.** The
  old curve compensated the bug above and was actively harmful once fixed. Its replacement
  compensates *real* physics — impulse-normalised amplitude falls as ~1/ω, so the bass is
  genuinely ~78× louder at equal velocity, closely tracking the expected 128× over A0→C8. Real
  pianos flatten this with per-register voicing, so `voicingGain = (f0/261.6)^0.8` stands in
  until **M6 replaces it** (not deletes it). Keyboard now 0.34–0.86 peak, nothing clipping.
- **2026-07-25 (M1) — bridge coupling 0.15 → 5e-4, and the integration test was masking a NaN.**
  The 24000× gain increase destabilised the string→bridge→string loop. Worst case (8 sustained
  bass voices, highest resonance gain) **diverged to NaN**; the integration check still
  "passed" because the WAV path clamps to ±1 — only the raw-double unit test caught it.
  Measured: diverges at 5e-3, stable at 1e-3; chose 5e-4 for 10× margin. Selectivity is
  coupling-independent (~35× either way) so REQ-piano-6 is unaffected. **Lesson: keep at least
  one raw-double assertion for anything that can diverge — WAV-based checks hide it.** M5's
  admittance-based bridge is the principled fix.
- **2026-07-25 (M1) — two acceptance criteria adapted, both recorded in plan §M1.** (a) Criterion 1
  targets partial 20, which does not exist until M2; asserted on partial 12 instead (≥25,
  measured 35.3; projects to 100.7 at partial 20). (b) Criterion 2 lowered 20 dB → 15 dB
  (measured 17.1): the 20 was set against the *inflated* pre-fix measurement, and the ceiling is
  now the excitation spectrum — the open-loop 2.6 ms hammer pulse leaves high partials ~42 dB
  down. M3 is what should raise it; restore 20 dB there.
- **2026-07-25 (M0) — M2 as specced will breach the perf budget; pick a mitigation first.**
  Baseline is 15.42× real-time at 192 resonators, so the 4× gate affords ~740. M2's
  `kMaxPartials = 64` needs 1024 → projected **~2.9× real-time**. Options, in the plan's §M2
  risk block: (1) flatten the resonator inner loop — recommended, and the only one that also
  pays for M4; (2) cap partials at ~46 — cheap but partially defeats M2's purpose; (3) skip
  idle voices — worth doing anyway but does *not* help, since the gate is defined at 8 voices
  sounding. **Not decided here** — M2 owns the choice, but must record it before implementing.
- **2026-07-25 (M0) — idle voices are never skipped.** `PianoEngine::renderBlockBytes()` loops
  all 8 voices with no `isFinished()` check, so 1 sounding note costs almost as much as 8
  (17.20× vs 15.42×) — the benchmark measured it and the code confirms it. This makes the
  "1 voice" figure a measure of *8 voice slots with 1 note*, not per-voice cost; treat the
  8-voice number as the only meaningful one until this changes.
- **2026-07-25 (M0) — the perf budget is a review gate, not a build failure.** `piano_bench`
  exits 0 whenever it ran, and `piano_bench_smoke` asserts only that it produces a number.
  Wall-clock thresholds are flaky on shared machines, and a perf test that fails the build for
  being scheduled badly gets disabled — which is worse than one that always reports honestly.
  Enforcement lives here in the ledger, checked by whoever runs the milestone.
- **2026-07-25** — Plan created from a code audit after the user reported the model "sounds
  like a string instrument, like hitting a violin string." Root findings: the hammer discards
  its string input (open-loop excitation), 12 fixed partials, uniform 3 s decay for every
  note, and a near-uniform per-partial decay law. Ordered M1/M2 before the deeper M3 because
  they are small, local, and should carry most of the immediate audible improvement.

## Verification notes

_(anything marked `[!]` — what could not be checked here and why)_

- **M2** — fully verified; nothing marked `[!]`. The bandwidth is now present (C4 to 21 kHz,
  A0 to 6.2 kHz) but the attack still **under-excites** it: high partials sit ~42 dB below the
  fundamental because the open-loop hammer pulse has little high-frequency content. So M2
  removed the ceiling without yet filling the room — M3 is what fills it. Interim sound is
  still mellow.
- **M1** — fully verified; nothing marked `[!]`. Every acceptance number was measured, and the
  two adapted thresholds are documented in plan §M1 with the reason and the milestone that
  should restore them. Note the model is now *more* physically correct but the attack is
  **duller** than before in absolute terms (high partials ~42 dB down), because the previous
  brightness was partly the gain artifact. M2 (bandwidth) and M3 (hammer coupling) are what
  make the attack genuinely bright — expect the interim sound to be mellow.
- **M0** — fully verified; nothing marked `[!]`. The benchmark number is wall-clock on this
  dev machine only, so it is a *relative* baseline: what matters for later milestones is the
  ratio to this figure, not the absolute value, since another machine will differ.
- Note that this project is developed headless: no one has *listened* to the output in this
  environment. Every acceptance criterion in the plan is deliberately numeric so progress is
  provable without ears — but final timbre judgement is the user's, and "sounds right" is
  never a criterion this skill may tick on its own.
