# resample — one sound held to another device's clock (REQ-resample-2, REQ-resample-3)

Two sound cards never agree on what 48 000 Hz is: each counts its own crystal, and 48 000 against 48 003 is a frame
gained every 16 seconds — a click within minutes. A host that plays one song on two devices runs its engine on one
(the CLOCK) and feeds every other (a FOLLOWER) through a buffer read a little faster or slower, so the buffer neither
fills nor empties (Solaris R-DEV-2). These are the two pieces of that reading; the buffer, the threads and the time
stamps are the host's.

| class | what it does | REQ |
|---|---|---|
| `SincKernel` | the band-limited kernel REQ-resample-1 already used — a Kaiser-windowed sinc, tabulated — shared | REQ-resample-1 |
| `AdaptiveResampler` | streaming, any channel count: each output block read `step` input frames a frame, `step` free to change every block with no click | REQ-resample-2 |
| `FillController` | a PI loop on how full the buffer is: the step that holds the fill at its target, and the drift it implies | REQ-resample-3 |

**Why not composed (rule 1):** the kernel IS reused — `spectral/TimeStretch`'s `resample` (REQ-resample-1) reads a
whole buffer at one fixed step; its kernel moved here as `SincKernel`, unchanged (the same numbers, the same
order), and both use it. What was missing is the streaming half: a read position carried across blocks, a step
that changes between them, and the history a block needs from the one before. No filter, delay or controller in the
library holds a phase across calls at a changing rate, so `AdaptiveResampler` is new; `FillController` is a
textbook PI loop with a one-pole smoother, written down below so its gains come from a stated settling time and
not from tuning.

## Math

```
(R2) h(t) = c · sinc(c·t) · I0(β√(1 − (t/R)²)) / I0(β),  |t| < R = Z/c   — SincKernel (spectral/README (R2), moved)
(R3) h tabulated at 512 points a sample and read linearly                — SincKernel::operator()

(A1) Position — AdaptiveResampler::process
     the read position P = n + φ (n an integer input frame, 0 ≤ φ < 1), carried across calls;
     output j of a block read at  t_j = φ + j·s  (s = step, input frames per output frame, fixed within the block);
     after a block of N:  P ← P + N·s  (φ the fractional part, n the whole) — so a new step bends the read on,
     never jumps it: the phase is continuous whatever s does
(A2) Output — AdaptiveResampler::process
     y[j] = Σ_{d = −R+1 … R} w_d · x[n + ⌊t_j⌋ + d],   w_d = h(f − d) / Σ_d h(f − d),   f = t_j − ⌊t_j⌋
     c = 1 (a step near 1: no cutoff to lower), Z = 16 so R = 16 — 32 taps; the weights normalised to sum 1, so a
     constant passes exactly at every phase (an unnormalised window's ripple would beat slowly as φ sweeps)
     the same weights for every channel
(A3) Input needed — AdaptiveResampler::inputNeeded
     a block of N reads up to frame  n + ⌊φ + (N−1)·s⌋ + R;  it needs that minus the last frame it holds, ≥ 0
     — computed with the same expression (A1) uses, so what is asked for is exactly what is read
(A4) Lookahead — AdaptiveResampler::lookahead
     the frame it plays is R frames behind the newest it needs: it holds R frames more than a buffer would —
     a constant delay of R/fs (0.33 ms at 48 kHz); before the first input, R frames of silence lie behind frame 0
(A5) Held — AdaptiveResampler::buffered:  (frames given) − P,  fractional
     a buffer's true fill is what waits in it PLUS this: frames handed over are whole, the read is not, and a fill
     counted in whole frames carries a frame of noise into (F3) every block

(F1) Smoothing — FillController::update, a block of B frames at rate fs
     F̄ ← F̄ + α (F − F̄),   α = 1 − exp(−B / (τ·fs)),   τ = smoothSeconds (4 s)
(F2) Error, frames:  e = F̄ − T   (T the target fill)
(F3) PI:  I ← clamp(I + Ki·e·B/fs, ±δmax);   δ = clamp(Kp·e + I, ±δmax);   step = s0·(1 + δ)
     δmax = maxPpm·10⁻⁶ (2000 ppm); the integral clamped too, so a long stall cannot wind it up
(F4) Gains from the settling time — the linearised buffer, a producer at s0·fc_true and a consumer at fs:
     dF/dt = fs·(ε − δ),  ε the producer's rate over the consumer's, less 1
     with δ = Kp·e + ∫Ki·e dt:  e'' + fs·Kp·e' + fs·Ki·e = 0   →   ωn² = fs·Ki,  2ζωn = fs·Kp
     Ki = ωn²/fs,  Kp = 2ζωn/fs,  ωn = 2π/Tn,  Tn = naturalSeconds (60 s),  ζ = damping (1: no overshoot)
     the smoother's pole (1/τ = 0.25 rad/s) sits above ωn (0.105 rad/s): it costs the loop ~23° of phase
     why slow: what the host measures carries its scheduler's jitter (a millisecond is 48 frames); through Kp
     that would wobble the step block to block — F̄ over 4 s and a minute's loop leave well under a ppm of it
(F5) Drift — FillController::driftPpm:  at rest e = 0, so δ = I = ε;  published as −I·10⁶ —
     "+" when the consumer's crystal runs fast against the producer's;  reset(fill, drift) starts I there —
     a device's drift from its last run, so a new run is locked from its first block
(F6) A new target — FillController::setTarget:  T moves toward it by at most rampPpm·10⁻⁶·B frames a block
     (200 ppm: 480 frames in 50 s) — the step follows the ramp, never a kick of Kp·ΔT
```

| parameter | symbol | unit / range | used in |
|---|---|---|---|
| `AdaptiveResampler` `channels` | — | ≥ 1 | (A2) |
| `AdaptiveResampler` `maxOut`, `maxStep` | — | the largest block, the largest step it is sized for | (A3) |
| `process` / `inputNeeded` `step` | s | input frames a frame, > 0 (near 1 here) | (A1), (A3) |
| `FillParams::target` | T | frames | (F2) |
| `FillParams::rate` | fs | the consumer's rate, Hz | (F1), (F4) |
| `FillParams::naturalSeconds` | Tn | s (60) | (F4) |
| `FillParams::damping` | ζ | (1) | (F4) |
| `FillParams::smoothSeconds` | τ | s (4) | (F1) |
| `FillParams::maxPpm` | δmax·10⁶ | ppm (2000) | (F3) |
| `FillParams::rampPpm` | — | ppm (200) | (F6) |
| `update` `fill`, `frames` | F, B | frames | (F1)–(F3) |

**What the fill must be.** `update` takes the fill as the host MEASURES it. A raw count of what is in a buffer filled
in one device's blocks and emptied in another's is a sawtooth a block deep, and its phase slides at the drift — a
beat far slower than any smoother — so the host estimates the fill between pushes (frames pushed, plus the time
since the last push at the producer's rate), and adds what the resampler holds (A5). Solaris does this with a time
stamp per push.

**Limits, said:** c = 1 suits a step within about 1 % of 1 — the passband is flat to 0.41·fs (19.7 kHz at 48 kHz),
and what lies between that and Nyquist may fold just under it; a step far from 1 (another device rate) wants
c = min(1, 1/s) and a longer kernel. A target changed while running travels through the loop — the fill moves
to it at rampPpm (F6), never a jump. The linear reading of the kernel's table (R3) bounds the interpolation at about
93 dB below the sound (its error ≈ h''·Δ²/8 a tap, Δ = 1/512) — far under a follower's own converters.
