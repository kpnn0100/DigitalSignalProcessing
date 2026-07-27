# physical/ — Physically-modeled piano voice

A struck-string voice built from first-principles DSP (modal resonators + a nonlinear hammer
contact ODE), not sample playback. Implements `REQ-piano-1..16` (`docs/requirements.md`).

> Status: DESIGN → being implemented against this doc (`arstro.dsp.implement` rule 2/3: this
> file is written *before* the code; if the code and this file ever disagree, this file is
> the bug unless `docs/requirements.md` is updated to say otherwise).

## Why new classes (rule 1 — reuse check)

`src/equalizer/` has only one-pole RC filters (`LowPassFilter`/`HighPassFilter`, scalar,
non-resonant). Nothing in this repo implements a resonant/biquad/modal filter, a nonlinear
contact model, or a multi-processor shared coupling bus — grep for
`waveguide|modal|karplus|resonat|piano|hammer` across `src/`, `docs/` returns nothing. This is
genuinely new core math, not a composition of existing effects (see `docs/requirements.md`
seed note `REQ-effects-1`). What *is* reused:

- `SignalGenerator` — `PianoVoice` inherits it directly (envelope + note-on/off + property
  contract), per `REQ-piano-1`.
- `equalizer::LowPassFilter` — reused as-is to shape the secondary-noise bursts (§9), instead
  of writing a new filter for that purpose.
- The existing `SmoothedParameter`/`ParameterSet` property system — every tunable (frequency,
  inharmonicity, hammer mass, etc.) is a normal `PropertyIndex` property, not a bespoke field.
