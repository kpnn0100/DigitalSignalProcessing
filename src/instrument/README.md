# instrument/ — what a host plays

| class | what | requirement |
|---|---|---|
| `Instrument` | the interface: notes in, samples **added** into the host's buffers out | REQ-inst-1 |
| `BasicSynth` | two-oscillator subtractive polysynth, 16 voices | REQ-synth2-1…5 |
| `DrumMachine` | ten synthesized pads on the General MIDI drum notes | REQ-drum-1…5 |

The first host is the Solaris DAW (`apps/solaris` in the umbrella), whose rule is that **all of its
sound lives here** (Solaris R-DSP-1). Sample-accurate timing is the host's job: it splits a block at
every note event and calls `render` per piece, so an instrument never sees a time stamp.

Not to be confused with `synth/SynthEngine`, the ESP32 firmware engine: a fixed 8-voice, 3-oscillator
voice with a shared effects chain, built for a microcontroller's budget. `BasicSynth` reuses the same
`Oscillator` and `ADSREnvelope` but owns a filter per voice and no effects — a DAW puts effects on the
strip, not inside the instrument.

---

## BasicSynth — Math

### Signal flow (per voice, per channel)

```
x[n]  = L1·osc1[n] + L2·osc2[n] + N·noise[n]                         ; osc = Oscillator (PolyBLEP, unison)
y[n]  = SVF_mode( x[n] ; fc[n], res )                                 ; StateVariableFilter::tick, README (S1)…(S6)
out[n] = y[n] · amp[n] · G                                            ; amp = ADSREnvelope (linear stages)
```

### Formulas — `oscFrequency`, `cutoffFor`, `noteOn`

```
(Y1)  hz(m)      = 440 · 2^((m − 69)/12)                              ; m = MIDI note (fractional allowed)
(Y2)  f_osc,k    = hz(note + 12·octave_k + semi_k + fine_k/100)
(Y3)  fc[n]      = clamp( cutoff · 2^(envAmount·fenv[n] + keytrack·(note − 60)/12), 20, 20000 )
(Y4)  G          = 10^(volumeDb/20) · (1 − velocity + velocity·vel/127)
(Y5)  allocation: the same note retriggers its voice; else the first idle voice; else the voice
                  with the oldest note-on (smallest age) is stolen
(Y6)  noise seed  = 0x5EED0000 + 16·voice + channel                  ; one generator per voice PER CHANNEL
```

