# Piano Physics Upgrade — Plan & Spec

The design document for turning `src/physical/` from a **struck-string model** into a
**piano model**. Companion files: the progress ledger
[`piano-physics-progress.md`](piano-physics-progress.md) (what's done / what's next) and the
`arstro.piano.implement` skill (the procedure).

> **How each milestone gets built is NOT specified here** — that is
> `arstro.dsp.implement`'s job (math derived before code, `## Math` in the module README,
> unit + integration tests, requirement traceability, commit). This document specifies
> **what** to build, **in what order**, and **how we prove it worked**.

---

## Why the current model sounds like a struck violin string

Diagnosed 2026-07-25 against the shipped code. Four findings, in order of severity:

| # | Finding | Code | Consequence |
|---|---|---|---|
| 1 | **The hammer never touches the string.** `HammerExciter::process(Sample /*in*/, …)` discards its input — the hammer is a free mass bouncing off an infinitely rigid wall, and its force is sprayed at a passive resonator bank. | `HammerExciter.cpp` | Open-loop "impulse → resonators" *is* the textbook plucked/struck-string model. Every piano-specific attack behaviour is absent. |
| 2 | **12 partials, fixed.** | `kPartialCount = 12` | C4 tops out at ~3.1 kHz; real piano attack energy runs to 8–10 kHz. Dull, near-pure tone. |
| 3 | **Every note decays in 3 s.** `PianoVoice` never sets a per-note decay. | `initProperty(baseDecayID, 3.0)` | Real: A0 ≈ 40 s, C4 ≈ 10 s, C7 ≈ 1 s. A uniform 3 s reads as "music box" in the treble and "cut off" in the bass. |
| 4 | **Near-uniform decay across partials.** `T60_n = T60_1/n^0.9` — partial 12 decays only ~9× faster than the fundamental. | `dampingExponentID = 0.9` | Almost no **spectral evolution**. Piano tone is defined by a bright complex attack that mellows to near-sine in ~1 s. A static spectrum reads as bowed/plucked. |

Secondary: strike position fixed at 1/8 for all 88 notes; one hammer mass/stiffness for the
whole keyboard; bridge modes span only 80–700 Hz so everything above 700 Hz radiates
completely uncoloured; `registerGain(f0)^0.6` and `kHammerToStringGain` are empirical
band-aids compensating for #2 (they should be *deleted*, not tuned).

> **Updated after M1:** `registerGain` turned out to be compensating a *bug* — the resonator's
> sustained-drive gain normalisation made loudness track decay time — and was deleted at M1
> once that was fixed. `kHammerToStringGain` remains, and is M3's to delete.

---

## Milestone order (and why)

```
M0  perf baseline        ← cheap; M2/M4 multiply CPU, so we need a gate before them
M1  loss model           ← biggest audible win per line of code (spectral evolution)
M2  partial count        ← restores the missing 3–10 kHz
M3  hammer↔string loop   ← the real architectural fix; deletes the gain hacks
M4  two polarizations    ← double decay ("bloom")
M5  soundboard           ← colour above 700 Hz
M6  per-register voicing ← notes stop sounding like transpositions of each other
M7  longitudinal modes   ← bass growl/clang        [amends REQ-piano-15]
M8  tension modulation   ← attack pitch glide
M9  Tier-3 detail        ← dampers/duplex/felt/una corda  [amends REQ-piano-16]
```

M1+M2 are deliberately first: they are small, local to `StringPartialBank`, and together
should stop it sounding like a static plucked string. M3 is the deep fix but is easier to
land once the bank it must couple to is already correct.

---

## M0 — Performance baseline & budget

**Goal.** A repeatable CPU measurement, before M2/M4 multiply the resonator count.

