# spectral — the FFT, a time-stretcher and pitch shifter, a band-limited resampler (REQ-fft-1, REQ-stretch-1…3, REQ-resample-1)

What a DAW needs to make a recording follow a song's tempo, or sing a key higher, without the chipmunk: a sound's
LENGTH and its PITCH moved apart (Solaris R-WARP-1). All of it is ANALYSIS of a whole sound — done once, off any audio
thread, allocating as it goes — and deterministic: no clock, no threads, no randomness.

| what | does | REQ |
|---|---|---|
| `FFT` | the DFT of 2^m points, in place, both ways | REQ-fft-1 |
| `timeStretch` | a sound `ratio` times as long, its pitch kept, or moved `semitones` with its length kept — or both | REQ-stretch-1…3 |
| `resample` | a sound read every `step` samples, band-limited | REQ-resample-1 |

**Why not composed (rule 1):** the library had no transform into frequency (its spectrum meter is a bank of
band-pass filters, which cannot be inverted), and no fractional-rate reader beyond the Sampler's linear interpolation
(fine for a voice, audibly aliased for a shift of a whole recording) and the meters' fixed 4×/8× interpolator. The
onsets that keep attacks sharp are composed: `meter/Onsets` (REQ-onset-1).

## Math

```
(F1) Forward — FFT::forward              X[k] = Σ_{n<N} x[n] · e^(−2πi kn/N)
(F2) Inverse — FFT::inverse              x[n] = (1/N) Σ_{k<N} X[k] · e^(+2πi kn/N)      (the conjugate twiddles, then 1/N)
(F3) Radix 2, decimation in time — FFT::transform
     x in bit-reversed order; then for len = 2, 4, … N, each block of len, j < len/2:
       t = W^(j·N/len) · x[s + j + len/2];  x[s + j + len/2] = x[s + j] − t;  x[s + j] += t
     W^j = e^(−2πi j/N) computed once each as (cos, sin) of −2πj/N, j < N/2 — never by repeated multiplication

The phase vocoder (timeStretch, vocode):
(P1) Frames — timeStretch
     N = fftSize (a power of two), O = overlap, Hs = N/O (the synthesis hop),  β = ratio · 2^(s/12)
     synthesis centres ts_k = k·Hs,  analysis centres ta_k = round(k·Hs/β),  k = −N/(2Hs) … (βL + N/2)/Hs + 1
     (half a window before the start and past the end, so every output sample is under O windows)
(P2) Analysis — vocode          X_k[b] = FFT( w[n] · x[ta_k − N/2 + n] ),  x = 0 outside the sound
     w[n] = ½ − ½·cos(2πn/N)   (periodic Hann),  bins b = 0 … N/2
(P3) A peak's frequency from its phase's travel over the ACTUAL hop Ha = ta_k − ta_{k−1}:
     ω_b = 2πb/N,  Δ = princarg(φa_k[b] − φa_{k−1}[b] − ω_b·Ha),  ω̂_b = ω_b + Δ/Ha,   princarg into [−π, π)
     φs_k[b] = φs_{k−1}[b] + ω̂_b · Hs
(P4) Identity phase locking (Laroche & Dolson 1999): a peak is a bin no smaller than the two either side; every other
     bin keeps its phase relation to the peak whose region holds it (half way to the next peaks):
     φs_k[q] = φs_k[b] + (φa_k[q] − φa_k[b])                — the "phasiness" of a plain vocoder, gone
(P5) The first frame: φs = φa.
(P6) Transients — timeStretch: the onsets o_i of the channels' mean (meter/Onsets, sensitivity `transients`; < 0: none).
     The analysis centre of a frame is a TIME MAP of its synthesis centre, ta_k = round(T(ts_k)); T is piecewise linear
     through the anchors (0, 0), then for each onset a RIGID ZONE
        (β·o_i − A_i, o_i − A_i) … (β·o_i + B_i, o_i + B_i)       slope 1: the attack copied, not stretched
        A_i = min(N/2, ¼·min(1, β)·(o_i − o_{i−1})),   B_i = min(N/2, ¼·min(1, β)·(o_{i+1} − o_i))   (o_0 = 0, o_{m+1} = L)
     then (β·L, L); between zones the slope is what is left (both gaps stay > 0, so T rises); outside [0, βL], 1/β.
     A frame centred before a zone reads input before o_i − A_i + N/2 ≤ o_i: no frame before the attack holds it. The
     first frame centred in a zone takes its own phases, φs_k = φa_k (Röbel's phase reset); inside it Ha = Hs, so by
     (P3) the zone is the input, moved to β·o_i, exactly. The attack lands at β·o_i and nothing of it is ahead of it.
     (Without a map — T(ts) = ts/β — an attack in the right half of a window is placed up to N/2 early.)
(P7) Synthesis — vocode         y[ts_k − N/2 + n] += (8/(3O)) · w[n] · IFFT( |X_k| · e^(iφs_k) )[n]
     Σ_k w²[n − k·Hs] = 3O/8 for the periodic Hann at O = 4, 8 — so β = 1 gives y = x exactly: with Ha = Hs,
     (P3) makes φs ≡ φa (mod 2π) every frame (identity, to rounding)
(P8) Pitch — timeStretch       stretched to round(β·L) frames, then read back every 2^(s/12) samples by (R1)…(R3):
     round(ratio · L) frames, every frequency × 2^(s/12)
(P9) A time map — StretchParams::map, (output o_k, input i_k) from (0, 0), both rising (else ignored): where each input
     frame is heard, a tempo that changes. The BASE map runs in stretched time, (o_k·2^(s/12), i_k); the output is
     round(o_last) frames; the input past i_last is not read. (P6)'s zones sit where the base map hears each onset,
     c = base⁻¹(o_i), their sizes a quarter of what lies between in input AND in stretched time; a GAP between zones
     (ze, ie) … (zs, is) follows the base map's own shape scaled to meet them:
        T(t) = ie + (base(t) − base(ze))·(is − ie)/(base(zs) − base(ze))
     (with no map the base is the line t/β and T is (P6)'s line, computed as before). Outside, the base's end slopes.

The resampler (resample):
(R1) out[j] = Σ_m in[m] · h(j·step − m),  m within R of j·step;  c = min(1, 1/step)  (the cutoff falls when it reads faster)
(R2) h(t) = c · sinc(c·t) · I0(β√(1 − (t/R)²)) / I0(β),  |t| < R = Z/c,  Z = 16 zero crossings,  β = 8.6 (Kaiser)
     sinc(u) = sin(πu)/(πu);  I0 by its series Σ ((x/2)^k / k!)²  — SincKernel (src/resample, shared with REQ-resample-2)
(R3) h tabulated at 512 points a sample and read linearly — SincKernel::operator(), used by resample
```

