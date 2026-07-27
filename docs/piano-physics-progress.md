# Piano Physics Upgrade — Progress Ledger

**State lives here, not in anyone's memory.** The `arstro.piano.implement` skill reads this
file first and updates it last, every session. Spec:
[`piano-physics-plan.md`](piano-physics-plan.md).

- **Last updated:** 2026-07-27 (M8 complete — tension modulation / attack pitch glide)
- **Last commit:** M8 — tension modulation (attack pitch glide), README ## 12.5
- **Perf budget:** ≥ 4× real-time, 8 voices @ 48 kHz (`REQ-piano-17`, plan §M0)

### Perf log

| After | 8 voices | 1 voice | Resonators | Budget |
|---|---|---|---|---|
| **M0 baseline (pre-upgrade)** | **15.42× RT** (median 15.32×) | 17.20× | 192 | ✅ met |
| **M1** (loss model) | **16.25× RT** | 18.35× | 192 | ✅ met |
| **M2** (64 partials + flattened loop) | **19.27× RT** (median 18.57×) | 20.75× | ~1024 | ✅ met |
| **M3** (hammer↔string coupling) | **14.96× RT** | — | ~1024 | ✅ met (3.7× margin) |
| **M4** (two polarisations) | **13.15× RT** | — | ~1280 | ✅ met (3.3× margin) |
| **M5** (128 soundboard modes) | **10.26× RT** | — | ~1408 | ✅ met (2.6× margin) |
| **M6** (per-register scaling) | **8.81× RT** | 9.56× | ~1900 | ✅ met (2.2× margin) |
| **M7** (longitudinal modes) | **7.92× RT** | — | ~1944 | ✅ met (2.0× margin) |
| **post-M7** (voice skipping) | **8.36× RT** | — | ~1944 | ✅ met (2.1× margin) |
| **M8** (tension modulation) | **11.79× RT** (median 11.66×) | 12.9× | ~1944 | ✅ met (2.9× margin) |

**M8's absolute figures are on a FASTER machine than M0–M7** (M0 re-measured here at 12.68×,
not the 15.42× of the original machine — bench numbers are relative, per the M0 note). The
honest reading is the *relative* cost: M8 dropped this machine's 8-voice figure 12.68× → 11.79×,
**~7%**, all of it the per-sample tension-envelope tracking + decimated re-tune on the bass/mid
voices whose `κ_t > 0` (the treble is gated off entirely, §12.5). No resonators added — M8
re-tunes existing ones. Budget met with 2.9× margin.

`piano_bench` holds all 8 voices *sounding*, so the voice-skipping optimisation cannot help it —
8.36× vs 7.92× is run-to-run noise, and that is the honest reading. Where skipping does help is
ordinary playing, which the benchmark deliberately does not represent: median block cost during
melodic playing (one note per 500 ms, keys released, no pedal) went **1.204 → 0.409 ms**, ~3×.

Measured by `./build/piano_bench` (5 passes × 10 s, best-of). M2's resonator count is
pitch-dependent (bass fills the 64 cap, treble uses ~5); ~1024 is the benchmark chord's worst
case. **M2 runs 5.3× more resonators than M1 yet is faster** — flattening the inner loop gave
a ~6.7× per-resonator speedup, which is why the projected 2.9× breach never happened.

---

## ► NEXT

