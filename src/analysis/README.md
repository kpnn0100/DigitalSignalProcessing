# analysis — what a whole sound holds (REQ-analysis-1…4)

Offline analysis over a whole sound — done once, off any audio thread, allocating, deterministic (no clock, no threads,
no randomness: the same sound and parameters give the same answer). The same kind of module as `meter/`'s
`detectOnsets` and `estimateTempo`, for Solaris's analysis tools (R-ASSIST-1, docs/discussion.md Round 22):

| file | what | REQ |
|---|---|---|
| `Stft.h` | a short-time Fourier transform and its exact inverse | REQ-analysis-1 |
| `Separate.h` | harmonic / percussive separation | REQ-analysis-2 |
| `Transcribe.h` | a monophonic melody's notes | REQ-analysis-3 |
| `Chords.h` | the chords, beat by beat | REQ-analysis-4 |

**Composed, not rewritten:** the FFT is `spectral/FFT` (REQ-fft-1); the pitch is the tuner's YIN, lifted out of `Tuner`
into `meter/Yin` so both use one copy (REQ-tuner-1's (Y1)…(Y4)); the attacks are `meter/Onsets` (REQ-onset-1). New math
was needed only where the library had none: an STFT with an exact inverse (the time stretcher's phase vocoder keeps its
frames to itself and changes them — it does not give a sound back untouched), the median filtering, the note
segmentation, the chroma and the chord search.

## Math

### The STFT — `stft`, `istft` (Stft.cpp)

```
(S1) w[j] = ½ − ½·cos(2πj/N),  j = 0 … N−1                       (periodic Hann; N a power of two)
(S2) X_t[k] = Σ_j w[j]·x[t·h − N/2 + j]·e^(−2πi jk/N),  k = 0 … N/2,  h = N/4
     (x outside the sound is 0; frames t = 0 … ⌊L/h⌋ — centred on every hop, the first on the first sample)
(S3) y[i] = Σ_t w[i − t·h + N/2]·IFFT(X_t)[i − t·h + N/2] / Σ_t w[i − t·h + N/2]²
     (the half spectrum made whole by Hermitian symmetry, X[N−k] = X[k]*)
(S4) Σ_t w²[· − t·h] = 3/2 wherever four frames overlap — y = x exactly (to rounding); at the ends fewer frames
     overlap and the division by what is there keeps it exact
```

`fftSizeFor(rate, s)` — the power of two nearest `s` seconds (by its log): 2048 at 48 kHz for 43 ms, 8192 for 170 ms.

### Separation — `separateHarmonicPercussive` (Separate.cpp)

```
(H1) S = STFT(Σ_c x_c),  A_t[k] = |S_t[k]|         one magnitude for every channel: they split alike, the image kept
(H2) H̃_t[k] = median{ A_u[k] : |u − t| ≤ L/2 }      along time — a held tone (a horizontal line) survives
     P̃_t[k] = median{ A_t[b] : |b − k| ≤ M/2 }      along frequency — a hit (a vertical line) survives
     (the window shrinks at the spectrogram's edges)
(H3) M_H = H̃^p / (H̃^p + P̃^p)  (½ where both are 0),  M_P = 1 − M_H        p = 2: Wiener's soft masks
(H4) h_c = ISTFT(M_H · STFT(x_c)),  p_c = ISTFT(M_P · STFT(x_c))  →  h_c + p_c = x_c   (S4, M_H + M_P = 1)
```

| parameter | symbol | default |
|---|---|---|
| `fftSize` | N | 0: `fftSizeFor(rate, 0.0427)` |
| `timeMedian` | L | 17 frames (≈ 180 ms at 48 kHz) |
| `freqMedian` | M | 17 bins (≈ 400 Hz at 48 kHz) |
| `power` | p | 2 |

L and M are Fitzgerald's (2010) — empirical, kept as the paper gives them.

### Transcription — `transcribe` (Transcribe.cpp)

```
(T1) frames of W samples every W/8 (W = fftSizeFor(rate, 0.0427)): the level ℓ_t = 10·log10(Σ_{j<W/2} x² / (W/2))
     over the half YIN compares; voiced when ℓ_t ≥ gate and YIN (Yin::period, (Y1)…(Y4)) finds a period τ_t
(T2) n_t = round(69 + 12·log2(rate / τ_t / 440)), −1 unvoiced;  the frame's time (t·W/8 + W/4) / rate
(T3) ñ_t = median{ n_u : |u − t| ≤ 2 }  (5 frames): a lone octave slip or dropout does not break a run
(T4) a note: a run of one ñ ≥ 0, at least minNoteMs long
(T5) the library's onsets (REQ-onset-1) inside a run, more than minNoteMs from its ends, split it (a pitch struck
     again); an onset within W/2 of a run's first frame is its start (the attack, where YIN's first voiced frame
     lags it); a note ends where the next begins
(T6) velocity = clamp(round(1 + 126·(max ℓ + 60)/60), 1, 127) — its loudest frame, −60 … 0 dBFS
```

| parameter | symbol | default |
|---|---|---|
| `window` | W | 0: 2048 at 48 kHz |
| `threshold` | YIN's | 0.15 (the tuner's) |
| `gateDb` | gate | −50 dBFS |
| `minHz`, `maxHz` | the lags | 50 … 2000 Hz |
| `medianFrames` | | 5 |
| `minNoteMs` | | 60 ms |
| `onsetSensitivity` | REQ-onset-1's | 0.5 |

