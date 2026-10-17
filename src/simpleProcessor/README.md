# simpleProcessor — `Delay`, `Gain`

The two smallest processors: a delay line the effects read at any fraction of a sample (`Chorus`, `Repeater`, and
`Delay::process` itself), and a gain.

## Math — `Delay.cpp` (REQ-delay-1)

```
(DL1) write(x): the buffer's 0 becomes x, everything else one further back      (`Delay::write`)
(DL2) read(D) BEFORE the next write, D = i + f (i whole, 0 ≤ f < 1):              (`Delay::read`)
      y = (1 − f)·b[i] + f·b[i + 1]       — b[k] the sample written k writes ago (b[0] the newest)
      so read(D) is the input D + 1 samples back, between samples; at a whole D exactly b[D]
(DL3) its response, a sine through a still fractional delay:  H(ω) = (1 − f) + f·e^(−jω),  |H| ≤ 1
      (linear interpolation's low-pass: f = ½ gives |cos(ω/2)| — −11.74 dB at 20 kHz, 48 kHz)
```

Before Solaris D-18 (2026-10-17) `read` carried the line through b[i − 1] and b[i] on past the newer one —
(1 + f)·b[i] − f·b[i − 1], extrapolation, |H| up to 1 + 2f toward Nyquist (+5.8 dB at 20 kHz for f = ½): a swept
chorus gained moving treble, and an echo fed back brightened at each repeat. Whole-sample reads never changed.
Guarded by `Delay_reads_between_samples` (unittest/fxTests.cpp: every sample (DL2) to 1e−12 at fractions 0, ¼, ½, ¾,
0.9; the 20 kHz sine at (DL3) to 0.01 dB).