- `ADSREnvelope`'s **sample-count ramp pattern** (ms → samples, per-channel stage counter) is
  followed (not reused by inheritance — it's a different physical quantity) for the damper
  ramp in §7, because the generic property-smoothing ramp in `SignalProcessor::out()` runs
  over one **control block** (`AudioConfig::bufferSize()` samples) and is not independently
  configurable in milliseconds — the wrong tool for a musically-meaningful ~20 ms damper
  engagement time. `PianoVoice::StringPartialBank` therefore rolls its own tiny linear ramp,
  the same shape `ADSREnvelope` already uses for attack/decay/release.
- `Voice::midiToHz` and the `noteOn(velocity)` 0..1 convention (`src/synth/Voice.cpp`) are
  followed exactly for pitch/velocity units, so a `PianoVoice` drops into the same calling
  convention as `Oscillator`.

## Class map

| Class | Base | Owns / responsibility |
|---|---|---|
| `StringResonator` | `SignalProcessor` | One damped sinusoidal mode (§1). The one reusable primitive — also used, unmodified, for the soundboard's body modes (§8). |
| `StringPartialBank` | `SignalProcessor` | N `StringResonator`s = one physical string: inharmonic partial frequencies (§2), frequency-dependent + damper damping (§3), strike-position mode-shape excitation (§4). |
| `HammerExciter` | `SignalProcessor` | Nonlinear hysteretic hammer-felt contact ODE (§6). |
| `PianoBridge` | `SignalProcessor` | Shared per-note-set soundboard modal bank + sympathetic-resonance feedback bus (§8). One instance shared by every `PianoVoice` in a `PianoEngine`. |
| `LongitudinalBank` | `SignalProcessor` | The string's *longitudinal* modes and the quasi-static tension response (§12), driven by the square of the transverse displacement. Bass-weighted; the source of phantom partials. |
| `PianoVoice` | `SignalGenerator` | Owns U unison `StringPartialBank`s (§5) + one `HammerExciter` + damper state (§7) + pedal state (§9 pedals) + secondary-noise bursts (§9 noise); ties §1–§9 together; `generate(channel)` is the physical step. |

---

## Math

Units: force in abstract normalized units (this is a *signal-level* model, not
SI-calibrated to real newtons — every "N" below is normalized so `StringResonator` output
stays in the same ±1-ish range as every other `SignalGenerator` in this codebase); time in
seconds unless stated; `f_s` = `AudioConfig::sampleRate()`, `dt = 1/f_s`.

**One empirical calibration constant** (measured against the actual implementation, not
derived — documented per rule 2 rather than left as unexplained magic number):

```
force_into_strings = HammerExciter.out() / U                  (U = unison count, §11.5)
audio_out          = Σ partial velocities · kVelocityToSignal
kVelocityToSignal  = 0.0116
```

**`kHammerToStringGain` is gone (M3).** The force→displacement path is fixed by the modal mass
(§6, §11.1), so no arbitrary scalar bridges "hammer force units" and "signal units".

**`voicingGain` is gone (M6).** It was a `(f0/261.6)^0.8` curve introduced at M1 as an
explicitly temporary stand-in for per-register voicing, flattening the fact that
impulse-normalised partials (§1) make a struck mode's amplitude scale as roughly `1/ω` — so at
equal hammer velocity the bass came out ~78× louder than the top octave. That spread was never
an implementation artifact: it is what a piano *would* do if every string had the same mass. The
instrument's own answer is per-register scaling, and §11 now implements it, so the curve has
nothing left to compensate.

> **Neither was it the `registerGain` curve M1 deleted.** *That* one compensated a genuine
> *bug* — §1's sustained-drive gain normalisation made loudness track decay time, invisible
> while every note decayed in 3 s and a 95× imbalance (with clipping at 880 Hz) the moment M1
> gave each note its own T60. Three different scalars, three different reasons, all now gone.

What remains is `kVelocityToSignal`: the bank outputs modal *velocity* in the normalised unit
system, and turning that into a line-level signal is a radiation/transduction constant, not a
fudge. It sets the instrument's overall loudness and nothing else. The measured consequence is
that the keyboard is flat in **loudness** rather than in peak — 300 ms RMS spans 6× with its
maximum in the mid register, falling toward both ends, which is a real piano's contour at
constant key velocity, while *peak* spans 15.7× because the treble's 5 partials align into a
high crest factor where the bass's 64 do not.

### 1. String / body mode — `StringResonator`

Continuous model: a lightly-damped harmonic oscillator (one vibration mode of a string, or of
a soundboard panel — the same math either way, which is why §8 reuses this class),

```
x'' + 2 ζ ω0 x' + ω0² x = f(t) / m
```

For piano-string / soundboard-panel modes, `ζ ≪ 1` (very lightly damped: strings ring for
seconds), so the damped natural frequency `ωd ≈ ω0`. Its impulse response is a decaying
sinusoid `e^(-ζ ω0 t) sin(ωd t)` — i.e. exactly the shape a struck string partial has.

Discretization: standard **matched-Z two-pole resonator** (poles at `r e^{±jθ}`, matched to
the continuous decay/frequency rather than derived via bilinear transform, so `f_n`/`T60_n`
land exactly where specified with no warping):

```
y[n] = 2 r cosθ · y[n-1]  −  r² · y[n-2]  +  G · x[n]

θ = 2π f_n / f_s                              (pole angle -> resonant frequency)
r = 10^(−3 / (T60_n · f_s))                   (pole radius -> decay time)

G = (1 − r²) · sinθ    (sustained-drive normalisation — the DEFAULT)
G = sinθ               (impulse normalisation — setImpulseNormalized(true))
```

`r`'s formula: −60 dB decay in `T60_n` seconds means `r^N = 10^(−60/20) = 10^-3` at
`N = T60_n · f_s` samples → `r = 10^(−3 / (T60_n f_s))`.

**The two `G` normalisations, and why the choice matters.** This resonator's impulse response
is `G·rⁿ·sin((n+1)θ)/sinθ`, peaking at ≈`G/sinθ`; its *sustained* response at resonance peaks
at ≈`G/(1−r²)`. So a single gain cannot normalise both:

- `G = (1−r²)sinθ` gives unit-ish **sustained** peak — correct for `PianoBridge`'s soundboard
  modes (§8), which are driven continuously by the string bus. This is the default.
- `G = sinθ` gives unit-ish **impulse** peak, *independent of decay time* — correct for struck
  string partials (§3), because how hard a mode is set moving is determined by the hammer force
  and mode shape, **not** by how slowly it later decays.

Using the sustained form for struck strings makes amplitude scale as `1/T60`. That was
invisible while every note decayed in 3 s, and became a 95× loudness imbalance across the
keyboard (bass inaudible, 880 Hz clipping at 2.79) the moment M1 gave each note its own
pitch-derived T60 — the physical error only became *visible* once the surrounding physics got
more correct. `StringPartialBank` therefore sets `setImpulseNormalized(true)` on every partial.

Per-channel state: `y[n-1]`, `y[n-2]` (two `Sample` history values), sized/reset in
`onChannelCountChanged()` (same convention as `Oscillator::mPhase`). `θ`/`r`/`G` are shared
(computed in `update()` from the `frequencyID`/`decayID` properties — standard property
contract, `REQ-base-2`). Output is denormal-flushed (`arstroFlush`) every sample — a lightly
damped IIR run for seconds will hit denormals as it decays toward silence, same reasoning as
`Delay`/`Compressor`'s existing `arstroFlush` calls.

**Property → symbol:** `frequencyID` (Hz) → `f_n`; `decayID` (seconds, T60) → `T60_n`.

### 2. Partial frequencies — inharmonicity (stiff string)

A real string isn't massless/perfectly flexible; bending stiffness raises each partial above
the ideal harmonic:

```
f_n = n · f0 · sqrt(1 + B n²)          n = 1 .. N_active
```

**How many partials (M2).** A string has as many transverse modes as fit below Nyquist, and
that count varies enormously with pitch. Truncating every note at a fixed 12 removed the whole
upper spectrum of every note below ~1.6 kHz — a dull, near-pure tone, and one of the reasons
the model read as a plucked string.

```
N_active = largest N ≤ kMaxPartials  such that  f_N < kNyquistFraction · f_s
kMaxPartials = 64        kNyquistFraction = 0.45        N_active ≥ 1 always
```

The cutoff is evaluated on the **inharmonic** `f_n`, never on `n·f0`: stiffness stretches the
series (`√(1+Bn²)` reaches 5.9× by n=120 in the bass), so far fewer partials fit under Nyquist
than the harmonic spacing suggests. Measured, at `f_s = 48 kHz`:

| note | B | partials to Nyquist | N_active (cap 64) | bandwidth |
|---|---|---|---|---|
| A0 (27.5) | 0.0028 | 120 | 64 | 6.2 kHz |
| C2 (65.4) | 0.00095 | 100 | 64 | 9.3 kHz |
| C4 (261.6) | 0.00017 | 63 | 63 | 21.3 kHz (full) |
| C6 (1046) | 0.00005 | 20 | 20 | 21.1 kHz (full) |
| C8 (4186) | 0.00005 | 5 | 5 | 20.9 kHz (full) |

So C4 upward is complete to Nyquist and only the bass is cap-limited — and it is cap-limited by
CPU (`REQ-piano-17`), not by the model: see `docs/piano-physics-progress.md` for the measured
cost of each cap. Because `N_active` varies, the mode-superposition normalisation of §4 must
divide by it (`2/N_active`), not by a constant.

**Nothing allocates on the audio thread.** Storage is a fixed `kMaxPartials`-sized array with
an `mActiveCount`; only the loop bound changes with pitch.

**The recurrence is inlined, not one `StringResonator` per partial (M2).** At 64 partials ×
3 unison × 8 voices the per-partial virtual dispatch and property-system overhead dominated the
five arithmetic ops it guarded, and the projected cost breached `REQ-piano-17`.
`StringPartialBank` therefore keeps flat coefficient arrays and runs §1's recurrence directly:

```
per partial i, per sample:   y = a1_i·y1_i − a2_i·y2_i + drive_i·F
                             a1_i = 2·r_i·cosθ_i     a2_i = r_i²
                             drive_i = sinθ_i · g_i · (2/N_active)
```

`cosθ_i`/`sinθ_i`/`drive_i` depend only on `f_n`, so they are computed once per `update()`; the
damper ramp (§7) changes only `r_i`, so its per-sample work is one `exp()` per partial rather
than a full coefficient rebuild. `StringResonator` remains the soundboard's mode type (§8) and
the **reference implementation** — a unit test drives both and asserts they agree to 1e-12, so
the optimisation cannot silently change the math.

`B` (inharmonicity coefficient, `StringPartialBank::setInharmonicity`) is a property, not
hardcoded — but per-note it needs *some* default, and this repo has no per-note string
gauge/tension table (that's out of scope, `REQ-piano-15`'s sibling scope note). Documented
**empirical** default curve (larger `B` for bass, smaller for treble, matching the real
qualitative trend — not derived from an actual string's physical dimensions):

```
B_default(f0) = clamp( 0.00056 · (100 / f0)^1.25,  Bmin = 0.00005,  Bmax = 0.02 )
```

`PianoVoice::setFrequency(hz)` calls this to seed `B` unless the caller has already called
`setInharmonicity()` explicitly.

### 3. Partial damping — frequency-dependent loss + damper coupling

**Piano tone is defined by its spectral *evolution*:** a bright, complex attack that mellows
to near-sinusoidal within about a second. That requires high partials to die *far* faster
than the fundamental — hundreds of times, not the ~9× a `T60_1/n^0.9` power law gave before
M1 (see `docs/piano-physics-plan.md` §M1; a near-static spectrum is what made the model read
as a bowed/plucked string).

**Continuous model.** A stiff, lossy string's amplitude decays as `a(t) = a₀·e^(−αt)`. The
loss rate combines a roughly frequency-independent term (air/viscous drag, bridge losses) and
a viscoelastic/internal term that scales with the *square* of frequency — the standard
piano-string damping parameterisation (Bensa et al. 2003; Bank):

```
α(f) = c₁ + c₃·(2πf)²                [nepers/s]
T60(f) = ln(1000)/α(f) = 6.907755/α(f)      [s]
α_n = c₁ + c₃·(2πf_n)²               per partial, f_n from §2
```

**Solving c₁/c₃ from two decay times.** Raw `c₁`/`c₃` are not values anyone can reason about,
so the parameters are two *decay times* and the coefficients are solved from them — the
fundamental's T60 (`baseDecayID`) and a high-frequency reference T60 at `f_ref = 5 kHz`
(`brightnessDecayID`, default 0.08 s — real ~5 kHz partials die in roughly 50–150 ms):

```
ω₁   = 2π·f₁      (f₁ = the actual first partial, f0·√(1+B) — inharmonicity included)
ω_ref = 2π·f_ref
α_lo = 6.907755 / T60_fundamental
α_hi = 6.907755 / T60_ref

c₃ = (α_hi − α_lo) / (ω_ref² − ω₁²)         clamped ≥ 0
c₁ = α_lo − c₃·ω₁²
```

**Two guards, both physical, both load-bearing:**

- `ω_ref² − ω₁² ≤ 0` (the note's fundamental is at or above the 5 kHz reference — outside a
  real piano's range, but the setter accepts any frequency): fall back to `c₃ = 0, c₁ = α_lo`,
  i.e. uniform damping. No solve is possible with both anchors at one frequency.
- **`c₁ < 0` — the treble case, and the reason a naive two-point solve breaks.** For a top-octave
  note the fundamental *already* decays fast (C8 ≈ 0.56 s), so demanding an additional 7×
  speed-up by 5 kHz forces the frequency-independent term negative — unphysical (it would mean
  low frequencies gaining energy). Physically, such a string's losses are *entirely* dominated
  by the ω² term at every frequency, so clamp `c₁ = 0` and **recompute** `c₃ = α_lo/ω₁²`. This
  keeps `T60_fundamental` exact and the ω² law intact; the only thing given up is hitting the
  5 kHz target exactly in the extreme treble, which is the right trade.

**Per-note fundamental decay.** Real piano decay varies by *two orders of magnitude* across
the keyboard, so a single constant (3.0 s for all 88 notes, pre-M1) is wrong everywhere.
`PianoVoice::defaultBaseDecaySeconds()` supplies a pitch-derived default — an **empirical
least-squares fit in log-log** to measured-piano anchor values, not derived from string
dimensions (this repo has no per-note string gauge/tension table):

```
T60_1(f0) = T60_ref_note · (f_ref_note / f0)^k        clamped to [0.25 s, 60 s]
T60_ref_note = 6.9 s   f_ref_note = 261.6 Hz (C4)   k = 0.906
```

| Note | f0 | anchor | fitted | residual |
|---|---|---|---|---|
| A0 | 27.5 | ~40 s | 53.1 s | +33 % |
| C2 | 65.4 | ~25 s | 24.2 s | −3 % |
| C4 | 261.6 | ~10 s | 6.9 s | −31 % |
| C6 | 1046 | ~2.5 s | 2.0 s | −21 % |
| C8 | 4186 | ~0.4 s | 0.56 s | +40 % |

The anchors are themselves approximate and do not lie on a true power law (they curve in
log-log), so residuals reach ±40 %. Accepted deliberately: decay-time perception is roughly
logarithmic, the *trend* across two orders of magnitude is what matters, and one documented
formula beats an 88-entry table nobody can verify. A per-note table is the refinement path
if it ever matters. `setBaseDecaySeconds()` overrides the default permanently for that voice
(same override latch as `setInharmonicity()`, §2).

**Worked example (C4, the acceptance case).** `f0 = 261.6`, `B = 1.68e-4`, `T60_1 = 6.9 s`,
`T60_5k = 0.08 s` → `α_lo = 1.001`, `α_hi = 86.35`, `c₃ = 8.67e-8`, `c₁ = 0.767`. Partial 12
(`f₁₂ = 3177 Hz`) then gets `α = 35.3 → T60 = 0.196 s`: it is gone in a fifth of a second
while the fundamental rings for seven seconds. *That* ratio is the spectral evolution being
bought.

Damper engagement (§7) scales this down further, per partial, uniformly:

```
T60_n,eff = T60_n / (1 + d · L_damper)     d ∈ [0,1] damper ramp value, L_damper default 40
```

`d=0` (lifted): `T60_n,eff = T60_n` (natural string decay). `d=1` (fully engaged): decay time
collapses by `1+L_damper` ≈ 41×, i.e. a `T60_1 = 6.9 s` C4 fundamental decays in ≈ 170 ms once
the damper is fully down — short, but not a click (`REQ-piano-7`).

### 4. Mode-shape excitation gain — strike position

For a string pinned at both ends, mode `n`'s shape is `sin(nπx/L)`; a force applied at
position `x = βL` (β = fractional strike position, `0 < β < 0.5`) excites mode `n` in
proportion to the mode shape's value there:

```
g_n = | sin(n π β) |                  StringPartialBank::setStrikePosition(β)
force_into_partial_n[t] = F_hammer[t] · g_n
```

This is the real comb-filter mechanism (`REQ-piano-4`): whenever `β ≈ k/n` for integer `k`,
`g_n ≈ 0` and that partial is suppressed — e.g. striking at `1/8` suppresses partials 8, 16, 24…
`β` is **graded by register** (§11.4), from 1/8 in the bass to 1/15 at the top.

> **There is deliberately no `(2/N)` factor.** This section previously carried one, described as
> "the standard mode-superposition normalization for N truncated modes." It was removed at M2 and
> the reasoning is worth keeping: modal superposition has no `1/N` term. `N` is a *truncation
> choice*, and adding a 64th partial must not quieten the first 63. The factor was harmless while
> `N` was a fixed 12 and became a spurious pitch-dependent gain the moment §2 made `N` vary with
> pitch — leaving the bass ~5× quieter than the treble, the opposite of a real piano.

### 5. Unison strings — detuning and beating

Each note has `U` (default 2, settable 1–3) full `StringPartialBank` instances, detuned a few
tenths of a Hz apart:

```
f0_k = f0 · 2^(detuneCents_k / 1200)        k = 0 .. U-1
detuneCents_k spread symmetrically around 0, e.g. U=2 -> {-c/2, +c/2}, U=3 -> {-c, 0, +c}
```

(`c` = `PianoVoice::setUnisonDetuneCents`, default ≈ 0.6 cents — a few tenths of a Hz at piano
pitches, matching real unison tuning spread.) All `U` banks are struck by the same hammer
(§6, which sees their mean displacement) and summed, so their near-identical partials **beat**
at `|f_k − f_j|` Hz. That is what unison detuning delivers, and it is real.

> **Corrected at M4.** This section previously also credited unison detuning + bridge coupling
> for the piano's **double decay**. Measured, it does not produce it: both unison strings couple
> to the bridge the same way, so they decay at essentially the same rate and their sum is still
> one slope. The actual mechanism is the two transverse polarisations of §5b, and
> `REQ-piano-3` has been amended to say so. The requirement's intent — that multi-stage decay
> *emerge* from physics rather than from a scripted envelope — is unchanged and still met.

### 5b. Two transverse polarisations — the double decay (M4)

**Physics.** A real string vibrates in two independent transverse planes:

- **Vertical** — the plane the hammer strikes in, and the plane in which the bridge is most
  compliant, so it is **strongly coupled** to the soundboard. Energy leaves fast: short decay,
  loud. This is the *prompt sound*.
- **Horizontal** — parallel to the soundboard, **weakly coupled** to the bridge. Energy leaves
  slowly: long decay, quiet. This is the *aftersound*.

The hammer drives the vertical plane; a small fraction leaks into the horizontal one via string
and bridge asymmetry. Bridge anisotropy also splits the two planes slightly in frequency.

That split (`d_pol`) is deliberately **small**. Measured, `3e-4` puts the polarisation beat
period *below* the aftersound decay time from C5 upward, so the beat masquerades as a decay
slope and corrupts the very envelope this section exists to produce. At `3e-5` the beat period
is 5–20× longer than the aftersound across the whole keyboard: the two planes stay slightly
incoherent — their real role here — without beating. Audible beating is unison detuning's job
(§5), not polarisation's.

**Only the bridge-loss term splits.** §3's loss law is `alpha_n = c1 + c3·w_n^2`, where `c1`
is the frequency-independent bridge/air loss and `c3·w_n^2` is the wire's internal viscoelastic
loss. The vertical/horizontal difference is a *bridge-coupling* asymmetry, so it belongs to
`c1` alone — the internal loss is a property of the wire and is identical in both planes:

```
vertical:    f_v = f_n              alpha_v = c1·sqrt(R_pol) + c3·w_n^2   drive x (1 - eps_pol)
horizontal:  f_h = f_n·(1 + d_pol)  alpha_h = c1/sqrt(R_pol) + c3·w_n^2   drive x eps_pol
             T60 = ln(1000)/alpha,  horizontal capped at T60_cap
output = y_v + y_h          (the hammer feels ONLY y_v — see below)

R_pol = 8      eps_pol = 0.05      d_pol = 3e-5      T60_cap = 60 s
```

This is what makes **double decay a low-partial phenomenon by physics rather than by fiat**:
low partials are bridge-loss dominated, so their two planes differ strongly (C4's fundamental
splits 2.9 s / 13.7 s); high partials are internal-loss dominated, so their planes barely
differ (partial 12: 0.188 s vs 0.196 s unsplit) and their prompt decay is left alone.

> Applying one flat `R_pol` to the *whole* of `alpha_n` instead — the obvious first
> implementation — shortened every partial's prompt decay by 2.83×, including the high ones,
> and measurably darkened the attack: spectral evolution fell from 20.5 dB to 11.2 dB. The
> `c1`-only split is both more physical and free of that side effect.

**Why the split is geometric around `T60_n`, not `T60_v = T60_n`.** The plan's first form gave
the horizontal plane `R_pol × T60_n`, which multiplies every note's *overall* ring time by up to
8× and would silently undo §3's calibrated pitch→decay curve. Splitting geometrically keeps the
geometric mean at `T60_n`, so §3's curve keeps its meaning, and lands both planes on
real-piano values — C4: prompt 2.4 s, aftersound 19.5 s (measured pianos: ~1–2 s and ~10–20 s).
The horizontal decay is capped at 60 s so a bass note cannot ring for minutes.

**Crossover.** With amplitudes `A_v` and `eps·A_v` decaying at `a_v` and `a_v/R`, the aftersound
takes over at

```
t_cross = ln(1/eps_pol) / (a_v · (1 − 1/R_pol))
```

≈ 1.2 s at C4 — early enough that the two slopes are separately measurable, which is exactly
what the acceptance test does.

**The hammer feels only the vertical plane.** Horizontal motion is perpendicular to the
hammer's compression axis, so it does not change `c = x_h − y_string` (§6) to first order. The
horizontal entries therefore carry a displacement weight of **zero** — they radiate but do not
push back on the felt.

**Two documented simplifications.** (1) Weak bridge coupling is modelled through the *decay*
(`R_pol`) alone; the horizontal plane is not additionally attenuated in radiation, so `eps_pol`
alone sets how quiet the aftersound starts. (2) Polarisation is applied only to the first
`kPolarizedPartials = 16` partials — double decay is perceptually a low-partial phenomenon, and
this bounds the cost against `REQ-piano-17`. Both are approximations, not derivations.

**Storage.** The horizontal twins are appended to the same flat arrays as extra entries
(`[mActiveCount, mTotalCount)`), so §1's recurrence still runs as one loop with no branching —
they are simply more resonators with their own coefficients.

### 6. Hammer–string contact — nonlinear, hysteretic, and COUPLED (`HammerExciter`)

A single-degree-of-freedom nonlinear-spring hammer: the felt is a compression-only nonlinear
spring, asymmetric between loading and unloading so contact dissipates energy — that asymmetry
*is* the hysteresis (a documented simplification of felt viscoelasticity, not the full Stulov
model).

**The string yields (M3).** Before M3 the felt compressed against an infinitely rigid wall —
the hammer never saw the string at all, which made the excitation open-loop and *is* the
textbook plucked-string model. Compression is a **relative** displacement:

```
c[n] = x_h[n] − y_string(β, n−1)          <- the coupling term
F[n] = K·max(0, c[n])^p          if v_h[n] ≥ 0   (loading)
F[n] = K·(1−ε)·max(0, c[n])^p    if v_h[n] < 0   (unloading — softer, dissipative)

v_h[n+1] = v_h[n] − (F[n]/m_h)·dt     (semi-implicit/symplectic Euler)
x_h[n+1] = x_h[n] + v_h[n+1]·dt
string driven by +F[n]                (Newton's third law)
contact ends when c ≤ 0 and v_h < 0    (the hammer rebounds off the string)
```

`y_string` is taken at `n−1` so the loop is causal — the same one-sample-delay argument as the
bridge feedback path (§8); at 48 kHz that is a 21 µs lag inside a 1–5 ms contact.

What the coupling buys, none of it reachable from an open-loop pulse:

- **Contact duration becomes a result**, set by the string's impedance rather than by bouncing
  off a wall — so it varies with *pitch*, not just velocity.
- **Force-pulse ripple:** the wave launched at the strike point reflects off the near
  termination and returns *while the hammer is still touching*, re-modulating F. This needs a
  well-resolved partial series to appear at all, which is why it only became reachable after
  M2 raised the partial count (12 partials smear the returning wave away).
- **Register divergence:** in the treble, contact lasts longer than one string period, so the
  hammer stays engaged across several reflections — a fundamentally different excitation
  spectrum from the bass, from one unchanged model.

#### Unit consistency — why the coupling needs a modal mass

`c = x_h − y_string` is only meaningful if both terms are displacements in the *same* units,
which forces the resonator's input gain to carry a real `1/m`. Starting from the modal form
of a point-driven string (mode shape `φ_n(x) = sin(nπx/L)`, `g_n = φ_n(βL)` from §4):

```
q̈_n + 2ζ_nω_n·q̇_n + ω_n²·q_n = (g_n/m)·F(t)          m = modal mass (= ρL/2, equal for all n)
```

Impulse-invariant discretisation of that second-order system (continuous impulse response
`(g_n/(m·ω_d))·e^(−ζω t)·sin(ω_d t)`, sampled and scaled by `T_s` so the discrete convolution
approximates the continuous integral) gives, for §1's resonator whose impulse response is
`G·rⁿ·sin((n+1)θ)/sinθ`:

```
G_n^disp = sinθ_n · g_n / (m · ω_n · f_s)        -> output is modal DISPLACEMENT q_n
G_n^vel  = ω_n · G_n^disp = sinθ_n · g_n / (m · f_s)   -> output is modal VELOCITY
```

**The bank runs at the velocity gain, and recovers displacement by weighting.** Velocity is
the right thing to output: what radiates is bridge force/velocity, not static displacement —
and it is also why the `1/ω_n` does *not* appear in the audio path (it cancels), which is
exactly the structure the pre-M3 code already had, minus the physical constant. Displacement
at the strike point is then recovered from the same partial outputs for free:

```
q_n        = y_n / ω_n                     (y_n = the partial's velocity-scaled output)
y_string   = Σ_n g_n · q_n = Σ_n (g_n/ω_n) · y_n
```

so `process()` accumulates two sums over the same loop — the audio sum and, with the
precomputed weight `g_n/ω_n`, the strike-point displacement. One extra multiply-add per
partial.

**`kHammerToStringGain` is deleted.** The force→displacement path is now fixed by `m`, so the
arbitrary scalar that used to bridge "hammer force units" and "signal units" has nothing left to
do. `m`, `m_h` and `K` form one consistent *normalised* unit system (this is still a
signal-level model, not SI — see ## Units); what remains free is only the instrument's overall
loudness, which is a legitimate control rather than a fudge factor.

> **`m` is not a constant (M6).** M3 fixed the *structure* — the drive gain must carry a real
> `1/m` — but left `m = 1` at every pitch, which says the top C string and the bottom A string
> have the same inertia. §11.1 grades it by register, and §11.2 explains why `m_h` cannot be
> graded without it. `StringPartialBank::kModalMassAtRef` is now only the C4 anchor.

**Properties (empirical/typical, documented as such — not measured from an instrument):**
`massID` → `m_h`, `stiffnessID` → `K`, `nonlinearExponentID` → `p` (default 2.5; real felt is
commonly cited in the 2–3.5 range), `hysteresisLossID` → `ε` (default 0.2 — 20 % of loading
stiffness lost on rebound). `m_h` and `K` are set per note by §11.2/§11.3 and
calibrated **together with `m`** against the coupled loop, targeting real contact durations of
1–5 ms that shorten with impact velocity; the ratio `m_h/m` matters more than either alone (see
§11.2, where it spans 0.15 → 184 across the keyboard). `kMaxContactMs` (15 ms) is a numerical
safety bound, not a physical target.

### 7. Damper engagement ramp

Own small linear ramp (following `ADSREnvelope`'s sample-count-stage pattern, not the
generic per-property block-smoothing — see rule-1 note above):

```
d[n+1] = clamp(d[n] + step, 0, 1)      step = ±1 / (engageMs · f_s / 1000)
```

`noteOff()` (sustain pedal not held, §9) sets the ramp target to `1` (damper closing) over
`engageMs` (default 20 ms — a felt damper physically takes roughly this long to fully seat).
`noteOn()`/sustain-pedal-press sets target `0` (damper lifted) over the same ramp shape. `d`
feeds §3's `T60_n,eff` formula every sample.

**Voice reuse correctness (found via `apps/piano_demo`/`examples/piano` integration testing,
`REQ-piano-12` addendum):** `StringPartialBank::reset()` (called from `PianoVoice::noteOn()`
before every strike, so a voice-stolen bank starts clean — see §"Why new classes") must
recompute each partial's decay coefficient from the just-cleared `d = 0`, not only zero the
resonator history. `PianoEngine::noteOnMidi()` calls `setFrequency()` (which recomputes §1's
`r`/`G` from *whatever `d` currently is*) **before** `noteOn()` (where `reset()` runs); on a
voice reused from a previous, fully-damped note (`d` was `1`), skipping this left the new
note's resonators with the *previous* note's short-`T60`/large-`G` coefficients (§1: `G`
grows as `T60` shrinks) until the *next* decay-affecting property change — so the hammer
struck a resonator calibrated for ~40× less decay time than the fresh string actually has,
producing a large spurious amplitude spike instead of a normal note. Fixed by having
`reset()` end with the same `recomputeEffectivePartials()` call §3's ramp uses, using the
now-zeroed `d`.

#### 7.1 No dampers in the top octaves (M9.1)

A real piano damps only the lower ~70 keys; the top ~1.5–2 octaves have **no dampers at all**
— those strings are short, high, and decay quickly on their own, and a felt damper there would
be both mechanically awkward and pointless. So a note above the cutoff **always rings**: releasing
the key does nothing, and it stops only when its own decay (§3) runs out or the voice is re-struck.

```
hasDamper(f0) = f0 < f_nodamp ,   f_nodamp = √(f(88)·f(89)) ≈ 1357 Hz
```

The cutoff is the geometric mean of MIDI 88 and 89 (`440·2^((n−69)/12)`), so the boundary sits
*between* two notes rather than on one (floating-point luck deciding a note's damper, the same
trap §11.5's unison breaks avoid): MIDI ≥ 89 (the top 20 keys) is undamped, matching "above
~MIDI 88". `PianoVoice::noteOff()` skips both the §7 damper ramp **and** the §10 damper-release
noise when `hasDamper(f0)` is false — there is no damper to seat, so neither happens.

**Interaction with §13's silent-voice freeze, deliberately left as-is.** An undamped voice never
satisfies `isSilent()` (its `damperValue` stays `0`), so it is never frozen — which is *correct*,
not a leak: §13's whole point is that an undamped string can still be re-excited through the
bridge (`REQ-piano-6`), and a no-damper note is undamped *by design*, so it must stay responsive
for as long as it is allocated. It costs CPU until the voice is stolen, but top-octave notes are
the cheapest in the pool (fewest partials, §2).

### 8. Bridge / soundboard coupling + sympathetic resonance (`PianoBridge`)

All `PianoVoice`s in a `PianoEngine` share **one** `PianoBridge`. Each voice's summed
unison-string output (§5) is treated as the drive force onto the soundboard; the soundboard
is modeled the same way a string is (§1) — a small bank of `StringResonator`s, just with
frequencies/decays typical of a piano soundboard's low-order body modes rather than a
string's harmonic series:

```
busIn[n]      = Σ_voices stringOut_voice[n]                 (this sample's total drive)
response[n]   = Σ_(m=1..M) StringResonator_m.process(busIn[n])     M = kBodyModeCount = 8
radiated[n]   = response[n] · radiationGain
```

**The mode set follows plate physics (M5).** A soundboard is a thin plate: bending waves give
`ω ∝ k²`, so the number of modes below `f` grows **linearly** with `f` and the modal density is
**constant in Hz**. Modes are therefore spaced uniformly in frequency — a harmonic series is
the wrong mental model here — across 50 Hz – 5 kHz:

```
spacing  D   = (f_high − f_low) / M                    M = kBodyModeCount = 64
f_m          = f_low + (m + jitter_m)·D                jitter_m ∈ [−0.35, 0.35], deterministic
T60_body(f)  = clamp( 0.35 · (100/f)^0.9,  0.015 s,  0.5 s )
weight_m     = radiation(f_m) / sqrt(M)
radiation(f) = 1 / sqrt(1 + (f/2500)²)                 gentle HF rolloff
response[n]  = Σ_m weight_m · StringResonator_m(busIn[n])
```

**Why 64 modes and why they must overlap.** A real board has ~0.05–0.1 modes/Hz — 250–500 in
this range — so 64 undersamples the true density ~5×. Left at realistic Q that would ring as a
row of isolated resonances rather than a plate. The fix is the behaviour the real plate already
has: **modal overlap**. `T60_body` falls with frequency, so mode bandwidth grows toward the
spacing:

| f | T60 | bandwidth | overlap | regime |
|---|---|---|---|---|
| 100 Hz | 0.35 s | 6 Hz | 0.08 | isolated modes — as a real board has at low frequency |
| 1 kHz | 0.044 s | 50 Hz | 0.65 | transition |
| 5 kHz | 0.015 s | 147 Hz | 1.9 | smooth continuum |

That is qualitatively how real soundboards behave: individually audible low resonances giving
way to a statistically smooth coloured plateau higher up. Frequencies carry a deterministic
low-discrepancy jitter so the uniform spacing cannot ring as an audible comb.

`PianoBridge` is a **single shared instance**, so 64 modes cost ~5 % of the string resonators
(~1280 across 8 voices) — the mode count is limited by realism bookkeeping, not by CPU.

**`1/sqrt(M)` is load-bearing, not cosmetic.** Modes at different frequencies sum incoherently,
so a bare sum would scale as `sqrt(M)` and changing the mode count would silently rescale the
string→bridge→string loop gain. That is precisely how the coupling gain came to be re-tuned at
M1 *and* M3. Normalising per mode makes the bridge's output level — and therefore the loop
gain — independent of `M`, so the mode count can be changed without re-tuning stability.

Values are a **documented generic set**, not measured from an instrument (`REQ-piano-14` — no
measured IR in scope).

**Sympathetic resonance** (`REQ-piano-6`): the *previous* sample's `response[n-1]`, scaled by
a small `couplingGainID`, is added into every voice's §4 excitation as extra drive, on top of
`F_hammer`:

```
totalDrive_voice[n] = force_into_partial_n[n]  +  response[n-1] · couplingGain
```

Using `n-1` (one sample of delay) rather than solving the mutual system simultaneously is
standard practice for stable causal coupled digital resonant networks — it introduces a
negligible (1/f_s ≈ 20 µs) delay in the coupling path.

**`couplingGain` is stability-bounded, not free.** Impulse-normalised partials (§1) have a
large *sustained* resonance gain (`G/(1−r²)` ≈ 800 for a 6.9 s C4 partial, higher in the bass
where T60 reaches 53 s), so this loop's gain is dominated by string Q. Measured worst case —
8 sustained bass voices on one bridge — diverges at `5e-3` and is stable at `1e-3`; the
default `5e-4` keeps a 10× margin. Lowering it costs nothing in `REQ-piano-6` terms because
the *selectivity ratio* is coupling-independent (both same-pitch and off-pitch response scale
linearly with it) — measured ≈ 35× either way. The principled fix, where bridge admittance
sets string damping *and* radiation so the loop is inherently self-limiting, is `M5`. Because every voice's own
`StringResonator`s are sharply frequency-selective (§1), feeding the *same* broadband
`response` signal into all of them only measurably excites the strings whose partials are
near a frequency actually present in `response` — i.e. sympathetic resonance emerges from
shared-bus feedback into resonant filters, not from a hand-authored per-note-pair coupling
table (`REQ-piano-6`'s explicit requirement).

**Damped strings gate sympathetic feedback too:** `PianoVoice` scales `response[n-1]` by
`(1 − d)` (§7's damper ramp value) before adding it as drive. A real damper mutes the
string's response to *any* driving, not only its own free decay; without the gate a damped
string keeps accepting bridge energy at full strength while supposedly being silenced.

`PianoBridge` runs mono (`REQ-piano-13`): `PianoEngine` adds `radiated[n]` to every output
channel identically. Stereo width, if ever added, belongs here (the way `Reverb` decorrelates
per-channel delay lines for width) — not in per-voice hammer/string physics.

#### 8.1 Duplex / aliquot scale — the treble shimmer (M9.2)

Beyond each bridge pin a real grand leaves a **short, un-struck string segment** free to
vibrate — the front and rear *duplex*. On the treble it is deliberately tuned (the **aliquot
scale**) so its pitch coincides with an upper partial of the speaking string; it is never hit,
but the speaking string drives it **sympathetically** through the shared bridge, and it rings
back a high, sustained shimmer that reinforces those partials. It is a treble effect — the
segments are short and bright, and the bass has no functional duplex.

Modelled as a small per-voice bank of high-`Q` `StringResonator`s (rule 1 — reuse the primitive,
the same pattern `LongitudinalBank` used), tuned to harmonics `n·f0` of the note:

```
f_alq,i      = n_i · f0 ,     n_i ∈ {4, 6, 8}   (upper partials, in the ~2–8 kHz brilliance band)
duplex[n]    = g_dup(f0) · Σ_i w_i · Resonator(f_alq,i, T60_dup)[ stringSum[n] ]
```

Three deliberate choices, each with a reason:

- **Driven by the speaking string's own motion `stringSum`, feed-forward — never by the hammer,
  and not fed back into the strings.** The duplex is excited only by what the speaking string
  puts across the bridge, so `stringSum` (§5, the transverse sum before the §12 longitudinal add)
  is the physically correct drive. It is *not* routed back into the bridge bus or the string
  drive, so it cannot form a feedback loop — it is a resonant *read-out* of the string, not a
  new term in the coupled network. (A documented simplification: a real duplex does load the
  bridge slightly; that back-coupling is below the level of the §8 sympathetic path and out of
  scope here.)
- **Tuned to the *harmonic* `n·f0`, not the string's inharmonic `f_n = n·f0·√(1+Bn²)` (§2).** The
  duplex is a separate short segment whose pitch is set by *its* geometry, not the speaking
  string's stiffness, so it lands a few cents off the string's own partial — exactly as a real
  aliquot does. That small offset is the shimmer: a slow beat between the pure, long-ringing
  duplex resonance and the stiffer, faster-decaying string partial at nearly the same frequency.
- **High `Q` and treble-weighted.** `T60_dup` is long (the segment is undamped), so the duplex
  *sustains past* the bridge-damped speaking partial — which is why it reads as an airy tail, not
  just a louder attack. `g_dup(f0)` rises from ~0 in the bass to 1 in the treble and switches the
  bank **off** below a crossover, so the bass pays nothing (the same off-below-a-crossover economy
  §12.4 uses, with the opposite register sign):

```
g_dup(f0) = clamp( (f0/f_cross)^1.5,  0,  1 ) ,     f_cross ≈ C4 ;  off below ~A3
```

The effect is provable by differencing a duplex-on and duplex-off render (§12.2's lesson): the
remainder carries energy at `n·f0` that **decays more slowly** than the same bin without the
duplex — the sustained shimmer — and it is present in the treble and absent in the bass.

### 9. Pedals

- **Sustain** (`PianoEngine::setSustainPedal(bool)` → broadcasts `setDamperHeld(true/false)`
  to every voice): while held, `noteOff()` does **not** start the §7 damper-close ramp — the
  string keeps ringing (and keeps feeding the bridge) until sustain is released, at which
  point any note whose key is already up starts closing its damper.
- **Sostenuto** (`PianoEngine::setSostenutoPedal(true)`): at the instant it's pressed, the
  engine calls `setDamperHeld(true)` only on voices that are *currently sounding* (key still
  down or ringing); future notes struck while sostenuto is held are unaffected and damp
  normally on their own `noteOff()`.
- **Una corda** (`PianoVoice::setUnaCorda(bool)`, M9.4): the real mechanism (`REQ-piano-16`,
  amended). The soft pedal shifts the hammer sideways so it strikes a **subset** of the note's
  unison strings; the un-struck string is left to ring **only through the bridge**:

```
S = struckUnisonCount = U − 1  (clamped ≥ 1)     una corda: trichord 3→2, bichord 2→1, mono 1→1
                       = U                        normal

force_per_string = HammerExciter.out(mean displacement of the STRUCK strings) / U
drive_k          = force_per_string + feedback    for the S struck strings (k < S)
                 = feedback                        for the U−S un-struck strings (only §8 bridge)
K_effective      = K · kUnaCordaStiffness          kUnaCordaStiffness 0.85 — softer, less-compacted felt
```

**Softer, and by the right amount.** The hammer's energy is normalised over the nominal `U` but
delivered to only `S` strings, so the directly-driven energy is `S/U` of normal — a trichord's ⅔
(≈ −3.5 dB), a bichord's ½ (≈ −6 dB). Measured RMS ratio is **0.66× for a trichord**, matching
`S/U` exactly. The un-struck string then rings *only* through the bridge (`REQ-piano-6`): with no
bridge it is silent, which is what makes the `S/U` drop measurable and proves the hammer never
touched it. A single-strung bass note (§11.5) has no string to drop, so there una corda is only
the softer-felt change.

**`kUnaCordaGain` is gone.** It was a flat `×0.6` excitation cut standing in for "fewer strings";
the level drop is now produced by actually driving fewer strings, the way `voicingGain` (M6) and
`registerGain` (M1) were removed once their real physics arrived. The subset is fixed at note-on,
not switched mid-sustain (`REQ-piano-16`).

> **What this model does *not* reproduce: the mellowing.** A real una corda is softer *and*
> mellower. The softer felt (`kUnaCordaStiffness`) is retained and does shift the contact spectrum
> down (verified: lowering hammer stiffness lowers the centroid), but here that is *outweighed* by
> a side effect of the honest subset-strike — dropping a unison string removes some of the
> low-frequency chorusing between the detuned unisons, which nudges the spectral centroid slightly
> *up*. Net, the model's una corda is clearly softer but marginally brighter, not mellower. The
> real instrument's mellowing comes largely from the un-struck string ringing *strongly* through
> tight bridge/agraffe coupling (a low, detuned halo), which the generic §8 sympathetic gain makes
> negligible here. Modelling that strong inter-unison coupling is out of scope for this tier-3
> item; recorded rather than faked. `REQ-piano-8`'s "softer/mellower" is met by the softer half.

### 10. Secondary mechanical noises

Two short filtered-noise bursts, reusing `equalizer::LowPassFilter` (rule 1 — no new filter
written) rather than a new noise-shaping class:

```
noise(t) = whiteNoise(t) · A · e^(−t/τ)              (t = seconds since trigger)
output   = LowPassFilter(cutoff).out(noise(t))
```

- **Hammer thump** — triggered at `strike()` (§6): `A = A_thump · velocity`, `τ ≈ 3 ms`,
  `cutoff ≈ 4 kHz` (`A_thump` default small, ≈ 0.03 — audible as texture, not as a separate
  tone).
- **Damper noise** — triggered when the §7 ramp starts closing (`d` leaves `0`): fixed small
  `A ≈ 0.015`, `τ ≈ 15 ms`, `cutoff ≈ 1.5 kHz` (duller than the hammer thump — felt-on-string
  contact, not felt-on-felt hammer impact).

`whiteNoise(t)` is a small xorshift32 PRNG (no new module — a private helper, deterministic
per voice instance so unit tests can assert boundedness/reproducibility).

### 11. Per-register scaling — why 88 notes are not 88 transpositions (M6)

Everything above §10 describes **one** note. Applied with the same parameters at every pitch it
produces a keyboard whose notes differ only in frequency — the last structural reason the model
sounded uniform. A real piano is *graded*: the builder changes the string, the hammer and the
striking geometry continuously from A0 to C8. All five gradings below are functions of `f0`
alone, applied by `PianoVoice::setFrequency()` and each individually overridable (the same
override-latch pattern §3's `T60_1` uses).

Reference pitch throughout: `f_ref` = 261.6 Hz (C4).

#### 11.1 String scaling → the modal mass `m(f0)`

§6 introduced `m = ρL/2`, the modal mass, and held it at 1.0 for every note. That is the single
most unphysical constant left in the model: it says the top C string and the bottom A string
have the same inertia, when in reality they differ by three orders of magnitude.

Piano strings are **not** ideally scaled (`L ∝ 1/f0` would need a 12 m bass string); builders
compress the bass length and restore the missing mass by overwinding, so `L` and `μ` each follow
a different, awkward curve. Their *product* does not — the modal mass is very nearly a single
power law across the whole compass:

```
m(f0) = (f_ref / f0)^1.62                       normalised so m(C4) = 1
```

Fitted to representative concert-grand design values (typical scaling, not a measurement of one
instrument). One normalised mass unit = 1.9 g, the C4 modal mass:

| note | `f0` | real `L` | real `μ` | real `m = μL/2` | law → `m` | law in grams | error |
|---|---|---|---|---|---|---|---|
| A0 | 27.5 | 2.0 m | 60 g/m | 60 g | 38.4 | 73 g | +22 % |
| C4 | 261.6 | 0.62 m | 6.2 g/m | 1.9 g | 1.00 | 1.9 g | (anchor) |
| C8 | 4186 | 0.05 m | 0.7 g/m | 0.0175 g | 0.0112 | 0.021 g | +22 % |

A 3400× real span reproduced within ±22 % by one exponent. Only the resonator's **input** gain
carries it — §6's velocity gain becomes

```
G_n^vel = sinθ_n · g_n / (m(f0) · f_s)
```

and displacement recovery (`q_n = y_n/ω_n`) needs no second factor, since `y_n` already carries
the `1/m`. So a heavy bass string yields less under the same hammer force *and* responds less to
the bridge's sympathetic feedback (§8) — both physically right, both free.

#### 11.2 Hammer mass `m_h(f0)` — and why it must be graded *with* `m`

```
m_h(f0) = 3.7 · (f_ref / f0)^0.21               normalised units (3.7 = 7 g / 1.9 g)
```

giving 11.3 g at A0, 7.0 g at C4, 3.9 g at C8 — real hammers run ~11–12 g in the bass to ~4 g in
the top octave. The exponent is small because hammer mass barely halves across a compass over
which string mass falls 3400-fold, and *that mismatch is the point*. §6 states that the contact
is governed by the **ratio** `m_h/m`, not by either mass alone; dividing the two laws gives it in
closed form:

```
m_h(f0)/m(f0) = 3.7 · (f0 / f_ref)^1.41         0.15 at A0 → 3.7 at C4 → 184 at C8
```

Real pianos: ≈ 0.19 at A0 and ≈ 230 at C8. The two ends are physically *opposite* regimes — the
bass hammer is light compared to the string it strikes and bounces off a wall of inertia, while
the treble hammer is ~200× heavier than its string and simply throws it. Grading `m_h` while
holding `m` at 1.0 would have inverted this (bass ratio 5.9, treble ratio 2.1) and made the model
*less* correct than uniform values, which is why 11.1 is not optional scope.

Nothing here scripts the two regimes: §6's coupled ODE was already capable of both, and only ever
saw one because `m` was constant.

#### 11.3 Felt stiffness `K(f0)`

Treble hammers are smaller, more tightly packed and lacquered harder; bass hammers are softer.

```
K_physical(f0) = K_ref · (f0 / f_ref)^0.75              K_ref = 1.0e12  (normalised units)
```

`K_ref` and the exponent are **calibrated, not derived** — measured against the two observables
felt stiffness controls: contact duration (real pianos ~1–5 ms, longest in the bass; measured
here 2.08–5.38 ms across the compass) and §1/M1's spectral-evolution figure, which a softer
felt directly dulls (measured 20.0 dB at this value, 12.1 dB at a 30× softer one).

**But `K_physical` is not what the model uses in the top registers**, and this is the sharpest
limitation in §11. The contact ODE (§6) is integrated explicitly against a **one-sample-delayed**
string displacement. Combine that with §11.1's grading — a top-octave string is ~3400× lighter
than a bass string — and the contact gets so brief that the loop stops resolving it. Measured at
C8 with the pre-M6 felt stiffness: contact 0.38 ms (18 samples) and the string leaving the
collision with **201× more energy than the hammer arrived with**. Downstream that appeared, very
indirectly, as one note 85× louder than its neighbours.

Contact time scales as `(m_red/K)^(1/(p+1))`, so bounding `K/m_red` bounds it from below:

```
m_red(f0) = m_h·m / (m_h + m)                    reduced mass the felt works against
K(f0)     = min( K_physical(f0),  C_stab · m_red(f0) )        C_stab = 3.0e12
```

`C_stab` is measured (the loop crosses into energy creation at ≈ 1.08e13) with a 3.6× margin.
The cap binds from roughly C5 upward, so across the top ~2.5 octaves the felt is **softer than
the real instrument's** — the model's treble is duller than a real piano's, and that is a
sample-rate limitation, not a voicing choice. Oversampling the contact loop is the principled
fix; it converges by 4× (measured: the C8 energy ratio goes 201 → 15.3 → 0.002 at 1×/2×/4×).

The cap is asserted **by its consequence** rather than by its formula — a unit test requires
that no key, at any velocity, leaves the string with more energy than the hammer brought.

#### 11.4 Strike position `β(f0)`

```
β(f0) = clamp( 0.125 · (f_ref / f0)^0.227,  1/15,  1/8 )
```

Real strike ratios sit near 1/8 through the bass and tenor and fall through the top two octaves
to ≈ 1/15. The upper clamp is what holds the bass flat at 1/8, so β is genuinely constant over
the lower half of the keyboard and only moves where real designs move it. Via §4's comb
`g_n = |sin(nπβ)|` this shifts the suppressed partials with pitch — β = 1/8 nulls partials
8, 16, 24…, β = 1/15 nulls 15, 30, 45… — which is only audible at all because M2 gave the bank
enough partials to reach those indices.

#### 11.5 Unison count `U(f0)`

```
U(f0) = 1   for f0 <  56.6 Hz     (A0 … A1     — single wound bass strings)
        2   for f0 <  89.9 Hz     (A#1 … F2    — bichords)
        3   otherwise             (F#2 … C8    — trichords)
```

The thresholds sit **between** the boundary notes (A1 = 55.00, A#1 = 58.27; F2 = 87.31,
F#2 = 92.50) rather than on them — a threshold placed on a note's own frequency decides that
note by floating-point luck.

matching standard stringing-scale break points. The bass losing its unison partner is a
*feature*, not a loss: single-strung notes have no unison beating, and their multi-stage decay
comes from the two polarisations of §5b instead — exactly the split of mechanisms M4 established.
Total drive is unchanged by `U` (§6 divides the hammer force by `U` and the `U` banks sum), so
this changes texture, not level.

#### 11.6 What this retires

`voicingGain` (## Units) is **gone**. It was introduced at M1 as an explicitly labelled stand-in
for this section: a `f^0.8` curve compensating the fact that impulse-normalised partials make the
bass ~78× louder at equal hammer velocity. That loudness spread was never a modelling artifact —
it is what a piano would do if every string had the same mass — and the instrument's own answer
to it is 11.1–11.3, which the model now has. What remains is `kVelocityToSignal`, one overall
loudness constant, recalibrated once here because 11.1 changed the unit system's anchor.

### 12. Longitudinal modes & phantom partials (M7)

Everything above §11 describes the string moving *sideways*. A real string also vibrates
**along its own length**, and those modes are the metallic growl/clang of the bottom octaves —
one of the most recognisable things about a real piano, and structurally unreachable from
§2's transverse series no matter how it is tuned. (`REQ-piano-15` declared this out of scope
until M7; it was amended first, per rule 5.)

#### 12.1 Where the longitudinal modes sit

Longitudinal waves travel at the material's bar speed, which for steel is essentially a
constant and **does not depend on tension**:

```
c_L = √(E/ρ) ≈ 5200 m/s          steel; unchanged by how hard the string is tuned
f_long,m = m · c_L / (2L)        m = 1, 2, 3, …
```

That last point is why they are audible at all: the transverse series is set by tension and
lands on the note, while the longitudinal series is set by geometry alone and lands *wherever
it lands* — inharmonic with respect to everything else, which is what makes it read as
"clang" rather than as pitch.

The model has no `L`, so it is fitted from the same concert-grand scaling data §11.1 used, over
the range where the effect is audible (the bass), and anchored at C4:

```
L(f0)      = 0.62 m · (f_ref/f0)^0.52
f_long,1   = c_L / (2L) = 4194 Hz · (f0/f_ref)^0.52
```

| note | law → `f_long,1` | measured on real strings | `f_long,1 / f0` |
|---|---|---|---|
| A0 | 1300 Hz | ~1.3 kHz | 47× |
| C2 | 2040 Hz | ~1.5–2.5 kHz | 31× |
| C4 | 4194 Hz | — | 16× |

Note the ratio is **not** constant — the plan's "10–20× f0" holds in the mid register but the
bottom octave runs nearer 50×, because bass strings are shortened far below ideal scaling and
overwound to compensate (§11.1). Fitting `L` rather than the ratio is what gets that right.

#### 12.2 The coupling — why the partials are *phantom*

Transverse motion stretches the string, and stretching is a **second-order** effect: a
displacement `y` lengthens the string by `≈ ½(∂y/∂x)²`, so the tension modulation goes as the
*square* of transverse amplitude.

```
ΔT(t) ∝ ∫ (∂y/∂x)² dx                    the exact driving term
```

**Which transverse signal to square is not a free choice — it is the whole milestone.** The
driving term integrates the string's *slope*, so in modal terms it weights mode `n` by `n`:

```
∂y/∂x = Σ_n q_n · (nπ/L)·cos(nπx/L)      slope: mode n enters weighted by n
y_n   = ω_n·q_n ∝ n·q_n                  §6: the bank already outputs modal VELOCITY
```

so the bank's own audio sum `Σ_n y_n` **is** the string slope, up to the mode-shape factor, and
it is already computed every sample. That is the signal this model squares:

```
drive(t) = κ · HP( (Σ_n y_n)² )
```

> **The obvious alternative is measurably wrong.** `displacementAtStrike` (§6) is right there and
> was tried first — but it weights mode `n` by `g_n/ω_n`, i.e. by `1/n`, the *opposite* extreme.
> Its spectrum is dominated by the fundamental, so its square lands almost entirely below 500 Hz
> and leaves the longitudinal modes at 1.3 kHz and above essentially undriven. Measured: **zero
> phantom energy** at a test frequency in the transverse series' widest gap — the effect simply
> did not exist. One power of `ω` is the difference between the mechanism working and not.

Squaring is the entire mechanism. A product of two partials generates sum and difference
frequencies, so `y²` contains energy at

```
2·f_i     and     f_i ± f_j
```

— frequencies at which the transverse series `f_n` (§2) predicts **no partial whatsoever**.
Those are the phantom partials, and they are *emergent*: nothing places a partial there, the
nonlinearity produces them. Two consequences follow for free and are exactly what the
acceptance criteria check:

- they scale as **amplitude²**, so they grow far faster with strike velocity than any real
  partial does, and
- they **vanish** at near-zero velocity, where a linearly-driven partial would not.

**`HP(·)` is not cosmetic.** `y²` is non-negative, so it carries a large DC term — physically
the *static* tension rise of a struck string, which shifts pitch (that is M8's subject) rather
than driving longitudinal vibration. Left in, it would also reach the output as a DC offset,
because a two-pole resonator's DC gain is not zero (measured ≈ 6 at 1.3 kHz). It is removed by
subtracting a low-passed copy — `HP(x) = x − LPF(x)`, reusing `equalizer::LowPassFilter`
(rule 1: no new filter class).

**Documented simplification:** the exact projection of `(∂y/∂x)²` onto longitudinal mode `m`
selects particular transverse pairs `(i, j)` by an orthogonality rule, so a faithful model needs
per-pair coupling — `O(N²)` at `N` = 64 partials, which is not affordable here. Squaring the
summed slope keeps every product term but gives them all the same weight into every longitudinal
mode. It changes how strongly each pair contributes; it does not change *which* frequencies are
generated, which is what makes the partials phantom.

#### 12.3 Response — resonant *and* quasi-static

A bank of high-Q resonators alone would be wrong here. The longitudinal system responds to
tension modulation at **every** frequency, with peaks at `f_long,m`; below the first
longitudinal resonance the response is quasi-static (stiffness-controlled), and that
non-resonant part is what carries the phantom partials at `2·f_i` in the first place — most
combination frequencies do not happen to coincide with a longitudinal mode.

```
out(t) = g_reg(f0) · [ w_direct·drive(t) + Σ_m w_m · Resonator(f_long,m, T60_long,m)[drive(t)] ]
```

so both are present: phantom partials everywhere, *amplified* where a combination frequency
lands on a longitudinal resonance — which is how real phantom partials behave.

`T60_long,m` is short (longitudinal modes are heavily damped — they couple strongly to the
bridge and are gone in a fraction of a second, which is why they are an *attack* colour):

```
T60_long,m = clamp( T60_long,1 · (f_long,1/f_long,m)^0.5 ,  0.05 s,  1.0 s )
```

#### 12.4 Register weighting

```
g_reg(f0) = clamp( (f_cross / f0)^1.5,  0,  1 )        f_cross = 130.8 Hz (C3)
```

Full strength through the bottom two octaves, ~0.35 by C4, ~0.04 by C6 — matching where the
effect is audible on a real instrument. It is also what keeps the cost off the treble: notes
above the crossover run **no longitudinal resonators at all** (see §Parameters), so the
milestone adds ~48 resonators to the benchmark chord rather than one bank per voice.

One bank per **voice**, not per string: the `U` unison strings are within a cent of each other
and the hammer already drives them from their *mean* displacement (§6), so a second per-string
copy would cost 3× to model a difference smaller than the detuning it came from.

#### 12.5 Tension modulation — the attack pitch glide (M8)

Transverse motion does not only pump the longitudinal modes (§12.2); it raises the string's
**average tension**, and tension is what sets pitch. A struck string is therefore momentarily
under higher tension than at rest, so the note starts slightly **sharp** and glides down to its
nominal pitch as the vibration — and with it the tension rise — decays. It is largest at the
attack and in the bass, where the amplitude-to-tension leverage is greatest, and it is one of
the small cues that separates a real piano from a static additive tone.

**M8 and M7 are two halves of one quantity.** §12.2's driving term is the slope-square integral
`⟨(∂y/∂x)²⟩ = ⟨slope²⟩`, and §12.2 keeps only its *AC* part (the longitudinal drive), removing
the *DC* part with `HP(x)=x−LPF(x)`. That removed DC part **is** the static tension rise:

```
ΔT(t) ∝ ⟨slope²⟩ ,     slope = Σ_n y_n   (the bank's velocity sum, §12.2)
E(t)   = LPF( slope² )                    the tension-rise envelope — §12.2's discarded DC half
```

**Pitch law.** Pitch goes as `√T`, so for a small rise `ΔT/T ≪ 1`:

```
f0(t) = f0·√(1 + ΔT/T) ≈ f0·(1 + κ_t·E(t))
```

`κ_t` folds the `½·(1/T)` sensitivity and the `EA/2L` tension leverage into one calibrated
constant — the same normalised-unit reasoning as §12.2's `κ`, and calibrated the same way
(measured against the level a real note shows, not derived, because every SI term is in §6/§11.1's
normalised system). Because tension is a property of the **whole wire**, every partial shifts by
the *same fraction* `δ(t)=κ_t·E(t)`: the series stretches uniformly — this is a re-tuning, not a
detuning of the fundamental alone.

**Applying the shift without per-partial trig.** Partial `n`'s pole angle is `θ_n = 2π f_n/f_s`;
shifting `f_n → f_n(1+δ)` changes only the recurrence's `cos` coefficient (`a1_n = 2 r_n·cos θ_n`;
the radius `r_n` is set by `T60` and does not move). `δ` is tiny — the acceptance target is
`≤ 2 cents ≈ 1.2×10⁻³` — so a first-order expansion is exact to `~(θ_nδ)²/2 ≲ 10⁻⁵` and needs no
`cos`:

```
cos(θ_n(1+δ)) ≈ cos θ_n − (θ_n·sin θ_n)·δ
a1_n(δ)        = 2 r_n·[ cos θ_n − (θ_n·sin θ_n)·δ ]
```

`θ_n`, `sin θ_n` and `cos θ_n` are all already computed in `update()`, so a re-tune costs one
multiply–add per partial and no trig. The stored per-entry sensitivity is `b_n ≡ −θ_n·sin θ_n`
(so `cos_bent = cos θ_n + b_n·δ`), which composes cleanly with the damper's radius recompute in
`recomputeEffectivePartials()`. The re-tune is **decimated** to once per `kBendRetuneInterval`
samples: `E(t)` evolves over tens of ms, so a ~1 ms grid is inaudible, and this keeps the
coefficient pass — the one thing that must *not* run every sample — off the hot path. `δ` is
clamped to `±kMaxPitchDelta` as a stability guard so a pathological input energy cannot push a
pole coefficient out of range.

**Envelope corner — the one place M8 does not literally reuse §12.2's filter.** `E(t)` is the
low-passed square of the slope. `slope²` of a note at `f0` carries the DC energy we want *plus*
ripple at `2·f0` and above; §12.2's DC-blocker corner is 60 Hz, which barely attenuates a
bottom-octave note's `2·f0 ≈ 55 Hz` ripple and would wobble the pitch audibly. M8 therefore uses
a dedicated slower one-pole at `f_E ≈ 12 Hz` (`τ_E ≈ 13 ms`, which also matches the physical
settling of the rise). Same quantity as §12.2, deliberately slower corner.

**Per string, not per voice** — the opposite choice from §12.4's longitudinal bank. The tension
rise is a property of *that individual string's* motion, each `StringPartialBank` already computes
its own `slope`, and the re-tune must land on that string's own partials, so each bank owns its
tension state and self-modulates. There is no shared-bank saving to be had as there was for the
longitudinal modes. Disabled (`κ_t = 0`) the whole path is skipped, so a voice that opts out pays
nothing.

### 13. Skipping silent voices (a performance gate, with a physics precondition)

A voice pool renders every slot every sample whether it sounds or not, so one note
costs almost as much as eight. That is pure waste during normal playing, where most of the
pool is idle — but skipping a voice freezes its state, and freezing the wrong voice is a
*physics* bug, not just a glitch.

A voice may be frozen only when **both** hold:

```
damperValue >= 0.999          fully damped
outputEnvelope < 1e-5         inaudible (~-100 dBFS)

outputEnvelope[n] = max( |y[n]|,  outputEnvelope[n-1]·e^(-1/(0.05·f_s)) )
```

**The damper condition is the important one.** An *undamped* string is still coupled to the
shared bridge and can be re-excited by other notes — that is §8's sympathetic resonance and
`REQ-piano-6`. Freezing a quiet-but-undamped string would silently break it: the string would
sit at −100 dB and never respond, where a real one would ring. A *damped* string is already
gated off that path by the `(1 − damperValue)` factor in `PianoVoice::generate()`, so it cannot
be re-excited, cannot become audible again, and is therefore safe to freeze until it is struck
— at which point `noteOn()` calls `reset()` anyway.

The threshold is ~−100 dBFS: below the 16-bit noise floor (−96 dB) and about five orders of
magnitude under a struck note's peak. The 50 ms release stops a note that passes briefly through
zero from tripping the gate mid-ring.

Implemented in `PianoVoice::isSilent()`; applied by `PianoEngine::renderFrame()`.

---

## Parameters

| Class | Setter | Unit | Symbol |
|---|---|---|---|
| `StringResonator` | `setFrequencyHz` | Hz | `f_n` |
| | `setDecaySeconds` | s (T60) | `T60_n` |
| `StringPartialBank` | `setFundamentalHz` | Hz | `f0` |
| | `setInharmonicity` | — | `B` |
| | `setBaseDecaySeconds` | s (T60) | `T60_fundamental` (latches an override, §3) |
| | `setBrightnessDecaySeconds` | s (T60 @ 5 kHz) | `T60_ref` → solves `c₃`/`c₁` (§3) |
| | `setStrikePosition` | 0..0.5 | `β` |
| | `setModalMass` | normalised (C4 = 1) | `m` (§11.1) |
| | `setTensionModulation` | normalised | `κ_t` — slope² envelope → uniform pitch glide; 0 disables the stage (§12.5) |
| | `pitchModulation()` / `tensionEnergy()` | — | current `δ` / `E(t)`, exposed for tests (§12.5) |
| | `setDamperEngagement` | 0..1 target | `d` (ramped) |
| | `setDamperEngageMs` | ms | ramp time |
| `HammerExciter` | `setMass` | normalized | `m_h` |
| | `setStiffness` | normalized | `K` |
| | `setNonlinearExponent` | — | `p` |
| | `setHysteresisLoss` | 0..1 | `ε` |
| | `strike(velocity)` | 0..1 | → `v0` |
| `LongitudinalBank` | `setFundamentalHz` | Hz | the *transverse* `f0`; `f_long,m` and `g_reg` both derive from it (§12) |
| | `setTensionCoupling` | normalised | `κ` — slope² → tension modulation; 0 disables the stage (§12.2) |
| | `firstModeHz(f0)` / `registerGainFor(f0)` *(static)* | — | the §12.1 / §12.4 laws, exposed for tests |
| | `isActive()` / `activeModeCount()` | — | false/0 above the §12.4 crossover: no resonators run |
| `PianoBridge` | `setCouplingGain` | linear | sympathetic feedback gain |
| | `setRadiationGain` | linear | output gain |
| `PianoVoice` | `setFrequency` (override) | Hz | `f0` (→ all unison banks, and the §3 default `T60_1` unless overridden) |
| | `setUnisonCount` | 1..3 | `U` |
| | `setUnisonDetuneCents` | cents | `c` |
| | `setBaseDecaySeconds` | s (T60) | `T60_fundamental` (latches an override, §3) |
| | `setBrightnessDecaySeconds` | s (T60 @ 5 kHz) | `T60_ref` (§3) |
| | `setTensionCoupling` | normalised | `κ` → the owned `LongitudinalBank` (§12.2) |
| | `setTensionModulation` | normalised | `κ_t` → all unison banks; the §12.5 pitch glide (latches an override) |
| | `setUnaCorda` | bool | soft-pedal gate |
| | `setDamperHeld` | bool | sustain/sostenuto gate |
| | `setStrikePosition` / `setModalMass` / `setHammerMass` / `setHammerStiffness` | — | each **latches an override** of the matching §11 law |
| | `defaultBaseDecaySeconds(f0)` *(static)* | s (T60) | the §3 pitch→decay curve, exposed for tests |
| | `defaultModalMass` / `defaultHammerMass` / `defaultHammerStiffness` / `defaultStrikePosition` / `defaultUnisonCount` *(static)* | — | the §11 register-scaling laws, exposed for tests |

Code cross-reference: every formula above is implemented in the `update()`/`process()` of the
class named in its section header — see the class map table for the file. §3's per-partial
`α_n`/`T60_n` solve is in `StringPartialBank::update()`; §3's pitch→`T60_1` curve is
`PianoVoice::defaultBaseDecaySeconds()`, applied from `PianoVoice::setFrequency()`.

**Read-only introspection** (`StringPartialBank::partialCount()`, `partialFrequencyHz(i)`,
`partialDecaySeconds(i)`) exposes the computed per-partial `f_n` and natural `T60_n` so the
tests can assert §2/§3's formulas *exactly* rather than inferring them statistically from
rendered audio.