**M9 — Tier-3 detail.** Four small, independent features (plan §M9), shippable in any order:
no dampers above ~MIDI 88; duplex/aliquot scale (treble shimmer); Stulov felt hysteresis
(replaces README §6's load/unload simplification); una corda done properly. Start the first
unchecked one.

⚠️ **Before implementing item 4 (una corda), amend `REQ-piano-16` first** — it currently
specifies the gain-reduction *approximation*, and real una corda (strike a subset of the unison
strings, the un-struck string driven only through the bridge) contradicts it. This is the one
**still-pending** requirement conflict; per `arstro.dsp.implement` rule 5, amend the requirement
with the reason and date *before* the code, not after.

M8 is complete and verified numerically. **This is the point the plan flagged (see M6's
verification note) where listening is worth more than another measurement** — M4–M8 have added
specific colours (bloom, soundboard, per-register voicing, bass growl, attack glide) on top of a
model that was already qualitatively a piano at M3. M9's items are refinements, not missing
mechanisms. Consider the §"When every milestone is done" whole-project review + a fresh render
for the user to hear, either after M9 or if the user wants to judge the timbre now.

Budget headroom on this machine: **11.79× RT**, 2.9× above the gate. M9's items are cheap
(damper gating and una-corda are free; duplex adds a few resonators per treble note; Stulov
hysteresis is a per-sample felt-state update, not a partial-loop change).

---

## Since M7 — work that is not a milestone

Done between M7 and M8, on user reports rather than the plan. Recorded so a later session does
not rediscover any of it. None of it changes the physics.

- **A parallel-compute layer exists** (`src/compute/`, [`parallel-architecture.md`](parallel-architecture.md),
  `REQ-compute-1..6`): `ParallelExecutor` + `SerialExecutor` + `ThreadPoolExecutor`, selected via
  `ComputeConfig`, built on the pre-existing `platform::Thread` seam. **Off by default, and
  measured NOT to pay yet** — `SynthEngine` blocks are too cheap (break-even at 512 frames,
  0.68× at 64). `PianoEngine` is *not* wired to it: block-level voice parallelism needs the
  bridge feedback delayed by one block, which is an approximation nobody has justified yet.
  Amdahl ceiling for the piano graph is 4.14× at 8 workers (`s = 0.133`, the bridge).
- **The app's "lag" was never DSP.** Measured 17–20 % of the real-time budget at every block
  size while it still lagged: the ALSA buffer was 50 ms. Latency and throughput are different
  axes (`REQ-compute-1`) — do not accept a parallelism argument for a latency complaint.
- **The output limiter had become a gain stage.** M6/M7 changed per-voice levels and
  `PianoEngine::kMasterGain` was never restaged: `tanh` was squashing peaks 58 % at three
  voices and 91 % at eight, i.e. chords driven near square. Now 0.22, verified by how *often*
  the limiter engages (1–3 voices: 0.000 % of samples). **Any milestone that changes per-voice
  level must re-check this** — it is the second time a downstream constant silently rotted.
- **Silent-voice skipping** (README `## 13`): a voice is frozen only when fully damped *and*
  inaudible. The damper half is a safety property, not an optimisation detail — an undamped
  string can still be re-excited through the bridge, so freezing one would break `REQ-piano-6`.
- **Render never misses its deadline.** 30 s of realistic playing at 256 frames: median 1.20 ms,
  p99.9 2.01 ms, max 3.27 ms, **zero** blocks over a 5.33 ms budget, flat over time. Remaining
  audio glitches on the dev machine are *scheduling* (rtprio 0 + PulseAudio), not DSP — see the
  skill's §"Setting up on a new machine".

---

## At a glance

| M | Milestone | Status |
|---|---|---|
| M0 | Perf baseline & budget | `[x]` |
| M1 | Frequency-dependent loss model (spectral evolution) | `[x]` |
| M2 | Pitch-dependent partial count (bandwidth) | `[x]` |
| M3 | Coupled hammer↔string interaction | `[x]` |
| M4 | Two transverse polarisations (double decay) | `[x]` |
| M5 | Soundboard / bridge | `[x]` |
| M6 | Per-register voicing | `[x]` |
| M7 | Longitudinal modes & phantom partials | `[x]` |
| M8 | Tension modulation (attack pitch glide) | `[x]` |
| M9 | Tier-3 detail | `[~]` |

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

### M5 — Soundboard / bridge `[x]`
- [x] Chose the **modal-extension** route — no `REQ-piano-14` conflict (the commuted/measured-IR
      shortcut would have needed the requirement amended first)
- [x] 8 modes (80–700 Hz) → **128 modes (50 Hz – 5 kHz)**, spaced uniformly in FREQUENCY
      because a plate's modal density is constant in Hz (`ω ∝ k²`) — a harmonic series is the
      wrong model here — with deterministic golden-ratio jitter so the grid cannot ring as a comb
- [x] Frequency-dependent mode damping so **modal overlap** grows with frequency: isolated
      resonances low (overlap 0.16 at 100 Hz, as a real board has), smooth coloured continuum
      high (overlap ~1.0 at 5 kHz). This is how a real board compensates our ~5× undersampling
      of its true modal density
- [x] **`1/sqrt(M)` per-mode normalisation** — modes sum incoherently, so a bare sum scales as
      `sqrt(M)` and any mode-count change silently rescales the string→bridge→string loop gain.
      That is exactly why the coupling gain had to be re-tuned at M1 *and* M3; it did **not**
      need re-tuning here despite 16× more modes
- [x] Unit test: transfer function **10.0 dB variation** above 700 Hz (≥6), plus a test that
      every octave band 125 Hz – 4 kHz carries real energy
- [x] Integration test: same 10.0 dB measured end-to-end through the WAV path
- [x] Sympathetic selectivity improved **66× → 288×** (denser bridge = sharper discrimination)
- [x] Loop stability re-verified at the worst case (8 sustained bass voices): stable at the
      **unchanged** coupling gain of 5.0; diverges only at 50
- [x] Re-run M0 benchmark: **10.26× RT** — budget met with 2.6× margin
- [x] 100 % line coverage held on all five `physical/` sources

### M6 — Per-register voicing `[x]`
- [x] Derived all five laws into README `## 11` **before coding**, each fitted to real
      concert-grand design values and each stated with its error against them
- [x] **Modal mass `m(f0)` graded — the scope the plan's table did not list, and without which
      the milestone would have made the physics *worse*.** M3 fixed the structure (the drive
      gain must carry a real `1/m`) but left `m = 1` at every pitch. Grading the hammer mass
      against a constant string mass inverts the `m_h/m` ratio the contact actually depends on
      (see decisions log). `m(f0) = (f_ref/f0)^1.62` reproduces a real 3400× span within ±22 %
- [x] Hammer mass `m_h(f0)` (11.3 g A0 → 3.9 g C8), felt stiffness `K(f0)`, strike position
      `β(f0)` (1/8 bass → 1/15 top), unison count `U(f0)` (1 / 2 / 3 by stringing scale)
- [x] `m_h/m` now spans **0.15 (A0) → 184 (C8)** vs real ≈ 0.19 → 230 — two physically
      *opposite* contact regimes, from laws fitted independently of each other
- [x] **`voicingGain` deleted** (README `## Units`), as promised at M1. `kVelocityToSignal` is
      now the only free scalar in the model
- [x] All five laws overridable with the latch pattern §3's `T60_1` established; unit-tested
      that an override survives a later `setFrequency()`
- [x] **Energy-conservation guard on all 88 keys × 4 velocities** — worst case 0.93, and it is
      what caught this milestone's real bug (see decisions log). This is now the permanent
      regression test for the contact loop's numerical validity
- [x] `K(f0)` capped by a derived stability bound `C_stab·m_red(f0)`; documented as a 48 kHz
      *sample-rate* limitation with the convergence measurement that proves the diagnosis
- [x] Unit tests: centroid rises 193 → 395 Hz across the keyboard; `centroid/f0` collapses
      76× (a keyboard of transpositions holds it constant); unison boundaries at the real
      stringing breaks; contact regrading vs the ungraded baseline
- [x] Integration test: **centroid 198 → 396 Hz, `centroid/f0` collapses 76× A0→C8**, measured
      end-to-end through the WAV path
- [x] **`REQ-piano-3` amended** — it required "2+ detuned unison strings" on every note, which
      the single-strung bass of `## 11.5` contradicts
- [x] M1's spectral evolution held at **20.0 dB** (it regressed to 12.1 mid-milestone — see
      decisions log); M4's double decay 3.90, M5's 10.0 dB, sympathetic 256× all held
