# Piano Physics Upgrade — Progress Ledger

**State lives here, not in anyone's memory.** The `arstro.piano.implement` skill reads this
file first and updates it last, every session. Spec:
[`piano-physics-plan.md`](piano-physics-plan.md).

- **Last updated:** 2026-07-25 (M4 complete)
- **Last commit:** M4 — two transverse polarisations (double decay)
- **Perf budget:** ≥ 4× real-time, 8 voices @ 48 kHz (`REQ-piano-17`, plan §M0)

### Perf log

| After | 8 voices | 1 voice | Resonators | Budget |
|---|---|---|---|---|
| **M0 baseline (pre-upgrade)** | **15.42× RT** (median 15.32×) | 17.20× | 192 | ✅ met |
| **M1** (loss model) | **16.25× RT** | 18.35× | 192 | ✅ met |
| **M2** (64 partials + flattened loop) | **19.27× RT** (median 18.57×) | 20.75× | ~1024 | ✅ met |
| **M3** (hammer↔string coupling) | **14.96× RT** | — | ~1024 | ✅ met (3.7× margin) |
| **M4** (two polarisations) | **13.15× RT** | — | ~1280 | ✅ met (3.3× margin) |

Measured by `./build/piano_bench` (5 passes × 10 s, best-of). M2's resonator count is
pitch-dependent (bass fills the 64 cap, treble uses ~5); ~1024 is the benchmark chord's worst
case. **M2 runs 5.3× more resonators than M1 yet is faster** — flattening the inner loop gave
a ~6.7× per-resonator speedup, which is why the projected 2.9× breach never happened.

---

## ► NEXT

**M5 — Soundboard / bridge.** Everything above 700 Hz currently radiates *completely
uncoloured*: `PianoBridge` has 8 modes spanning 80–700 Hz, so the treble is naked resonators —
music-box territory. Extend the modal set to ~30–40 modes covering 50 Hz – 5 kHz with realistic
plate modal density and radiation rolloff.

⚠ **Requirement conflict if you take the shortcut.** Commuted synthesis / convolving a measured
soundboard impulse response would be cheaper and very effective, but contradicts `REQ-piano-14`
(measured IR / room acoustics explicitly out of scope). Per rule 5, **amend the requirement
first** if choosing that route — the modal-extension route has no conflict.

Also worth doing here: `PianoBridge`'s coupling gain has now been rescaled twice (M1, M3)
purely to chase stability as the drive path's units changed. M5's admittance-based bridge —
where the bridge sets string damping *and* radiation, making the loop self-limiting — is the
principled fix, and would retire `kVelocityToSignal` too.

Budget: M4 leaves **13.15× RT**, 3.3× above the gate.

---

## At a glance

| M | Milestone | Status |
|---|---|---|
| M0 | Perf baseline & budget | `[x]` |
| M1 | Frequency-dependent loss model (spectral evolution) | `[x]` |
| M2 | Pitch-dependent partial count (bandwidth) | `[x]` |
| M3 | Coupled hammer↔string interaction | `[x]` |
| M4 | Two transverse polarisations (double decay) | `[x]` |
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

### M3 — Coupled hammer↔string `[x]`
- [x] Derived modal-mass unit consistency into README `## 6` **before coding** — impulse-invariant
      discretisation gives `G_n^disp = sinθ_n·g_n/(m·ω_n·f_s)`, and since velocity is what
      radiates the audio path uses `G_n^vel = sinθ_n·g_n/(m·f_s)` (the `1/ω_n` cancels)
- [x] `StringPartialBank::displacementAtStrike()` = `Σ (g_n/ω_n)·y_n`, accumulated in the same
      loop as the audio sum — one extra multiply-add per partial
- [x] `HammerExciter::process()` uses `in` as string displacement; `c = x_h − y_string`
- [x] **`kHammerToStringGain` deleted** — the force→displacement path is now fixed by
      `kModalMass`. What remains is `kVelocityToSignal`, an honest velocity→signal
      transduction constant that M5's bridge should absorb
- [x] Contact duration varies with **pitch**: 5.2 ms (C2) → 1.1 ms (C8) at fixed velocity —
      structurally impossible before this milestone
- [x] Contact/period spans **0.46 → 12.0**: the bass hammer leaves before the reflection
      returns, the treble stays engaged across 12 periods (criterion 4)
- [x] Contact still shortens monotonically with velocity (criterion 2)
- [x] Bass force pulse shows a post-peak local maximum — the reflection ripple (criterion 3)
- [x] Output bounded across the keyboard: 0.37–0.86 peak, `registerGain` long gone
- [x] Integration: spectral evolution rose to **20.5 dB**, so the plan's ORIGINAL 20 dB target
      was restored (M1 had lowered it to 15 pending exactly this milestone)
- [x] Re-run M0 benchmark: **14.96× RT** — budget met with 3.7× margin
- [x] 100 % line coverage held on all five `physical/` sources

