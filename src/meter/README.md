# meter — loudness, true peak, correlation, spectrum (REQ-meter-1…4)

What a mastering engineer reads off a mix. All four are ANALYSIS: they take blocks of `float` samples (as a host
mixes) and change nothing. They read the rate from `AudioConfig` when built or reset, allocate only then, and
process with no allocation, lock or I/O, so a host runs them on its audio thread (Solaris R-METER-1).

| class | what it reads | REQ |
|---|---|---|
| `LoudnessMeter` | momentary (400 ms), short-term (3 s) and integrated (gated) loudness, LUFS — ITU-R BS.1770-4 | REQ-meter-1 |
| `TruePeakMeter` | the inter-sample peak, dBTP — 4× oversampled (BS.1770-4 Annex 2) | REQ-meter-2 |
| `CorrelationMeter` | stereo correlation −1 … +1, smoothed over 300 ms | REQ-meter-3 |
| `SpectrumAnalyser` | 61 band levels, 1/6 octave, 19.7 Hz … 20.2 kHz | REQ-meter-4 |

**Why not composed (rule 1):** the spectrum IS a composition — a bank of `equalizer/Biquad` band-passes. The
K-weighting is not: BS.1770's pre-filter is a high shelf whose numerator mixes two gains (`Vh`, `Vb`) — not one of
the RBJ cookbook's shelves `Biquad::design` makes — so its two sections are designed here (`KWeighting::design`)
and run in `MeterSection`, a bare transposed-direct-form-II section with given coefficients. The true peak's
interpolator is an FIR, which the library had none of.

## Math

```
(L1) Pre-filter (high shelf) at rate fs — KWeighting::design
     f0 = 1681.974450955533 Hz, G = 3.999843853973347 dB, Q = 0.7071752369554196
     K  = tan(π f0 / fs),  Vh = 10^(G/20),  Vb = Vh^0.4996667741545416,  a0 = 1 + K/Q + K²
     b  = [ (Vh + Vb K/Q + K²)/a0,  2(K² − Vh)/a0,  (Vh − Vb K/Q + K²)/a0 ]
     a  = [ 1,  2(K² − 1)/a0,  (1 − K/Q + K²)/a0 ]
(L2) RLB weighting (high-pass) — KWeighting::design
     f0 = 38.13547087602444 Hz, Q = 0.5003270373238773,  K = tan(π f0 / fs),  a0 = 1 + K/Q + K²
     b  = [ 1, −2, 1 ],  a = [ 1,  2(K² − 1)/a0,  (1 − K/Q + K²)/a0 ]
     (L1)·(L2) at fs = 48 kHz are BS.1770-4's Tables 1 and 2 to 1e-9 (the bilinear derivation of the standard's
     analogue prototype, as libebur128 writes it) — one filter at every rate.
(L3) Mean square, channels summed with weight 1 (left, right) — LoudnessMeter::process
     z = Σ_ch (1/N) Σ_n y_ch[n]²       y = (L2)((L1)(x))
(L4) Loudness — LoudnessMeter::lufs
     L = −0.691 + 10·log10(z)  LUFS            (silence: −144)
     momentary = L over the last 400 ms (4 sub-blocks of 100 ms); short-term = L over the last 3 s (30 sub-blocks)
(L5) Gating blocks — LoudnessMeter::process
     400 ms blocks every 100 ms (75 % overlap); a block above the ABSOLUTE gate (−70 LUFS) is kept: in a histogram of
     0.01 LU bins from −70 to +10 LUFS, each bin counting its blocks and summing their z — so nothing grows with time
(L6) Integrated — LoudnessMeter::integrated
     Γr = L(mean z of the kept blocks) − 10 LU                    (the RELATIVE gate)
     integrated = L(mean z of the kept blocks whose bin lies at or above Γr)
     (exact but for the 0.01 LU bin Γr falls in)
(T1) The interpolator — TruePeakInterpolator::TruePeakInterpolator (factor F, the meter's F = 4)
     h[m] = sinc((m − c)/F) · w[m],  m = 0 … N−1,  c = (N − 1)/2,  N = 16F   (the meter: N = 64, c = 31.5)
     w[m] = I0(β √(1 − u²)) / I0(β),  u = 2m/(N−1) − 1,  β = 6          (Kaiser)
     `onSamples` (the Limiter's, README effects (L6)): N = 16F − 1, so c is an upsampled point ON a sample
(T2) F phases of 16 taps, h_p[k] = h[Fk + p], each normalised so Σ_k h_p[k] = 1 (a slow signal reads its level)
     I0 by its series Σ (x/2)^(2k) / (k!)²  — besselI0. Phase p's point lies (c − p)/F input samples back: for
     either design 7.875 − p/F … — between x[n−8] and x[n−7] (`kDelay` = 8). The meter's points fall at ⅛, ⅜, ⅝, ⅞
     of the way (BS.1770-4's own 48-tap reference is centred between points too); `onSamples` at F = 8 puts them
     every ⅛, the sample included — both grids a 4× meter may use. Flat to ±0.003 dB up to 16 kHz at 48 kHz.
(T3) True peak — TruePeakInterpolator::push, TruePeakMeter::process
     y_p[n] = Σ_k h_p[k] · x[n − k],  peak = max over n, p of |y_p[n]| and |x[n]|;  dBTP = 20·log10(peak)
(C1) Correlation — CorrelationMeter::process / correlation
     α = 1 − e^(−1/(0.3 fs));  E[·] ← E[·] + α(· − E[·])  for  LR, LL, RR
     r = E[LR] / √(E[LL]·E[RR])   (0 below 1e-12)
(S1) Bands — SpectrumAnalyser::bandHz / reset
     f_b = 1000 · 2^((b − 34)/6),  b = 0 … 60;  Q = 1 / (2^(1/12) − 2^(−1/12)) ≈ 8.651 (a 1/6-octave band)
     each a Biquad BandPass (RBJ, 0 dB at f_b — equalizer/README (B5)); none above 0.45 fs
(S2) Level — SpectrumAnalyser::process / bandDb
     x = (L + R)/2;  P_b ← P_b + α(y_b² − P_b) (α as (C1));  level = 10·log10(2 P_b)  dB re a full-scale sine
```