- [x] Fixed pre-existing doc drift in README `## 4`: it still documented the `(2/N)` excitation
      factor M2 removed
- [x] Re-run M0 benchmark: **8.81× RT** — budget met with 2.2× margin
- [x] 100 % line coverage held on all five `physical/` sources

### M7 — Longitudinal modes & phantom partials `[x]`
- [x] **`REQ-piano-15` amended first** — it declared this out of scope; rewritten to *require*
      genuinely phantom partials, emergent from a nonlinear coupling rather than added at
      hand-picked frequencies
- [x] Derived README `## 12` **before coding**: `f_long,m = m·c_L/(2L)` from the steel bar speed
      (5200 m/s) and §11.1's string-length fit — **1300 Hz at A0**, matching real bass strings
- [x] New `LongitudinalBank`, built from `StringResonator` + `equalizer::LowPassFilter` — **no
      new primitives** (rule 1); the same reuse pattern `PianoBridge` established
- [x] **Which signal to square turned out to be the whole milestone** — the strike-point
      displacement (the obvious choice, already computed for M3) weights mode `n` by `1/n` and
      produced *zero* phantom energy. The driving term integrates the string's **slope**, which
      weights by `n`, and the bank's velocity sum already **is** that. See decisions log
- [x] Quasi-static (direct) response **plus** resonances, so phantoms exist at combination
      frequencies that do not happen to land on a longitudinal mode — which is most of them