Monophonic: two pitches at once (a release under the next attack) have a common period that is neither note — a
melody is transcribed as it is played, one note at a time.

### Chords — `detectChords` (Chords.cpp)

```
(C1) S = STFT(x) with N = fftSizeFor(rate, 0.17);  for each bin k with f = k·rate/N in [110, 4000] Hz:
       m = 69 + 12·log2(f/440),  r = round(m),  w = max(0, 1 − 2|m − r|)
       c_t[r mod 12] += w·|S_t[k]|²                     (a triangle a semitone wide: full at the note, 0 half off)
(C2) a beat [b_i, b_{i+1}): v_i = the mean of c_t over its frames (by their centres t·N/4/rate), then v_i / ‖v_i‖;
     a beat shorter than a hop takes the frame nearest its middle; its level from its samples
(C3) score_i(r, q) = (v_i[r] + v_i[r + 3 or 4] + v_i[r + 7]) / √3       (minor: +3, major: +4) — the cosine with the
     triad's three classes;  score_i(N) = minScore;  a silent beat (< silenceDb): N scores 1, every triad 0
(C4) the path s_0 … s_{B−1} maximising Σ_i score_i(s_i) − stay·#{i : s_i ≠ s_{i−1}}  (Viterbi; ties keep the state)
     — a one-beat detour costs one change more than staying, so a passing note flips the chord only when the
     beat gains more than `stay`
(C5) equal neighbours merged into spans [b_first, b_last+1)
(C6) names: C C# D Eb E F F# G Ab A Bb B, "m" for minor; "N" none
```

| parameter | default | |
|---|---|---|
| `fftSize` | 0: 8192 at 48 kHz | 5.9 Hz bins: the semitones apart from 110 Hz up |
| `minHz`, `maxHz` | 110, 4000 Hz | |
| `minScore` | 0.6 | empirical: an unrelated noise scores under it against every triad |
| `silenceDb` | −60 dBFS | |
| `stay` | 0.1 | empirical: a passing note as loud as the triad (E minor over C by 0.066, measured) does not flip it |

## Tests

`unittest/analysisTests.cpp`: `Stft_gives_the_sound_back` (noise at three sizes, any length, to 1e-5; empty),
`Separation_splits_a_tone_from_its_hits` (the halves sum back; a half-level channel splits at half; the tone within
−13 dB, the clicks within −6 dB — −33 and −12 measured), `Yin_finds_a_period`, `Transcription_finds_a_melody` (four notes,
one pitch struck twice without a gap, starts within 5 ms; silence, a tone under the gate, a short sound: none),
`Chords_of_a_progression` (C Am F G, a passing B, a silent second). `tests/run_integration.py`:
`separation_hears_tone_and_clicks`, `transcription_of_a_synth_melody`, `chords_of_a_synth_progression` — the registry's
Basic Synth (one saw) played through the harness.