| symbol | meaning |
|---|---|
| fs | the rate, `AudioConfig::sampleRate()` |
| z | a block's K-weighted mean square, summed over the channels |
| Γr | the relative gate, 10 LU under the absolute-gated mean |
| h_p | phase `p` of the 4× interpolator |
| α | the one-pole coefficient of a 300 ms time constant |

Guarded by `KWeighting_at_48k_is_BS1770s_table`, `Loudness_of_a_sine_is_its_level`,
`Loudness_is_gated_absolutely_and_relatively`, `TruePeak_finds_the_peak_between_samples`,
`Correlation_is_one_minus_one_and_zero`, `Spectrum_puts_a_sine_in_its_band`, `Meters_do_not_allocate_after_construction`
(unittest/meterTests.cpp) and `meters_match_bs1770` (tests/run_integration.py: the integrated loudness against
BS.1770-4 computed from the standard's own table and gating with every block kept, to 0.05 LU; the true peak
against the signal's continuous maximum, to 0.3 dB).

## Onsets — Math (REQ-onset-1)

Where sounds begin in a whole recording (`detectOnsets`, `src/meter/Onsets.cpp`): an analysis done once, off any
audio thread — for slicing a recording at its transients.

```
(O1) Energy      L[m] = 10·log10( (1/N)·Σ_{i=mH}^{mH+N−1} x[i]² + 10⁻¹² )   dB;  N = 1024, H = 256
(O2) Flux        d[m] = max(0, L[m] − L[m−1])                               ; how much louder than the frame before
(O3) Threshold   T[m] = median( d[m−8 … m+8] ) + δ,   δ = 3 + 12·(1 − sensitivity)  dB
(O4) An onset    d[m] > T[m],  L[m] ≥ floor,  d[m] a peak (d[m−1] ≤ d[m] > d[m+1]),  ≥ minGap after the last
(O5) To the sample: in [(m−1)H, mH + N), the first i with |x[i]| > b + ¼·(max|x| − b),
     b = max|x| over the hop before the window                              ; what sounded already does not count
```

| parameter | default | symbol |
|---|---|---|
| `sensitivity` | 0.5 | δ (O3) |
| `minGapMs` | 50 ms | (O4) |
| `floorDb` | −60 dB | floor (O4) |

The constants N, H, the ±8-frame median and the quarter of (O5) are empirical — a hop of 5 ms at 48 kHz finds a
drum's hit, the median keeps a loud passage from reading as one long onset, and a quarter of the peak lands on the
attack's first steep rise rather than on its noise. Why not composed: the library had no analysis of a whole sound;
the energy is a plain mean square, no filter needed. (O5) measured from what sounded before: a quarter of the bare peak
lands on a steady tone underneath a soft hit, a window early (a test showed it). Guarded by `Onsets_are_found_where_sounds_begin`
(unittest/meterTests.cpp: four bursts found within 64 samples, none in a quiet noise floor, two closer than the gap
counted once, a lower sensitivity ignoring a soft one) and `onsets_find_the_drums` (tests/run_integration.py: a Drum
Machine's hits through the registry, found within 5 ms).