**Build.** A `tools/` or `tests/` benchmark that renders N seconds of an 8-voice chord
through `PianoEngine` offline and reports **× real-time** (e.g. "renders 10 s of audio in
0.8 s → 12.5× RT").

**Budget (the gate every later milestone must still pass).** ≥ **4× real-time** for 8
simultaneous voices at 48 kHz on this dev machine. Below that, the interactive
`examples/piano` app is at risk of ALSA underruns.

**Acceptance.** Benchmark runs, prints a stable number, and is recorded in the ledger as the
pre-upgrade baseline.

---

## M1 — Frequency-dependent loss model (spectral evolution)

**The single most recognisable feature of piano tone is that the spectrum *changes*:**
bright and complex at the attack, near-sinusoidal a second later. The current power law
barely does this.

**Physics.** Amplitude decay rate α (nepers/s) combines a roughly frequency-independent
term (air/viscous, bridge) and a viscoelastic/internal term that scales with frequency
squared. Standard piano-synthesis parameterisation (Bensa et al. 2003; Bank):

```
α(f)   = c1 + c3·(2πf)²                [Np/s]
T60(f) = ln(1000)/α(f) = 6.9078/α(f)   [s]
α_n    = c1 + c3·(2πf_n)²              per partial, f_n from README §2
```

**Parameterisation.** Rather than exposing raw `c1`/`c3`, solve for them from two decay
times the user can reason about — the fundamental's T60 and a high-frequency reference:

```
α_lo = 6.9078 / T60_fundamental        at f = f_1
α_hi = 6.9078 / T60_ref                at f = f_ref (5000 Hz)
c3   = (α_hi − α_lo) / ((2πf_ref)² − (2πf_1)²)      clamp ≥ 0
c1   = α_lo − c3·(2πf_1)²                            clamp > 0
```

**Per-note fundamental decay.** `PianoVoice::setFrequency()` must derive a default T60 from
pitch instead of leaving 3.0 s everywhere. Target anchors (undamped, sustain held):

| Note | f0 (Hz) | T60 target |
|---|---|---|
| A0 | 27.5 | ~40 s |
| C2 | 65.4 | ~25 s |
| C4 | 261.6 | ~10 s |
| C6 | 1046 | ~2.5 s |
| C8 | 4186 | ~0.4 s |

Fit a monotonic curve through these (a power law in f0 plus clamps is sufficient — derive
it in the README, state it as empirical/fitted, not derived from string dimensions).

**Params.** `baseDecayID` keeps its meaning (T60 of the fundamental). **Replace**
`dampingExponentID` with `brightnessDecayID` (T60 at 5 kHz, default ~0.08 s). Removing a
public setter is an API change — update README, `docs/requirements.md`, and every test/caller.

**Sanity check of the target behaviour.** C4 with T60₁ = 10 s and T60₅ₖ = 0.08 s gives
c3 ≈ 8.7e-8; partial 20 (5232 Hz) then has T60 ≈ 0.073 s — it dies in under 100 ms while the
fundamental rings for 10 s. That ratio is the effect we are buying.

**Acceptance.**
1. `T60(partial 20) < T60(partial 1) / 50` at C4.
   > **Adapted at M1:** partial 20 does not exist until M2 raises `kPartialCount` above 12.
   > M1 therefore asserts the same tilt on the highest partial that *does* exist —
   > `T60(partial 1)/T60(partial 12) ≥ 25`, measured **35.3**. Projecting M1's solved
   > coefficients to partial 20 gives **100.7**, so this criterion's original form is expected
   > to pass unchanged once M2 lands; re-assert it there.
2. **Spectral evolution:** high-band (>2 kHz) to low-band energy ratio drops by ≥ 20 dB
   between the attack window and t = 1 s.
   > **Adapted at M1 to ≥ 15 dB (measured 17.1).** The 20 dB target was set before M1
   > discovered that §1's gain normalisation was inflating short-T60 (high) partials at the
   > attack; removing that artifact removes the inflated headroom too. The remaining ceiling
   > is the *excitation* spectrum, not the decay law: the open-loop ~2.6 ms hammer pulse
   > leaves high partials ~42 dB down at the attack. **M3** (hammer↔string coupling, whose
   > reflection ripple re-injects high-frequency energy) is what should raise this — restore
   > the 20 dB target there.
3. `T60_fundamental(A0) / T60_fundamental(C7) ≥ 10`.
4. Existing `piano_damper_decay` and `piano_inharmonicity` checks still pass.

---

## M2 — Pitch-dependent partial count (bandwidth)

**Physics.** A string has as many transverse modes as fit below Nyquist. Truncating every
note at 12 removes the entire upper spectrum of every note below ~1.6 kHz.

```
include partial n while   f_n < kNyquistFraction · f_s     (kNyquistFraction = 0.45)
N_active = min(kMaxPartials, that count)
```

Note `f_n = n·f0·√(1+Bn²)` grows **faster** than `n·f0`, so the cutoff must be evaluated on
the actual inharmonic `f_n`, not on `n·f0`.

**No audio-thread allocation.** Keep a fixed-capacity `std::array<StringResonator,
kMaxPartials>` (`kMaxPartials = 64`, a perf/realism compromise — document it) and an
`mActiveCount`; `process()` loops only to `mActiveCount`. Never resize on the audio thread.

**Also:** `kExciteNorm = 2.0/kPartialCount` must become `2.0/mActiveCount`.

### ⚠ Budget risk — measured at M0, read this before starting M2

M0 measured the pre-upgrade baseline at **15.42× real-time for 8 voices = 192 resonators**
(8 voices × 2 unison × 12 partials). The 4× gate therefore affords roughly **740 resonators**.

```
kMaxPartials = 64  →  8 × 2 × 64 = 1024 resonators  →  ~2.9× real-time   ✗ BREACHES
fits the gate      →  740 / (8 × 2)                 ≈  46 partials
```

**M2 as originally specced will breach `REQ-piano-17`.** Choose a mitigation and record it in
the ledger's decisions log before implementing:

1. **Flatten the resonator inner loop (recommended — highest leverage).** Every partial
   currently pays a virtual `SignalProcessor::out()` call plus its per-sample smoothing branch
   to perform *five* arithmetic ops. Iterating a flat `{y1, y2, a1, a2, g}` array inline
   inside `StringPartialBank::process()` — no virtual dispatch, no per-resonator property
   machinery — should pay for M2 *and* M4 outright. Costs: `StringResonator` stops being the
   per-partial unit (it stays the soundboard's mode type), so the README §1 code
   cross-reference must be updated.
2. **Cap `kMaxPartials` at ~44–46.** Trivial, keeps the architecture, but leaves the top of
   the spectrum truncated on bass notes — a partial retreat from M2's whole purpose.
3. **Skip idle voices.** `PianoEngine::renderBlockBytes()` loops all 8 voices with no
   `isFinished()` check, which is why M0 measured 1 voice (17.2×) as barely cheaper than 8
   (15.4×). Worth doing regardless — but it does **not** help the gate, which is defined at 8
   voices sounding, i.e. the case where nothing is skippable.

**Acceptance.**
1. Active count matches the formula at C2 / C4 / C8 (C8 ≈ 5 partials, C4 at the cap).
2. **Every** `f_n < f_s/2` — no partial above Nyquist, at every note across the keyboard.
3. C4 has measurable energy above 3 kHz (Goertzel at partial 15+) where it previously had
   exactly none.
4. M0 benchmark still ≥ 4× real-time — see the budget risk above; this is the criterion most
   likely to fail, so measure it early rather than at the end.

---

## M3 — Coupled hammer–string interaction

**The defining piano interaction.** Felt compresses against a *yielding* string; the string
pushes back and its motion modulates the contact force while the hammer is still touching.

**Physics.** Contact compression is *relative*:

```
c[n] = x_hammer[n] − y_string(β, n−1)          ← the term that is currently missing
F[n] = K·max(0, c[n])^p        (hysteretic, README §6)
m_h·dv/dt = −F        →  v[n+1] = v[n] − (F[n]/m_h)·dt
                         x[n+1] = x[n] + v[n+1]·dt
string driven by +F[n]         (Newton's third law)
contact ends when c ≤ 0 and v < 0
```

One-sample delay on `y_string` keeps the loop causal and computable (same justification as
the bridge feedback path, README §8).

**What this buys — none of which is reachable today:**
- Contact duration becomes a *result* of string impedance, not of bouncing off a wall.
- **Force-pulse ripple:** the wave reflects off the near termination and returns to the
  strike point *during* contact, re-modulating F. A piano fingerprint.
- **Register divergence:** in the treble, contact time exceeds the string period, so the
  hammer stays engaged across multiple reflections — a fundamentally different spectrum from
  the bass. This is why a piano's registers sound like different instruments yet one
  instrument.

**API.** `HammerExciter::process(Sample in, int channel)` starts *using* `in` as the string
displacement at the strike point. `StringPartialBank` gains
`Sample displacementAtStrike() const` = `Σ gₙ·yₙ[n−1]`, which needs
`StringResonator::lastValue(channel)`.

**Unit consistency — derive this before coding (rule 2).** Closing the loop only works if
`x_hammer` and `y_string` are in the same units. Modal form:

```
q̈_n + 2ζ_nω_n·q̇_n + ω_n²·q_n = (g_n/m_n)·F        m_n = modal mass
```

so the resonator input gain must carry `1/m_n`, not an arbitrary constant. **Deliverable: `kHammerToStringGain` is deleted**, replaced by
physically determined modal-mass scaling. (`registerGain` was already deleted at M1, which
found it was compensating a gain bug; its successor `voicingGain` models real per-register
voicing and belongs to M6 — do not delete that one here.)

**Acceptance.**
1. Contact duration varies with **pitch** at fixed velocity (`contact_ms(C2) ≠ contact_ms(C7)`)
   — impossible before this milestone.
2. Contact duration still decreases monotonically with velocity.
3. Bass note force pulse has ≥ 1 local maximum *after* the primary peak (reflection ripple).
4. Treble note: contact duration > one period of f0.
5. Output stays bounded across the keyboard **with `registerGain` removed**.
6. M0 benchmark still ≥ 4× real-time.

---

## M4 — Two transverse polarisations (double decay)

**Physics.** The hammer strikes vertically, but the string vibrates in two planes: vertical
(strongly coupled to the bridge → fast decay) and horizontal (weakly coupled → slow decay),
split slightly in frequency by bridge anisotropy.

```
f_vert = f_n                    T60_vert = T60_n
f_horiz = f_n·(1 + δ_pol)       T60_horiz = T60_n · R_pol
excitation: vertical (1−ε_pol),  horizontal ε_pol
output = y_vert + y_horiz
δ_pol ≈ 1e-4      R_pol ≈ 3–10      ε_pol ≈ 0.05
```

Result: the **prompt sound → aftersound** envelope — a fast initial fall, then a long quiet
tail, with slow beating. Piano notes "bloom". A single exponential reads as *plucked string*.

> The README currently credits unison detune + bridge coupling for double decay. Two strings
> at 0.6 cents does not deliver it; correct the README when this lands.

**Cost control.** 2× resonators is expensive. Apply polarisation only to the first
`kPolarizedPartials` (~16) partials — double decay is perceptually a low-partial phenomenon.
Document that as a deliberate approximation.

**Acceptance.**
1. Fit two exponentials to the RMS envelope; the late slope is ≥ 3× slower than the early one.
   > **Adapted at M4 — asserted for bass/mid (C3, C4), plus a trend test.** Only the bridge-loss
   > term `c1` splits between the planes (README §5b), so the effect's strength tracks how
   > bridge-dominated a note's fundamental is: C3 89 % → ratio 5.4, C4 77 % → 4.3, C5 50 % →
   > 2.0. That fall with pitch is the physics *and* matches real pianos, where aftersound is a
   > bass/mid phenomenon — demanding a flat ≥3× across the whole keyboard would demand the
   > model be wrong. The treble is covered by a separate test asserting the ratio *decreases*
   > monotonically with pitch and stays > 1.2.
2. Equivalently: decay rate over [0, 0.5 s] vs [1.5 s, 3 s] differs by ≥ 3×.
   > **Replaced at M4 by note-relative windows.** The crossover time scales with T60
   > (C4 1.2 s, C3 2.3 s, A0 9.3 s), so fixed windows only straddle it in the mid register —
   > measured 5.5× at C4 but 2.6× at C3, purely from window placement. Windows are now placed
   > at fractions of each note's own predicted crossover, and the slope is a least-squares fit
   > over 12 sub-windows so residual beating cannot masquerade as decay.
3. M0 benchmark still ≥ 4× real-time.
4. **Must not regress M1's spectral evolution.** (Added at M4: the first implementation applied
   one flat `R_pol` to the whole of `alpha_n`, shortening every partial's prompt decay including
   the high ones, and the spectral-evolution figure collapsed 20.5 → 11.2 dB. The `c1`-only
   split fixes it; the threshold settles at 18 dB because double decay legitimately speeds the
   *fundamental's* prompt phase, which this particular ratio metric divides by.)

> ⚠ **Stacks on top of M2's budget risk.** M4 adds `kPolarizedPartials` (~16) extra resonators
> per string on top of whatever M2 settled on. Off M0's measured 192-resonator baseline, M2
> at 46 partials plus M4's 16 gives 8 × 2 × 62 ≈ 992 resonators ≈ 3.0× real-time — a breach
> *unless* M2 took mitigation (1), the flattened inner loop. Treat that as the default plan:
> if M2 shipped with the cap-only mitigation, expect to do the flattening work here instead.

---

## M5 — Soundboard / bridge

**Problem.** 8 modes spanning 80–700 Hz means everything above 700 Hz radiates completely
uncoloured — naked resonators, i.e. music box. Also string decay is an independent parameter
rather than emerging from bridge admittance.

**Recommended approach (no requirement conflict):** extend the modal set to ~30–40 modes
covering 50 Hz – 5 kHz. Plate modal density is roughly constant per Hz, so space them
accordingly with randomised-but-fixed detuning; give the set a realistic radiation rolloff.

> **Rejected option — flag before choosing it:** commuted synthesis / convolving a measured
> soundboard impulse response would be cheaper and very effective, but **conflicts with
> `REQ-piano-14`** (measured IR / room acoustics explicitly out of scope). Taking that route
> requires amending REQ-piano-14 first, per `arstro.dsp.implement` rule 5 — do not do it
> silently.

**Stretch (optional, larger):** make string decay partially *emerge* from bridge admittance
(reflection coefficient at the termination sets both decay and radiation), rather than being
set independently by M1's T60. Only attempt after M1–M4 are stable.

**Acceptance.** Impulse the bridge and measure its transfer function: response above 700 Hz
shows ≥ 6 dB of variation across frequency (i.e. is coloured), where it is currently flat.

---

## M6 — Per-register voicing

Real pianos are graded across the keyboard; ours is uniform, which is a major reason all
notes share one character.

| Property | Bass | Treble | Currently |
|---|---|---|---|
| Hammer mass | ~11–12 g | ~4 g | one value |
| Felt stiffness | softer | harder | one value |
| Strike position β | ~1/8 | ~1/15 (top octave strikes very close to the end) | 0.125 everywhere |
| Strings per note | 1 (wound bass) → 2 | 3 | 2 everywhere |

All four become functions of f0. With M2 landed, the β comb pattern is finally audible and
note-dependent (before, 12 partials meant β only nulled partial 8).

**Acceptance.**
1. Spectral centroid vs. pitch follows the expected monotonic trend.
2. Unison count matches the table at the register boundary notes.
3. Contact duration differs across registers beyond what M3 alone produces (mass/stiffness
   grading contributes).

> **Scope added at implementation (2026-07-26): the string's modal mass is graded too.** The
> table above lists four hammer/geometry properties and no string property, but README §6 records
> that the contact is governed by the *ratio* `m_h/m`, and M3 left `m = 1` at every pitch.
> Grading `m_h` alone inverts that ratio relative to a real instrument, so implementing only the
> table would have made the model less correct than leaving it uniform. See the progress ledger's
> decisions log.
>
> **Criterion 1 sharpened.** "Monotonic centroid" is necessary but far too weak — transposing one
> note across the keyboard also produces a monotonically rising centroid, which is precisely the
> defect M6 exists to remove. Asserted instead on `centroid/f0`, which a keyboard of pure
> transpositions holds *constant* by construction: measured to collapse **76×** from A0 to C8.
>
> **A criterion was added, not adapted.** Grading the modal mass turned the coupled contact loop
> numerically unstable in the top octave (201× energy gain at C8), so M6 also carries an
> energy-conservation bound over all 88 keys × 4 velocities. It found the bug; it stays in the
> suite as the contact loop's validity guard.

---

## M7 — Longitudinal modes & phantom partials

⚠️ **Amends `REQ-piano-15`**, which currently declares this out of scope. Per
`arstro.dsp.implement` rule 5, update the requirement *first*, with the reason (user
feedback: bass notes read as bass-guitar-like without it), then implement.

**Physics.** Longitudinal string modes sit far above the transverse series:

```
f_long,m ≈ m·(1/2L)·√(E/ρ)          typically 10–20× f0
```

They are driven by **tension modulation**, which depends on the *square* of transverse
displacement, so they generate **phantom partials** at `2·f_i` and `f_i ± f_j` — frequencies
where no transverse partial exists.

**Implementation.** A small bank of longitudinal resonators driven by the squared transverse
signal, with gain strong in the bass and negligible in the treble. This is the metallic
**growl/clang of low piano notes** — the single most missing bass characteristic.

**Acceptance.** For a hard bass strike, measurable energy at a frequency where `f_n` predicts
**no** transverse partial (a genuine phantom), rising with strike velocity and absent at
near-zero velocity.

---

## M8 — Tension modulation (attack pitch glide)

**Physics.** A hard blow raises average string tension, so the note starts sharp and glides
down as it decays:

```
f0(t) = f0·(1 + κ·E_transverse(t))
```

**Acceptance.** Hard bass blow: measured f0 in the first 50 ms is ≥ 2 cents sharp relative to
f0 at t = 1 s. Soft blow: < 0.5 cents. (Measure via Goertzel/zero-crossing on windowed
segments.)

---

## M9 — Tier-3 detail

Small, independent, ship in any order:

1. **No dampers in the top octaves** — notes above ~MIDI 88 have no damper and always ring.
2. **Duplex / aliquot scale** — short sympathetic segments beyond the bridge; treble shimmer.
3. **Stulov felt hysteresis** — real felt is rate/history dependent (relaxation kernel),
   not merely load/unload stiffness asymmetry. Replaces README §6's simplification.
4. **Una corda done properly** — strike a *subset* of the unison strings so the un-struck
   string is driven only through the bridge.
   ⚠️ **Amends `REQ-piano-16`**, which currently specifies the gain-reduction approximation.

---

## Cross-cutting rules

- **Every milestone** follows `arstro.dsp.implement`: math derived and written into
  `src/physical/README.md`'s `## Math` **before** coding; unit tests (100% line coverage on
  `physical/`); a numeric integration assertion; requirement traceability; commit.
- **Every milestone** re-runs the M0 benchmark and records the number in the ledger. A
  milestone that breaches the 4× real-time budget is not done — either optimise or reduce
  its scope (and say so in the ledger's decisions log).
- **Never mark a milestone done on "it compiles" or "it sounds better to me"** — this project
  is being built headless; the acceptance criteria above are all numerically measurable, and
  that is deliberate. If something genuinely cannot be verified here (needs a listener),
  mark it `[!]` and say exactly what is unverified.
- **Requirement conflicts (M5-commuted, M7, M9.4) must be resolved by amending
  `docs/requirements.md` first**, never by silently implementing against a stale requirement.