### M4 — Two polarisations `[x]`
- [x] Derived the polarisation split into README `## 5b` **before coding**, including the
      geometric T60 split (the plan's `T60_v = T60_n` would have multiplied every note's ring
      time by up to 8× and undone §3's calibrated pitch→decay curve)
- [x] **Only the bridge-loss term `c1` splits**, not `c3·ω²` — the vertical/horizontal
      difference is bridge-coupling asymmetry; internal viscoelastic loss is a property of the
      wire and identical in both planes. This is what makes double decay a low-partial
      phenomenon *by physics* rather than by the `kPolarizedPartials` cap
- [x] Horizontal twins carry displacement weight **zero** — perpendicular to the hammer's
      compression axis, so they radiate but do not push back on the felt (README ## 6)
- [x] Corrected README §5's double-decay claim and **amended `REQ-piano-3`** accordingly
      (unison detuning delivers beating, not double decay)
- [x] Reference test extended to both planes — the flattened loop still matches
      `StringResonator` **sample-exactly**, so the split itself is pinned
- [x] Unit tests: prompt→aftersound ratio **5.4 (C3), 4.3 (C4)** (≥3), plus a trend test that
      the effect *weakens* toward the treble as physics requires (C5 2.0)
- [x] Integration test: **prompt 2.43/s → aftersound 0.57/s, ratio 4.27**
- [x] M1's spectral evolution held at **20.0 dB** (the first, uniform-`R` implementation
      collapsed it to 11.2 — see decisions log)
- [x] Re-run M0 benchmark: **13.15× RT** — budget met with 3.3× margin
- [x] 100 % line coverage held on all five `physical/` sources

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

- **2026-07-25 (M4) — the polarisation split belongs to the BRIDGE loss term, not to the whole
  decay rate.** The obvious first implementation applied one flat `R_pol` to all of `alpha_n`,
  shortening every partial's prompt decay by 2.83× — including the high partials, whose decay
  is set by internal viscoelastic loss and has nothing to do with which plane they vibrate in.
  Measured, the attack went 9 dB darker and M1's spectral-evolution figure collapsed
  **20.5 → 11.2 dB**. Splitting only `c1` (bridge/air) leaves partial 12 at 0.188 s vs 0.196 s
  unsplit while the fundamental still splits 2.9 s / 13.7 s. Bonus: double decay is now a
  low-partial phenomenon *by physics* rather than by the `kPolarizedPartials` cap, which
  becomes purely a CPU optimisation. **A milestone can silently regress an earlier
  milestone's criterion — every one of them stays in the suite for exactly this reason.**
- **2026-07-25 (M4) — the T60 split is geometric about T60_n, not `T60_h = R·T60_n`.** The
  plan's literal form would have multiplied every note's overall ring time by up to 8×,
  silently undoing M1's calibrated pitch→decay curve. Splitting geometrically keeps the
  geometric mean at `T60_n` and lands both planes on real-piano values (C4 prompt 2.4 s,
  aftersound 19.5 s; measured pianos ~1–2 s and ~10–20 s).
- **2026-07-25 (M4) — two acceptance criteria adapted, both to match physics rather than to
  pass.** (a) The ≥3× ratio is asserted for C3/C4 and replaced by a *trend* test above:
  because only `c1` splits, the effect's strength tracks how bridge-dominated a fundamental is
  (C3 89 % → 5.4×, C4 77 % → 4.3×, C5 50 % → 2.0×). That fall with pitch matches real pianos,
  where aftersound is a bass/mid phenomenon; demanding a flat ≥3× everywhere would demand the
  model be wrong. (b) The plan's fixed [0,0.5]/[1.5,3] windows were replaced by note-relative
  ones: the crossover scales with T60 (C4 1.2 s, C3 2.3 s, A0 9.3 s), so fixed windows measured
  5.5× at C4 but 2.6× at C3 *purely from window placement*.
- **2026-07-25 (M4) — polarisation detune cut 3e-4 → 3e-5.** At 3e-4 the polarisation beat
  period drops below the aftersound decay time from C5 up, so the beat masquerades as a decay
  slope and corrupts the very envelope M4 exists to produce. At 3e-5 it is 5–20× longer than
  the aftersound everywhere. Audible beating is unison detuning's job (README §5); the
  polarisation split's job is the double decay.
- **2026-07-25 (M3) — a coupling constant must be rescaled whenever the drive path's units
  change, and a "passing" test hid it.** M3 gave the string drive its physical
  `1/(kModalMass·f_s)` factor — ~48000× smaller — which left the bridge's sympathetic feedback
  ~125 dB down: inaudible, and *below 16-bit WAV resolution*. The integration check reported
  **"ratio 0.00 PASS"** because it only compared same-pitch against off-pitch, and `0 < 0` is
  false. Fixed both: coupling re-measured against the bass worst case (diverges at 50, grows at
  20, stable at 10 → **5.0**, a 4× margin, putting sympathetic response ~45 dB below the struck
  note as on a real piano), and the check now asserts a non-zero floor *first*. **Second time a
  vacuous/masked pass has hidden a real regression** (M1's NaN was hidden by WAV clamping) —
  every ratio assertion needs a magnitude floor beside it.
- **2026-07-25 (M3) — hammer stiffness recalibrated for the coupled loop.** Against a rigid
  wall `K = 1e10` gave 2.9–7.1 ms contacts; with the string yielding, the same K is too soft.
  Swept K against contact duration and chose **3e11**: C2 5.2 ms, C4 2.1 ms, C6 1.2 ms,
  C8 1.1 ms — matching real pianos (1–5 ms, longest in the bass) with the register trend
  emerging from the physics rather than being curve-fitted.
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

- **M4** — fully verified; nothing marked `[!]`. Notes now **bloom**: a fast prompt sound
  giving way to a long quiet aftersound, strongest in the bass/mid as on a real instrument.
  Still missing before this can be judged as intended timbre: **M5** (everything above 700 Hz
  is uncoloured — the single largest remaining gap) and **M6** (per-register voicing).
- **M3** — fully verified; nothing marked `[!]`. This is the milestone that addressed the
  original diagnosis, and the attack finally has structure: contact duration now depends on
  pitch, and the treble hammer stays engaged across 12 string periods where the bass leaves
  after half of one. Spectral evolution reached the plan's original 20 dB target. **The model
  is now qualitatively a piano rather than a struck string** — but M4 (bloom), M5 (soundboard
  colour above 700 Hz) and M6 (per-register voicing) are all still absent, so it should be
  judged as unfinished rather than as the intended timbre.
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