| parameter | symbol | unit / range | used in |
|---|---|---|---|
| `StretchParams::ratio` | the output's length over the input's | 0.25 … 4 | (P1), (P8) |
| `StretchParams::semitones` | s, pitch × 2^(s/12) | −24 … +24 | (P1), (P8) |
| `StretchParams::fftSize` | N | 256 … 16384, a power of two (2048 at 48 kHz = 43 ms) | (P1), (P2) |
| `StretchParams::overlap` | O | 4 or 8 | (P1), (P7) |
| `StretchParams::transients` | the onset sensitivity of (P6)'s zones | 0 … 1, < 0 none (a plain vocoder) | (P6) |
| `StretchParams::map` | (P9)'s anchors, output and input frames | from (0, 0), both rising; empty: `ratio` | (P9) |
| `resample` `step` | input samples a sample | > 0 | (R1) |

**Limits, said:** a rigid zone is at most a window (N) long and at most half of what lies between two onsets (a
quarter each side), so hits closer than about 4N/min(1, β) get smaller zones — a dense roll is stretched more like a
plain vocoder; the frames that cross into a zone mix carried and reset phases, which can colour the quiet just before
an attack. Channels are vocoded apart (the same frames, the same resets) — a wide stereo image can lose a little focus at a
large ratio.