| parameter | symbol | unit / range | default |
|---|---|---|---|
| `osc1/2.wave` | — | sine · saw · square · triangle | saw / square |
| `osc1/2.octave`, `.semi`, `.fine` | (Y2) | −3..3 oct, −12..12 st, −100..100 ct | 0 / −1 oct, +7 ct |
| `osc1/2.level` | L1, L2 | 0..1 | 0.8 / 0.5 |
| `osc1/2.voices`, `.detune` | unison | 1..5, cents (Oscillator's own) | 1, 15 |
| `noise` | N | 0..1 | 0 |
| `filterMode` | — | LP · BP · HP · notch | LP |
| `cutoff` | (Y3) | Hz | 2400 |
| `resonance` | res | 0..1 → Q 0.707..32 (S5) | 0.25 |
| `envAmount` | (Y3) | octaves at full envelope, −8..8 | 2 |
| `keytrack` | (Y3) | 0..1 (1 = the cutoff follows the note exactly) | 0.5 |
| `filterEnv`, `ampEnv` | fenv, amp | ADSR: ms, ms, 0..1, ms | 3/350/0.25/300 · 3/120/0.8/250 |
| `volumeDb` | (Y4) | dB | −12 |
| `velocity` | (Y4) | 0..1 sensitivity | 0.8 |

**Why exponential cutoff modulation (Y3).** Pitch is heard logarithmically, so an envelope that adds
*octaves* sweeps evenly by ear, and key tracking of 1 keeps the filter at a fixed interval above the
note — the behaviour of an analog synth's 1 V/oct control voltage.

**The oscillators' own envelopes are gates** (attack 0, decay 0, sustain 1, release 0), opened at
note-on and closed when the amplitude envelope finishes. `Oscillator` owns an ADSR by the library's
voicing model; letting it shape the sound would put an amplitude envelope *before* the filter and
cut the release tail. The amp envelope runs after the filter: oscillators → filter → amplifier.

**Why one noise generator per channel (Y6).** Each block renders one channel and then the next;
a generator shared by the channels hands each one whichever stretch of the sequence the block size
leaves it, so the same notes chopped into blocks of 128 and of 77 sounded different. Found by
Solaris's chunking-determinism test; guarded here by `BasicSynth_noise_does_not_depend_on_block_size`.

**Levels (empirical).** One voice at `volumeDb = 0`, velocity 127, default patch, C4 peaks at 1.41
(two oscillators summing); the default −12 dB puts a note at ≈ 0.35, leaving headroom for chords.

Verified: equal temperament to 0.026 cents (`synth2_equal_temperament`, Python recomputes (Y1));
the filter removes > 25 dB of the energy above 3 kHz at a 300 Hz cutoff, and its envelope makes the
attack > 15 dB brighter than the sustain (`BasicSynth_filter_cutoff_…`); velocity follows (Y4) to
1 %; the 200 ms release is half-way at 100 ms and silent after (`BasicSynth_release_…`).

---

## DrumMachine — Math

Every pad is a model from `Phasor`, `Noise`, `StateVariableFilter` and `DecayEnvelope`; the
topologies are the analog machines' (TR-808/909). `tr = 2^(tune/12)`; `t` = seconds since the hit;
`E(T)` = a `DecayEnvelope` with T60 = T; `BP_k` = an SVF band-pass multiplied by `1/Q` so its centre
is 0 dB; `sin(f)` / `sq(f)` = a `Phasor` read as a sine / naive square at frequency f.

```
(K0)  pads and defaults (decay = T60, ms):
        kick 36 (450) · rim 37 (45) · snare 38 (220) · clap 39 (300) · low tom 41 (500)
        closed hat 42 (60) · mid tom 45 (420) · open hat 46 (450) · high tom 48 (360) · cowbell 56 (350)

(K1)  kick:   f(t) = 48·tr · 2^(S·e^(−t/0.035)),  S = 1.5 + 3·tone  octaves
              y = sin(f(t))·E(decay) + (0.15 + 0.35·tone) · HP_3k(noise)·E(6 ms)
(K2)  toms:   f(t) = f_end·tr · 2^(S·e^(−t/0.080)),  S = 0.6 + 0.8·tone,  f_end = 80 / 120 / 170 Hz
              y = sin(f(t))·E(decay) + 0.08 · HP_600(noise)·E(25 ms)
(K3)  snare:  d(t) = 1 + 0.4·e^(−t/0.012)
              body = 0.6·sin(185·tr·d) + 0.4·sin(330·tr·d)
              y = (1 − tone)·body·E(0.6·decay) + (0.3 + 0.7·tone) · LP_9k(HP_1.2k·tr(noise))·E(decay)
(K4)  clap:   y = BP(1100·tr·2^(tone−0.5), res 0.35)(noise) · (b(t) + tail(t))
              b = E(7 ms) hit at 0, 10, 20 ms;  tail = 0.9·E(decay) hit at 30 ms
(K5)  hats:   metal = (1/6)·Σ sq(r_i·tr),  r = 205.3, 304.4, 369.6, 522.7, 540, 800 Hz   ; the 808's six
              s = (1 − m)·metal + m·noise,  m = 0.2 + 0.6·tone
              y = HP_7k( BP(10 kHz, res 0.3)(s) ) · E(decay)
(K6)  choke:  a closed-hat hit sends the open hat's envelope to −60 dB in 8 ms (DecayEnvelope::choke)
(K7)  rim:    y = (0.4 + 0.6·tone)·BP(2.2k·tr, res 0.6)(noise)·E(decay) + (1 − 0.5·tone)·sin(480·tr)·E(decay/2)
(K8)  cowbell: s = ½(sq(540·tr) + sq(800·tr))
              y = BP(2640·tr·2^(tone−0.5), res 0.3)(s) · (0.6·E(decay/6) + 0.4·E(decay))
(K9)  output:  out = volume · trim_pad · (vel/127) · 10^(level/20) · y
(K10) pan:     balance, unity at centre:  gL = (pan > 0 ? cos(pan·π/2) : 1),  gR = (pan < 0 ? cos(−pan·π/2) : 1)
(K11) noise seed per pad = 0xD7500000 + pad index; reset() reseeds — a reset replays the same noise
```

**Constants that are voicing, not derivation** (stated, per this repo's rule): the sweep depths and
time constants in (K1)–(K3), the filter corners in (K3)–(K8), and the **trims** of (K9) —
`kick 0.8, rim 0.9, snare 0.9, clap 3.4, toms 0.85, closed hat 2.0, open hat 1.75, cowbell 1.3`.
The trims were set by measurement so the default pads sit at comparable peaks at velocity 127,
volume 0 dB (kick 0.91, toms 0.87–0.89, snare 0.82, rim 0.65, clap 0.65, cowbell 0.64, open hat
0.55, closed hat 0.50). The 808 hat frequencies of (K5) are the documented circuit values.

**Why naive squares in (K5) and (K8).** The metallic timbre IS the inharmonic cluster of six square
waves; band-limiting them buys nothing a listener hears, because their output goes straight into a
10 kHz band-pass and a 7 kHz high-pass where the aliased partials are just more metal.

**Why (K10) is the balance law and not constant power.** It is the suite's mix law (Solaris
R-MIX-11, Interstellar's `AudioMix`): a centred pad plays at unity in both channels.

Verified: the kick settles on 48·2^(tune/12) Hz — 48.1 Hz at tune 0 and 72.0 Hz at +7
(`kick_settles_on_its_tuned_fundamental`, Python), sweeping from more than an octave above; every
pad sounds on its GM note and stops (`DrumMachine_every_pad_…`); the choke drops the open hat by more
than 60 dB (`DrumMachine_closed_hat_chokes_…`); a reset replays a pattern byte-identically.

## Sampler — Math (REQ-inst-sampler-1)

A recorded sound s[k] (N frames, recorded at fr Hz, mono or stereo) played by notes at fs Hz.

```
(S1)  ratio = 2^((note − root)/12) · fr / fs    ; chromatic          (`noteOn`)
      ratio = fr / fs                           ; one-shot: as recorded, whatever the key
(S2)  [a, b) = [⌊start·N⌋, ⌈end·N⌉)             ; the span (start > end is swapped)
      forward:  p₀ = a,     Δp = +ratio
      reverse:  p₀ = b − 1, Δp = −ratio
(S3)  x(p) = (1 − f)·s[k] + f·s[min(k+1, b−1)],  k = ⌊p⌋, f = p − k   ; linear interpolation  (`render`)
(S4)  y = x(p) · env · 10^(level/20) · (1 − σ + σ·vel/127)            ; σ = velocity sensitivity
(S5)  the voice ends when p leaves [a, b − 1] — or, chromatic, when its release has finished
```

`env` is the library's `ADSREnvelope` per voice (attack 0 → 1 from the first sample, so a note at the
root with sustain 1 reproduces the recording exactly). A one-shot ignores its note-off. Sixteen voices:
the same note retriggers, else an idle voice, else the oldest is stolen. The host decodes the file and
hands the frames in (`setSample`, a copy); the library never opens a file.

| parameter | unit | symbol |
|---|---|---|
| `mode` | chromatic / one-shot | which (S1) |
| `root` | key | root |
| `start`, `end` | fraction | (S2) |
| `reverse` | on / off | (S2) |
| `attack`, `decay`, `sustain`, `release` | ms, ms, 0…1, ms | env |
| `level` | dB | level |
| `velocity` | 0…1 | σ |

Why not composed: the library had no resampler; pitching by interpolated playback is (S3). The
envelope IS composed (ADSREnvelope). Verified: at the root the output equals the recording sample for
sample, reversed and spanned likewise; an octave up is 880 Hz and over in half the time; a fifth down is
440·2^(−7/12) and within 2e-3 of the analytic tone (nearest-frame playback misses by 1.4e-2 — a mutant
showed it); a 24 kHz recording keeps its pitch at 48 kHz (`Sampler_plays_its_sound_pitched_spanned_and_reversed`);
through the registry a 44.1 kHz 220 Hz recording plays 109 / 218 / 326 / 436 Hz at −12 / 0 / +7 / +12
(`sampler_pitch`).