- [x] DC blocker `HP(x) = x − LPF(x)`: `y²` is non-negative, and a two-pole resonator's DC gain
      is ~6, not 0. Residual offset **0.19 % of peak**, asserted < 1 %
- [x] Unit tests: phantom at a frequency ≥ 12 Hz from every `f_n` with longitudinal dominating
      transverse; velocity exponent **2.39** vs the fundamental's 0.85; phantom share 620× larger
      at full velocity than at 0.02; geometry-not-pitch (`f_long/f0` is 47× at A0, 16× at C4);
      treble bank fully disabled; no DC; loop stable at **10× the shipped coupling**
- [x] Integration test: **phantom at 1280 Hz (17 Hz from any partial), 62× the transverse
      content there, growing as v^2.35** — measured end-to-end through the WAV path
- [x] Bass-weighted and switched fully OFF above ~C5: **44 longitudinal resonators** across the
      entire benchmark chord (+2.3 % of the resonator count)
- [x] `StringResonator::reset()` re-added — M2 deleted it as dead code when it lost its only
      caller; it has one again, and that is the right reason to bring it back
- [x] M1 spectral evolution 19.9 dB, M4 double decay 3.89, M5 treble colour 10.0 dB, M6
      centroid collapse 78×, sympathetic 726× — all held
- [x] Re-run M0 benchmark: **7.92× RT** — budget met with 2.0× margin
- [x] 100 % line coverage held on all **six** `physical/` sources

### M8 — Tension modulation `[x]`
- [x] Derived README `## 12.5` **before coding**: `f0(t)=f0·(1+κ_t·E(t))`, `E=LPF(slope²)` —
      the **DC half of §12.2's slope-square quantity**, the exact static tension rise M7's DC
      blocker discards. First-order `cos(θ(1+δ)) ≈ cosθ − θ·sinθ·δ` re-tunes every partial with
      **no per-partial trig**, decimated to once per 64 samples off the hot path
- [x] `REQ-piano-18` **added** — no requirement covered tension modulation (a gap, not a
      conflict; rule 5 needs a written requirement before the work)
- [x] Lives **inside `StringPartialBank`** (self-modulating, per string): `setTensionModulation`,
      a one-pole `E(t)` envelope (τ≈13 ms), the decimated bend, `±kMaxPitchDelta` stability clamp
- [x] `PianoVoice::defaultTensionModulation(f0)` register law `κ_ref·(f_ref/f0)^3`, floored at
      C2 (deep-bass cap) and **switched off above C5** (glide < 0.05 c — inaudible — so the
      treble pays nothing per sample); override-latched like the §11/§12 laws
- [x] **Acceptance (plan §M8), measured exactly via `pitchModulation()` introspection**
      (`f0(t)=f0(1+δ)`, so δ *is* the f0 shift): C2 fortissimo **peak δ = 2.77 c** in the first
      50 ms, glide-vs-1 s **2.48 c** (≥ 2 ✓); C2 pianissimo **0.12 c** (< 0.5 ✓)
