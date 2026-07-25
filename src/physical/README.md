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
| `PianoVoice` | `SignalGenerator` | Owns U unison `StringPartialBank`s (§5) + one `HammerExciter` + damper state (§7) + pedal state (§9 pedals) + secondary-noise bursts (§9 noise); ties §1–§9 together; `generate(channel)` is the physical step. |

---

## Math

Units: force in abstract normalized units (this is a *signal-level* model, not
SI-calibrated to real newtons — every "N" below is normalized so `StringResonator` output
stays in the same ±1-ish range as every other `SignalGenerator` in this codebase); time in
seconds unless stated; `f_s` = `AudioConfig::sampleRate()`, `dt = 1/f_s`.

**Two empirical calibration constants** (measured against the actual implementation, not
derived — documented per rule 2 rather than left as unexplained magic numbers):

```
force_into_strings = HammerExciter.out() · kHammerToStringGain · voicingGain(f0)
kHammerToStringGain = 5.8e-6
voicingGain(f0) = clamp( (f0 / 261.6 Hz)^0.8,  0.1,  10.0 )
```

`kHammerToStringGain` brings the hammer ODE's force scale (which runs into the thousands at
high stiffness/velocity, §6) down to the resonator bank's signal scale.

`voicingGain` compensates a **real** physical trend rather than an implementation artifact.
With impulse-normalised partials (§1) a struck mode's amplitude scales as roughly `1/ω`, so at
equal hammer velocity the bass is *genuinely* far louder than the treble — measured, a ~78×
peak spread from A0 to C8, closely tracking the expected `1/ω` (128× over that range). Real
pianos flatten the same trend with **per-register voicing**: lighter, harder hammers and
higher tension toward the treble. Until `M6` models that properly
(`docs/piano-physics-plan.md` §M6), one documented curve stands in for it — measured peak
`f^-0.9`, so `f^0.8` flattens the keyboard to a ~2.5× spread (0.34 → 0.86 at full velocity,
nothing clipping) while keeping a natural mild bass emphasis.

> **This is not the `registerGain` curve M1 deleted.** That one compensated a *bug* — §1's
> sustained-drive gain normalisation made loudness track decay time, which was invisible while
> every note decayed in 3 s and became a 95× imbalance (and clipping at 880 Hz) the moment M1
> gave each note its own T60. `voicingGain` compensates physics the instrument itself
> compensates, and is *replaced* by M6 rather than deleted.

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
f_n = n · f0 · sqrt(1 + B n²)          n = 1 .. N   (N = kPartialCount = 12)
```

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
g_n = | sin(n π β) |                  StringPartialBank::setStrikePosition(β), default β ≈ 1/8
force_into_partial_n[t] = F_hammer[t] · g_n · (2/N)
```

`(2/N)` is the standard mode-superposition normalization for N truncated modes (keeps total
injected energy from scaling with the arbitrary partial count `N`). This is the real
comb-filter mechanism (`REQ-piano-4`): whenever `β ≈ k/n` for integer `k`, `g_n ≈ 0` and that
partial is suppressed — e.g. striking at `1/8` suppresses partials 8, 16, 24…

### 5. Unison strings — detuning, beating, and *emergent* two-stage decay

Each note has `U` (`kUnisonStrings`, default 2, settable 1–3) full `StringPartialBank`
instances, detuned a few tenths of a Hz apart:

```
f0_k = f0 · 2^(detuneCents_k / 1200)        k = 0 .. U-1
detuneCents_k spread symmetrically around 0, e.g. U=2 -> {-c/2, +c/2}, U=3 -> {-c, 0, +c}
```
(`c` = `PianoVoice::setUnisonDetuneCents`, default ≈ 0.6 cents — a few tenths of a Hz at
piano pitches, matching real piano unison tuning spread.)

All `U` banks are struck by the *same* hammer force (§6) and summed. **Deliberately not
implemented as a hand-authored envelope:** because the `U` strings are at nearly-identical
frequencies, their damped sinusoids beat (`|f_k − f_j|` Hz) and, critically, their combined
(coherent) motion couples into the shared `PianoBridge` (§8) much more strongly than a single
string's residual, decorrelated ringing does — so the *coupled system itself* naturally
produces a fast initial decay (energy dumping into the bridge while the strings are roughly
in phase) followed by a slower tail (once phase has drifted and each string is left mostly
driving itself) — the real piano's well-known two-stage decay. `REQ-piano-3` requires this be
emergent, not a second decay-rate constant bolted on; it is not — §3's `T60_n` is the only
per-partial decay parameter, the two-stage shape comes from unison superposition +
bridge coupling.

### 6. Hammer–string contact — nonlinear, hysteretic (`HammerExciter`)

A single-degree-of-freedom nonlinear-spring hammer model (the standard simplified
alternative to solving the full coupled hammer+string PDE in real time): the hammer felt is a
compression-only nonlinear spring, asymmetric between loading (compressing) and unloading
(rebounding) so contact dissipates energy — this asymmetry *is* the hysteresis, a documented
simplification of felt viscoelasticity (not the full Stulov model).

State: hammer compression `x_h` (≥ 0), velocity `v_h`. At `strike(velocity)` (called from
`PianoVoice::noteOn`): `x_h ← 0`, `v_h ← v0`, contact begins.

```
v0 = vMaxImpact · velocity              velocity ∈ [0,1] (noteOn arg), vMaxImpact empirical (default 4.0)

c[n]  = max(0, x_h[n])                                    (felt compression this sample)
F[n]  = K · c[n]^p                       if v_h[n] ≥ 0     (loading)
F[n]  = K · (1 − ε) · c[n]^p             if v_h[n] < 0      (unloading — softer, dissipative)

v_h[n+1] = v_h[n] − (F[n] / m_h) · dt     (semi-implicit/symplectic Euler — stable for a stiff
x_h[n+1] = x_h[n] + v_h[n+1] · dt          nonlinear spring at audio sample rates)
```

Contact ends when `x_h` returns to `0` while `v_h < 0` (hammer rebounds off the string), or a
safety bound (`kMaxContactMs`, default 15 ms) is reached, whichever comes first — matches
typical real hammer-string contact durations of 1–5 ms scaling with impact velocity (harder
strikes → shorter, stiffer contact → brighter tone), which **falls out of the ODE above**
rather than being separately curve-fitted.

`F[n]` (the reaction force, Newton's third law) is what's fed into `StringPartialBank` as
`F_hammer` in §4.

**Properties (all empirical/typical, documented as such per rule 2 — not derived from a
measured instrument):** `massID` → `m_h` (default 1.0, normalized hammer inertia),
`stiffnessID` → `K` (default 1×10¹⁰, measured — via a standalone stiffness scan against this
ODE — to put contact duration in a realistic ≈1–9 ms window: ≈2.6 ms at max velocity down to
≈9.3 ms at a very soft touch, matching real hammer contact times shortening with impact
force), `nonlinearExponentID` → `p` (default 2.5; real felt is commonly cited in the 2–3.5
range), `hysteresisLossID` → `ε` (default 0.2 — 20% of loading stiffness lost on rebound).
`kMaxContactMs` (15 ms) is a numerical safety bound, not a physical target — at the tuned
default it only binds for very soft (near-zero-velocity) touches.

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

Body-mode frequencies/decays are a **documented generic placeholder set**, not measured from
a real instrument (`REQ-piano-14` — no measured IR / room acoustics in scope):
`{80, 130, 190, 250, 340, 420, 550, 700} Hz`, `T60 ≈ 0.3–0.6 s` each (shorter than string
`T60`s, so the board itself doesn't ring forever — it's a lossy coupling path, not a second
set of strings).

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

### 9. Pedals

- **Sustain** (`PianoEngine::setSustainPedal(bool)` → broadcasts `setDamperHeld(true/false)`
  to every voice): while held, `noteOff()` does **not** start the §7 damper-close ramp — the
  string keeps ringing (and keeps feeding the bridge) until sustain is released, at which
  point any note whose key is already up starts closing its damper.
- **Sostenuto** (`PianoEngine::setSostenutoPedal(true)`): at the instant it's pressed, the
  engine calls `setDamperHeld(true)` only on voices that are *currently sounding* (key still
  down or ringing); future notes struck while sostenuto is held are unaffected and damp
  normally on their own `noteOff()`.
- **Una corda** (`PianoVoice::setUnaCorda(bool)`): approximates the real "hammer shifts to
  strike fewer strings" mechanism (`REQ-piano-16`, explicitly *not* literal dynamic
  unison-count switching) as a hammer-excitation scale:

```
K_effective       = K · kUnaCordaStiffness          kUnaCordaStiffness default 0.85 (softer contact)
F_hammer_effective = F_hammer · kUnaCordaGain         kUnaCordaGain default 0.6 (quieter + mellower,
                                                       since a softer/shorter contact also shifts
                                                       energy away from high partials — §6's ODE)
```

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
| | `setDamperEngagement` | 0..1 target | `d` (ramped) |
| | `setDamperEngageMs` | ms | ramp time |
| `HammerExciter` | `setMass` | normalized | `m_h` |
| | `setStiffness` | normalized | `K` |
| | `setNonlinearExponent` | — | `p` |
| | `setHysteresisLoss` | 0..1 | `ε` |
| | `strike(velocity)` | 0..1 | → `v0` |
| `PianoBridge` | `setCouplingGain` | linear | sympathetic feedback gain |
| | `setRadiationGain` | linear | output gain |
| `PianoVoice` | `setFrequency` (override) | Hz | `f0` (→ all unison banks, and the §3 default `T60_1` unless overridden) |
| | `setUnisonCount` | 1..3 | `U` |
| | `setUnisonDetuneCents` | cents | `c` |
| | `setBaseDecaySeconds` | s (T60) | `T60_fundamental` (latches an override, §3) |
| | `setBrightnessDecaySeconds` | s (T60 @ 5 kHz) | `T60_ref` (§3) |
| | `setUnaCorda` | bool | soft-pedal gate |
| | `setDamperHeld` | bool | sustain/sostenuto gate |
| | `defaultBaseDecaySeconds(f0)` *(static)* | s (T60) | the §3 pitch→decay curve, exposed for tests |

Code cross-reference: every formula above is implemented in the `update()`/`process()` of the
class named in its section header — see the class map table for the file. §3's per-partial
`α_n`/`T60_n` solve is in `StringPartialBank::update()`; §3's pitch→`T60_1` curve is
`PianoVoice::defaultBaseDecaySeconds()`, applied from `PianoVoice::setFrequency()`.

**Read-only introspection** (`StringPartialBank::partialCount()`, `partialFrequencyHz(i)`,
`partialDecaySeconds(i)`) exposes the computed per-partial `f_n` and natural `T60_n` so the
tests can assert §2/§3's formulas *exactly* rather than inferring them statistically from
rendered audio.