- [x] Velocity² law asserted (glide grows ~v², the same signature as §12.2's phantom) and
      treble-negligible (C7 glide ~0.001 c, now exactly 0 above C5)
- [x] Integration test `piano_pitch_glide`: differences a glide-on/off render (common-mode onset
      transient cancels — §12.2's lesson, in the phase domain) → accumulated-phase estimator over
      [30,130] ms **matches ground truth to ~10%**: hard **1.54 c** vs soft **0.06 c** (26×),
      **uniform across partials** (p2 1.67 c ≈ p4 1.54 c — proves a re-tune, not a fundamental detune)
- [x] All prior criteria re-checked: M1 spectral 19.8 dB, M4 double decay 3.89, M5 colour 10.0 dB,
      M6 centroid 78×, **M7 phantom still dominates** (see decisions log — the glide *did* nudge it)
- [x] Re-run M0 benchmark: **11.79× RT** (median 11.66×) — budget met with 2.9× margin
- [x] 100 % line coverage held on all **six** `physical/` sources

### M9 — Tier-3 detail `[~]`
- [x] **M9.1 — No dampers above ~MIDI 88.** `defaultHasDamper(f0) = f0 < √(f(88)·f(89)) ≈
      1357 Hz` (geometric mean, so the boundary is between notes — MIDI ≥ 89, the top 20 keys,
      ring undamped). `noteOff()` skips both the §7 ramp and the §10 damper noise when false.
      README `## 7.1`; `REQ-piano-7` amended with the exception. Verified: C7 released tail =
      **1.00× held** (release is a no-op), C6 damped tail 0.6× (damper still works below cutoff);
      the `isSilent()` freeze correctly never fires on an undamped voice (README ## 7.1). 100 %
      coverage held; bench **11.65× RT** (item is off the hot path — `noteOff` isn't benchmarked)
- [ ] Duplex / aliquot scale
- [ ] Stulov felt hysteresis (replaces README §6's load/unload simplification)
- [ ] Una corda proper — **amends `REQ-piano-16`**
- [ ] Re-run M0 benchmark, record

---

## Decisions & deviations log

_(newest first — record anything that departs from the plan, or resolves an open choice, so
it is never re-litigated)_

- **2026-07-27 (M8) — the ledger's "tap M7's DC tracker, it may be nearly free" framing is
  right about the QUANTITY and wrong about the FILTER.** M8's driving signal genuinely is the DC
  half of §12.2's `slope²` — the same quantity, both halves used, exactly as the ► NEXT note
  predicted. But M7's DC blocker runs at a **60 Hz** corner, and a bottom-octave note's `slope²`
  ripples at `2·f0 ≈ 55 Hz`; feeding that straight to pitch would wobble the note audibly. So M8
  computes its own envelope from the same `slope²` with a **slower ~12 Hz corner** (τ≈13 ms). It
  is not a second *quantity*, just a second (correct) *corner* — the reuse the ledger wanted, one
  filter constant short of literal. Also: the glide is **per-string** (each `StringPartialBank`
  self-modulates from its own slope), the opposite of §12.4's one-bank-per-voice longitudinal
  choice, because the re-tune has to land on *that* string's own partials.
- **2026-07-27 (M8) — re-tuning 64 partials never calls a single `cos`.** `update()` is a
  `pow`/`sin`/`cos` loop the ledger warned must not run per sample. The pitch shift is ≤ 2 cents
  (`δ ≲ 1.2e-3`), so `cos(θ(1+δ)) ≈ cosθ − (θ·sinθ)·δ` is exact to `~(θδ)²/2 ≲ 1e-5`: store
  `b_n = −θ·sinθ` in `update()` (where `θ`, `sinθ`, `cosθ` already exist) and the bent coefficient
  is one multiply-add per partial, folded into the damper's existing `recomputeEffectivePartials`
  pass and **decimated to every 64 samples**. The physics was a better derivation, not a bigger
  budget — the same lesson M7 learned about its drive signal.
- **2026-07-27 (M8) — the exact criterion is asserted by introspection; the audio test proves
  it survives the WAV.** A 2-cent shift on a *bass onset* cannot be resolved to that precision by
  any single-window Goertzel/zero-crossing — the pitch changes within the window and the
  broadband attack swamps the bin (the same reason M7 had to difference two renders). So the unit
  test asserts the plan's literal ≥ 2 c / < 0.5 c on `pitchModulation()` — which *is* the f0 shift
  exactly — and the integration test differences a glide-on/off pair (common-mode transient
  cancels) and reads the **accumulated phase** of the ratio, which matches ground truth to ~10%.
  Two measurements of one thing, each honest about what it can see. **Same adaptation pattern as
  M1/M4**: assert what is measurable, document why.
- **2026-07-27 (M8) — the glide nudged M7's phantom test, and that is physically correct.** With
  the glide on by default, A0's transverse partials shift up to ~1.7 c during the attack, so the
  phantom test's nearest-transverse-partial gap moved 17 → 15 Hz and its longitudinal/transverse
  ratio 62× → 36×. It still passes with wide margin (36× dominance, still growing as v^2.37), and
  the movement is real — the glide genuinely moves those partials. Left as-is rather than
  disabling the glide in that scenario, which would hide a true interaction. **Third time a
  milestone has touched an earlier one's criterion** (M4, M6 before it); this time it bent it, did
  not break it, and the suite noticed either way.
- **2026-07-27 (M8) — `κ_t` grows toward the bass, because the raw energy does the opposite.**
  Intuition says a "bass effect" needs a bass-weighted gain, but the *measured* slope-energy `E`
  is actually ~10× larger at C5 than at A0 (a top-octave note's modal-velocity sum is larger in
  the normalised system). So `κ_t` must fall as `(f_ref/f0)^3` just to make the glide a bass
  phenomenon, and even then a bare cube gives A0 an 861× multiplier and ~20 c of glide — hence the
  **C2 floor** (κ_t flat through the bottom octaves) and the **C5 cutoff** (κ_t = 0 above, glide
  < 0.05 c). Measured profile: A0 1.8 c, C2 2.5 c, A2 0.65 c, C4 0.17 c, C5 0.045 c, C7 0. **The
  register weighting had to be measured, not reasoned — the same trap M6's modal-mass grading was.**

- **2026-07-26 (M7) — WHICH transverse signal you square is the entire milestone, and the
  obvious choice produces nothing.** The strike-point displacement was already computed for M3's
  hammer coupling, so squaring *that* was the natural implementation — and it yielded **zero**
  phantom energy: measured at the widest gap in A0's partial series, turning the coupling on
  changed the spectrum by 0.0 %. The reason is a single power of `ω`. `displacementAtStrike`
  weights mode `n` by `g_n/ω_n`, so its spectrum is dominated by the fundamental and its square
  lands almost entirely below 500 Hz — nowhere near the longitudinal modes at 1.3 kHz and up.
  The true driving term `∫(∂y/∂x)²dx` integrates the *slope*, which weights mode `n` by `n`, and
  since §6 already outputs modal **velocity** (`y_n = ω_n·q_n ∝ n·q_n`), the bank's own audio sum
  *is* the slope — free, and correct. **The fix was a better derivation, not a bigger constant.**
- **2026-07-26 (M7) — a nonlinear stage needs its coupling constant calibrated against the
  SQUARE of the signal scale, which is not where intuition puts it.** The first `κ` was chosen by
  reasoning about the transverse amplitude and came out ~5 orders of magnitude too small, so the
  stage was inaudible and looked broken in exactly the same way the wrong drive signal did — two
  distinct failures presenting identically. Worth separating next time: check *whether* a stage
  contributes (isolate it by differencing) before tuning *how much* it contributes.
- **2026-07-26 (M7) — differencing two renders is the right way to test an added stage.** Render
  the same note with the coupling on and off, subtract, and the remainder *is* the new stage's
  output, exactly. That turned "is this a phantom partial or just spectral leakage?" — which the
  first attempt could not answer, because the attack transient is broadband and swamps a single
  bin — into a provable statement, and it is what both the unit and integration tests now use.
- **2026-07-26 (M6) — a "voicing" milestone had to grade the STRING, or it would have made the
  physics worse.** Plan §M6's table lists four properties, all of them hammer/geometry; none is
  the string's modal mass. But README §6 records that the contact is governed by the **ratio**
  `m_h/m`, and M3 left `m = 1` at every pitch. Grading `m_h` alone would have given bass 5.9 and
  treble 2.1 — real pianos are ≈ 0.19 and ≈ 230, i.e. the ratio would have been *inverted*, and
  a uniform model would have been closer to the truth than the "improved" one. Grading both gives
  0.15 → 184 from two laws fitted independently of each other. **Scope defined by the physics the
  existing code already documented, not by the plan's table.**
- **2026-07-26 (M6) — the milestone's real bug was numerical, and only an ENERGY measurement
  found it.** Grading the modal mass makes a top-octave string ~3400× lighter, and the contact
  ODE is integrated explicitly against a one-sample-delayed string displacement. Below ~1.7 ms of
  contact the loop stops resolving and starts *manufacturing* energy: measured **201× energy gain
  at C8**, which surfaced only as "the keyboard's peak spread is 85×, treble-loud". Peak, RMS,
  contact duration and spectrum all looked merely *odd*; the ratio `E_string/E_hammer` said
  "impossible" immediately. Fixed by capping `K` at `C_stab·m_red` (README §11.3), and the
  measurement became the permanent test — all 88 keys × 4 velocities, worst case 0.93.
  **The lesson generalises past this project: when a coupled physical model looks wrong, measure
  a conserved quantity, not an output.**
- **2026-07-26 (M6) — the model's treble felt is softer than a real piano's, and that is a
  sample-rate limit, not a voicing choice.** The stability cap binds from ~C5 upward, so across
  the top ~2.5 octaves `K` is set by what 48 kHz can resolve rather than by §11.3's physical law.
  Oversampling the contact loop is the principled fix and it *converges*: the C8 energy ratio
  goes 201 → 15.3 → 0.002 at 1×/2×/4×. Recorded rather than hidden — the treble is duller than
  the real instrument, by a known mechanism, with a known fix that was out of scope here.
- **2026-07-26 (M6) — deleting `voicingGain` moved the keyboard from flat-in-PEAK to
  flat-in-LOUDNESS, which is the correct trade.** Peak spread went 2.5× → 15.7× and that looked
  alarming until measured properly: 300 ms RMS spans 6× with its maximum in the mid register,
  falling toward both ends — a real piano's contour at constant key velocity. The peak spread is
  crest factor: the treble's 5 partials align where the bass's 64 do not. **Peak was the wrong
  loudness statistic across registers all along**; it only looked right while a compensating
  gain curve was forcing it flat.
- **2026-07-26 (M6) — M1's spectral evolution regressed 20.0 → 12.1 dB mid-milestone.** The
  first calibration chose `K` purely for contact duration and landed 30× softer at C4, which
  directly dulls the attack. Restoring the mid-register `K` and confining the cap to where it is
  numerically *required* brought it back to exactly 20.0 dB. **Second milestone running to
  regress an M1 criterion** (M4 was the first) — the suite keeps every past criterion for this
  reason, and both times it was the one that noticed.
- **2026-07-26 (M6) — the ledger's own perf prediction was wrong: 10.26× → 8.81×, not flat.**
  M5's note said "M6 adds no resonators (it re-parameterises existing ones)". §11.5 does change
  the count: the bass drops 2→1 string but the whole mid and treble go 2→3, so the resonator
  total rises ~35 %. Budget still met with 2.2× margin, but M7 must be costed *before*
  implementing — it is the first milestone to approach the gate.
- **2026-07-25 (M5) — normalise a modal bank by `1/sqrt(M)`, or its mode count becomes a hidden
  stability parameter.** Modes at different frequencies sum incoherently, so a bare sum scales
  as `sqrt(M)`. Going 8 → 128 modes would have multiplied the string→bridge→string loop gain 4×
  and forced a *third* coupling-gain re-tune (after M1 and M3). With the normalisation the gain
  of 5.0 was left untouched and the worst case (8 sustained bass voices) stayed stable. The mode
  count is now a realism knob, not a stability knob.
- **2026-07-25 (M5) — modal OVERLAP, not mode count, controls how smooth a plate sounds.** A real
  board has 250–500 modes in 50 Hz–5 kHz; 128 undersamples that ~5×, and at realistic Q it rings
  as a row of isolated resonances. Measured the trade-off directly: at damping slope 0.9 the
  response was over-smoothed to **4.8 dB** variation (failing the ≥6 criterion from the wrong
  direction — too flat, not too peaky); at slope 0.45 with 128 modes it lands at **10.0 dB**,
  matching a real board's ripple, with overlap ~1.0 at 5 kHz and 0.16 at 100 Hz. Damping slope
  turned out to be the dominant control, not mode count.
- **2026-07-25 (M5) — the admittance-based bridge is deferred, deliberately.** Plan §M5's stretch
  goal (bridge admittance sets string damping *and* radiation, making the loop self-limiting and
  retiring `kVelocityToSignal`) would rewrite M1's calibrated pitch→decay curve, since string
  decay would stop being an independent parameter. That is a milestone-sized change with real
  regression risk against M1/M3/M4, not a stretch on top of M5. Recorded here so it is not
  silently forgotten: it remains the principled fix for the coupling-gain rescaling and for
  `kVelocityToSignal`.
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

- **M8** — fully verified; nothing marked `[!]`. The note now **starts sharp**: a C2 fortissimo
  reaches 2.77 cents sharp within the first 50 ms and glides down to nominal, a pianissimo barely
  0.12 c, and the treble does not glide at all — all confirmed both by exact `pitchModulation()`
  introspection and, independently, through the rendered 16-bit audio. Three honest points to
  carry forward. (1) **`κ_t` is calibrated, not derived** — its SI value is `½·(1/T)·(EA/2L)` but
  every term is in §6/§11.1's normalised system, so like §12.2's `κ` it was set against the level
  a hard bass blow shows (~2.5 c at C2). (2) **The register weighting is empirical** — `(f_ref/f0)³`
  with a C2 floor and a C5 cutoff, fitted so the glide is bass-only against a slope-energy that
  actually rises toward the treble; it is not a first-principles tension model. (3) **A 2-cent
  bass-onset pitch shift is below what a single windowed Goertzel/zero-crossing can resolve**, so
  the *audio* test measures the glide over a settled [30,130] ms window via a differenced
  phase estimator (validated against ground truth to ~10%), while the plan's literal peak
  criterion is carried by the introspection unit test. Perf margin is now the widest since M5 on
  this machine (2.9×), but M8's cost is *relative* ~7% and lives on every bass/mid voice's
  per-sample path — the number to watch if M9's duplex scale adds treble resonators.
  **From here the model should be judged by ear** (see M6's note): M4–M8 are colours on a model
  already qualitatively a piano at M3, and M9 is refinement, not missing mechanism.
- **M7** — fully verified; nothing marked `[!]`. The bass now has a genuinely nonlinear
  component: 1280 Hz energy on an A0 whose partial series has nothing within 17 Hz, 62× the
  transverse content there, growing as v^2.35 and effectively absent when the key is whispered.
  Two honest limitations. (1) **The pair-selection rule is not modelled** — the exact projection
  of `(∂y/∂x)²` onto longitudinal mode `m` picks particular transverse pairs by orthogonality,
  which is `O(N²)` at 64 partials; squaring the summed slope keeps every product term but gives
  them all equal weight. The phantom *frequencies* are right; their relative strengths are
  approximate. (2) **`κ` is calibrated, not derived** — its SI value is `EA/2L`, but every term
  is in §6/§11.1's normalised system, so it was set against the level a real bass note shows
  (−19.8 dB at A0, full velocity) rather than computed.
  Perf is now the thing to watch: **2.0× margin** is the tightest of the project, and the
  longitudinal bank runs `StringResonator` objects through virtual dispatch — the same cost M2
  removed from the string partials by flattening. If M8/M9 need headroom, flattening this bank is
  the obvious ~10 % back.
- **M6** — fully verified; nothing marked `[!]`, but two limitations are worth carrying forward
  rather than burying. (1) **The treble felt is stability-capped**, so above ~C5 it is softer
  than a real piano's and the top end is correspondingly duller; the fix (oversampling the
  contact loop) is known and demonstrated to converge, and was out of scope here. (2) **The bass
  is the quietest register** — 300 ms RMS 15.6 dB below the mid — which is more than a real
  instrument. Part of that is genuine and part is structural: `REQ-piano-5` says the bridge is
  "the last stage before the voice's audible output", but `PianoVoice::generate()` still adds the
  raw string sum to the output directly, so strings radiate *without* the soundboard's frequency
  shaping. Routing the voice through the bridge is a milestone-sized change and a real
  requirement gap; M7's longitudinal modes will independently add bass content.
  **M6 was the last structural gap the plan identified**, so from here the model should be judged
  on its timbre. M7–M9 add specific colours (bass growl, attack glide, tier-3 detail) rather than
  missing mechanisms — this is the point where listening to it is worth more than another
  measurement.
- **M5** — fully verified; nothing marked `[!]`. The treble is no longer radiating naked: the
  soundboard now colours 700 Hz – 5 kHz by ~10 dB where it was previously flat. **M6 is the last
  structural gap** — one hammer and one strike position still serve all 88 notes, so notes remain
  transpositions of each other in character. After M6 the model should be judged on its timbre
  rather than on missing mechanisms.
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
