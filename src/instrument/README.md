# instrument/ — what a host plays

| class | what | requirement |
|---|---|---|
| `Instrument` | the interface: notes in, samples **added** into the host's buffers out | REQ-inst-1 |
| `BasicSynth` | two-oscillator subtractive polysynth, 16 voices — an oscillator may play a wavetable (the registry's "wavetable") | REQ-synth2-1…5, REQ-wavetable-3 |
| `DrumMachine` | ten synthesized pads on the General MIDI drum notes | REQ-drum-1…5 |
| `Piano` | the physical piano (`physical/PianoEngine`) as an instrument, 16 voices | REQ-inst-piano-1 |
| `FMSynth` | four sine operators by phase modulation — eight algorithms, a feedback operator, an ADSR each; 16 voices | REQ-fm-1…3 |
| `Bass808` | a tuned bass drum — a sine's pitch punched down to the note, gliding between notes, a click, drive and tone; mono | REQ-808-1…2 |
| `GranularSampler` | a recorded sound as a cloud of short grains — size, density, position (and its scan), spray of place, pitch and pan; 16 voices | REQ-granular-1…2 |
| `Sfz` · `MultiSampler` | a multi-sample instrument read from SFZ text: regions by key, velocity, round robin and chance; loops, one-shots, choke groups; 32 voices | REQ-multisample-1…3 |

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
(Y4)  G          = 10^(volumeDb/20) · (1 − velocity + velocity·vel/127)   ; the velocity term at note-on, the volume per
                   ; block: g[i] = g_prev + (g − g_prev)·(i+1)/N across a block of N (D-8 — a live volume moves a held note)
(Y5)  allocation: the same note retriggers its voice; else the first idle voice; else the voice
                  with the oldest note-on (smallest age) is stolen
(Y6)  noise seed  = 0x5EED0000 + 16·voice + channel                  ; one generator per voice PER CHANNEL
(Y7)  control grid: every K = 32 samples COUNTED FROM `reset` (mPos), each sounding voice's pitch P is re-set on its
      oscillators — only if it moved (`retune`); render runs in pieces on that grid, so a host's chopping changes nothing
(Y8)  P = to − (to − from)·left/total                                ; a glide: linear in SEMITONES, constant time —
      total = glide·fs/1000 samples, `left` counted down by the samples rendered; no glide: P = the note
(Y9)  P_heard = P + bend·bendRange + mod·vibrato·sin(2π·vibratoRate·mPos/fs)   ; the wheel and the vibrato (`modulation`)
(Y10) mono / legato: ONE voice and a stack of the keys held, the last on top (16 deep). A key pressed: on top; the
      voice slides there (Y8) — mono strikes again (the envelopes from their attack), legato does not while a key is
      held. A key let go that was sounding: back to the key now on top, sliding, never striking; none left: release.
```

| parameter | symbol | unit / range | default |
|---|---|---|---|
| `osc1/2.wave` | — | sine · saw · square · triangle | saw / square |
| `osc1/2.octave`, `.semi`, `.fine` | (Y2) | −3..3 oct, −12..12 st, −100..100 ct | 0 / −1 oct, +7 ct |
| `osc1/2.level` | L1, L2 | 0..1 | 0.8 / 0.5 |
| `osc1/2.voices`, `.detune` | unison | 1..5, cents (Oscillator's own) | 1, 15 |
| `osc1/2.width` | stereo width w | 0..50 cents: channel c detuned by (c − (C−1)/2)·w (Oscillator's `spreadID`); 0 = mono-identical — REQ-synth2-6 | 0 |
| `noise` | N | 0..1 | 0 |
| `filterMode` | — | LP · BP · HP · notch | LP |
| `cutoff` | (Y3) | Hz | 2400 |
| `resonance` | res | 0..1 → Q 0.707..32 (S5) | 0.25 |
| `envAmount` | (Y3) | octaves at full envelope, −8..8 | 2 |
| `keytrack` | (Y3) | 0..1 (1 = the cutoff follows the note exactly) | 0.5 |
| `filterEnv`, `ampEnv` | fenv, amp | ADSR: ms, ms, 0..1, ms | 3/350/0.25/300 · 3/120/0.8/250 |
| `volumeDb` | (Y4) | dB | −12 |
| `velocity` | (Y4) | 0..1 sensitivity | 0.8 |
| `voice.mode` | (Y10) | poly · mono · legato | poly |
| `voice.glide` | (Y8) | 0..2000 ms (mono / legato only) | 0 |
| `voice.bend` | bendRange (Y9) | 0..24 st at full wheel | 2 |
| `vibrato.depth`, `vibrato.rate` | (Y9) | 0..2 st at full mod wheel · 0.1..20 Hz | 0.5 · 5.5 |
| `pitchBend(a)`, `modWheel(m)` | bend, mod | −1..+1 · 0..1 (REQ-inst-expr-1) | 0 · 0 |

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

**Why an absolute control grid (Y7).** A pitch that moves inside a block (a glide, a vibrato) must be set somewhere
in it; setting it at the start of every `render` would make the output depend on how a host chops time — the rule
the host's own engine is held to (byte-identical however time is chopped). Counting the grid from `reset` makes the
points the same samples whatever the blocks; a still pitch is never re-set, so a patch with no glide, no bend and no
vibrato renders exactly as before. 32 samples is 0.67 ms at 48 kHz: a 5.5 Hz vibrato is 260 steps a cycle; the
glide's midpoint lags its line by at most one step (0.08 st for an octave in 100 ms). **Why glide is linear in
semitones (Y8).** Pitch is heard logarithmically — a constant-time slide even in semitones sounds even; its
midpoint is the two notes' geometric mean in Hz. **Piano and kit** take no bend or wheel (an acoustic piano's
strings are tuned, a pad is a hit): `Instrument::pitchBend` defaults to ignoring it.

**Levels (empirical).** One voice at `volumeDb = 0`, velocity 127, default patch, C4 peaks at 1.41
(two oscillators summing); the default −12 dB puts a note at ≈ 0.35, leaving headroom for chords.

Verified: equal temperament to 0.026 cents (`synth2_equal_temperament`, Python recomputes (Y1));
the filter removes > 25 dB of the energy above 3 kHz at a 300 Hz cutoff, and its envelope makes the
attack > 15 dB brighter than the sustain (`BasicSynth_filter_cutoff_…`); velocity follows (Y4) to
1 %; the 200 ms release is half-way at 100 ms and silent after (`BasicSynth_release_…`).

---

**A table in an oscillator's place (REQ-wavetable-3).** `Osc::table` ≥ 0 sets the oscillator to `Table` on
`Wavetable::builtIn(table)` (or the sound given by `setWavetable` — `kSampleTable`); its position is
`clamp(position + posEnv · e)`, `e` the filter envelope's value — set at the 32-sample grid (Y7) and ramped across it
(generator/README (W7)); a new note's table starts at `position` (e = 0) at once. The built-ins are made in the
constructor, never on an audio thread.

## DrumMachine — Math

Every pad is a model from `Phasor`, `Noise`, `StateVariableFilter` and `DecayEnvelope`; the
topologies are the analog machines' (TR-808/909). `tr = 2^(tune/12)`; `t` = seconds since the hit;
`E(T)` = a `DecayEnvelope` with T60 = T; `BP_k` = an SVF band-pass multiplied by `1/Q` so its centre
is 0 dB; `sin(f)` / `sq(f)` = a `Phasor` read as a sine / naive square at frequency f.

```
(K0)  pads and defaults (decay = T60, ms):
        kick 36 (450) · rim 37 (45) · snare 38 (220) · clap 39 (300) · low tom 41 (500)
        closed hat 42 (60) · mid tom 45 (420) · open hat 46 (450) · high tom 48 (360) · cowbell 56 (350)
        crash 49 (1800) · ride 51 (1200)                                         ; REQ-drum-6, last in the pad order

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
(K12) crash:  s = (1 − m)·metal + m·noise,  m = 0.45 + 0.4·tone            ; the hats' six squares, noisier
              y = (0.6·BP(3.5k·tr·2^(tone−0.5), res 0.2)(s) + 0.8·HP_5k·tr(s)) · (0.3·E(decay/8) + 0.7·E(decay))
(K13) ride:   bell = BP(3k·tr·2^(tone−0.5), res 0.45)(½(sq(598·tr) + sq(1002·tr))) · E(decay/3)
              wash = HP_8k·tr(½·(¼Σ_{i=2..5} sq(r_i·tr)) + ½·noise) · E(decay)
              y = (0.45 − 0.2·tone)·bell + (0.55 + 0.2·tone)·wash
(K9)  output:  out = volume · trim_pad · (vel/127) · 10^(level/20) · y
(K10) pan:     balance, unity at centre:  gL = (pan > 0 ? cos(pan·π/2) : 1),  gR = (pan < 0 ? cos(−pan·π/2) : 1)
(K11) noise seed per pad = 0xD7500000 + pad index; reset() reseeds — a reset replays the same noise
```

**Constants that are voicing, not derivation** (stated, per this repo's rule): the sweep depths and
time constants in (K1)–(K3), the filter corners in (K3)–(K8), and the **trims** of (K9) —
`kick 0.8, rim 0.9, snare 0.9, clap 3.4, toms 0.85, closed hat 2.0, open hat 1.75, cowbell 1.3, crash 0.93,
ride 1.1` (crash and ride measured at 0.55 peaks, beside the open hat). The ride's bell squares, 598 and 1002 Hz,
are chosen so their 5th and 3rd harmonics (a square has odd ones only) meet in its 3 kHz band — at 511/762 Hz,
the first try, none landed there and the bell carried 2 % of the sound.
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
(S4)  y = x(p) · env · 10^(level/20) · (1 − σ + σ·vel/127)            ; σ = velocity sensitivity   ; the level applied per block, ramped as (Y4)'s volume (D-8)
(S5)  the voice ends when p leaves [a, b − 1] — or, chromatic, when its release has finished
(S6)  p += Δp · 2^(bend·bendRange/12)            ; the pitch wheel moves every voice's rate (REQ-inst-expr-1)
(S7)  slices (REQ-inst-sampler-2):  k = note − root;  silent unless 0 ≤ k < n   (n = `slices`)
      [a_k, b_k) = [a + ⌊(b − a)·k / n⌋,  a + ⌊(b − a)·(k + 1) / n⌋)    ; slice k of the span, the voice's own span
      ratio = fr / fs                           ; as recorded, to the slice's end (a one-shot: the note-off ignored)
```

`env` is the library's `ADSREnvelope` per voice (attack 0 → 1 from the first sample, so a note at the
root with sustain 1 reproduces the recording exactly). A one-shot ignores its note-off. Sixteen voices:
the same note retriggers, else an idle voice, else the oldest is stolen. The host decodes the file and
hands the frames in (`setSample`, a copy); the library never opens a file.

| parameter | unit | symbol |
|---|---|---|
| `bend` | semitones at full wheel (0..24, default 2) | bendRange (S6) |
| `mode` | chromatic / one-shot / slices | which (S1); slices (S7) |
| `slices` | parts of the span (1..64, default 8) | n (S7) |
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
(`sampler_pitch`). Slices (S7): a recording of eight tones in eighths plays tone k at root + k, each exactly its
eighth and over at its end, a key outside them silent, a note-off ignored (`Sampler_slices_play_by_the_key`);
through the registry the keys play their slices' pitches (`sampler_slices`). Not composed: it is (S2)'s span cut in
n, the same voice code with a span of its own.

## Piano — Math (REQ-inst-piano-1)

**Why composition, not new math.** The sound is entirely `physical/` — `PianoVoice` (hammer, stiff
inharmonic strings, two polarisations, longitudinal and duplex banks) on one shared `PianoBridge`, pooled
and pedalled by `PianoEngine`; every equation is in `src/physical/README.md`. `Piano` adds only the three
mappings a host needs (`src/instrument/Piano.cpp`):

```
(P1) v_hammer = clamp(v_midi, 1, 127) / 127               noteOn → PianoEngine::noteOnMidi
(P2) out_c += g · limiter_c,   g = 10^(volume_dB / 20)     render → PianoEngine::renderAdd(…, g)
     limiter_c = tanh(drive · Σ voices_c + radiated)       (the engine's soft limiter; Drive = its Tune row)
(P3) allNotesOff = every key up (the pedals stay down);  reset = PianoEngine::silence()
```

| registry parameter | symbol | unit | maps to |
|---|---|---|---|
| `hammer.hardness` `hammer.curve` `hammer.hysteresis` `hammer.relax` | | x, —, —, x | the engine's Tune rows Hardness, FeltCurve, FeltHysteresis, FeltRelax |
| `strings.decay` `.brightness` `.inharmonicity` `.detune` `.growl` `.glide` `.shimmer` `.sing` | | x, x, x, cents, —, x, —, x | Decay, Brightness, Inharmonicity, UnisonDetune, BassGrowl, AttackGlide, TrebleShimmer, Aftersound |
| `body.mix` `body.resonance` | | — | Body, BodyResonance |
| `pedal.sustain` `pedal.soft` | | off/on | `setSustainPedal`, `setUnaCorda` |
| `drive` | drive | — | MasterGain (the limiter's input gain) |
| `volume` | volume_dB | dB | (P2) |

Ranges and defaults are `PianoEngine::tuneSpec`'s — the registry reads them, never restates them.

**Voices.** 16 (a silent voice is skipped, `physical/README.md` ## 13, so the pool's size costs nothing
until notes sound). A voice is taken in this order: never struck; finished; the longest-RELEASED (damped,
then held only by a pedal); only then the oldest held key — so a held bass under an arpeggio is never
stolen while a released note rings (`PianoEngine::allocateVoice`).

**Silence and determinism.** `PianoEngine::silence()` puts every voice back as made — each string's
history zeroed and its damper SEATED at once (`StringPartialBank::silence`), the hammer's last contact
reading (`displacementAtStrike`) zeroed, the envelope Idle, the noise seed and the noise filters reset,
the bridge's modes cleared — and the pool's bookkeeping too (no voice "used"). The constructor ends with
it: a never-struck voice is a damped string, not an undamped one at the constructor's pitch ringing
through the bridge. So the same notes from a `reset()` are the same samples as from a new piano
(`Piano_reset_is_silent_at_once_and_starts_again_the_same`).


## FMSynth — Math (REQ-fm-1…3)

Four operators, each a sine whose PHASE the operators above it modulate (as Yamaha's four-operator instruments: the
TX81Z's eight algorithms). **Why not composed (rule 1):** BasicSynth's voice is an oscillator into a filter; an FM
voice is four sines each with its own envelope, read in an order the algorithm sets — a different voice, not a
different oscillator. What it reuses: `ADSREnvelope` (one an operator), BasicSynth's control grid for pitch (Y7) so a
render is the same however a host chops time, its pitch formula and its bend and vibrato.

```
(M1) An operator k of a voice on note p — FMSynth::render
     f_k = 440·2^((p + b − 69)/12) · r_k · 2^(c_k/1200)          r_k ratio (0.5 … 16), c_k fine (cents), b bend+vibrato
     φ_k[n+1] = φ_k[n] + f_k/fs  (wrapped to [0,1)),  φ_k = 0 at the note's start (every operator in step: key sync)
     y_k[n] = sin(2π·φ_k[n] + β_k[n]) · g_k[n]
     g_k = L_k · e_k[n] · (1 − s_k + s_k·v)                      L_k level 0…1, e_k its ADSR, s_k velocity sense, v vel
(M2) What an operator hears — the algorithm A:  β_k = I · Σ_{j ∈ A(k)} y_j   (j > k: read first, ops 4 → 1)
     I = 8 rad (a modulator at level 1 swings its carrier ±8 radians)
     feedback on operator 4:  β_4 = F·π·(y_4[n−1] + y_4[n−2])/2   (the mean of two: a DX's, it does not oscillate)
(M3) The algorithms (→ modulates; carriers are heard):
     1: 4→3→2→1          carriers 1          5: 4→3, 2→1        carriers 1, 3
     2: (3, 4)→2→1        carriers 1          6: 4→(3, 2, 1)     carriers 1, 2, 3
     3: 3→2→1, 4→1        carriers 1          7: 4→3, 2, 1       carriers 1, 2, 3
     4: 4→3→1, 2→1        carriers 1          8: 1, 2, 3, 4      carriers all four
(M4) Out:  y = vol · Σ_{carriers} y_k / C   (C the carriers' count — an algorithm does not change the loudness)
     the same on every channel (FM is mono here)
(M5) Two operators, a check on (M1)–(M2): a carrier at r·f0 under a modulator at f0 of index I·L
     sin(2π r f0 t + I·L·sin 2π f0 t) = Σ_n J_n(I·L) sin(2π (r + n) f0 t)  — sidebands at the Bessel functions
(M6) The sine — a table of 4096 points read linearly (its error ≈ (2π/4096)²/8 ≈ 3·10⁻⁷, −130 dB); its phase from
     the operator's accumulator, never a clock
```

| parameter | symbol | unit / range | default |
|---|---|---|---|
| `algorithm` | A | 1 … 8 | 1 |
| `feedback` | F | 0…1 (π·F radians at most) | 0 |
| `op1…4.ratio` | r_k | 0.5 … 16 | 1, 1, 2, 3.5 |
| `op1…4.fine` | c_k | −100 … 100 cents | 0 |
| `op1…4.level` | L_k | 0…1 | 1, 0.6, 0.3, 0.2 |
| `op1…4.attack/decay/sustain/release` | e_k | ms, ms, 0…1, ms | (op 1) 2/400/0.6/300, (modulators) 2/600/0.3/300 |
| `op1…4.velocity` | s_k | 0…1 | 0.7 |
| `volume` | vol | dB | −12 |
| `bend`, `vibrato`, `vibrato.rate` | b | st, st, Hz | 2, 0.5, 5.5 |

**Limits, said:** a modulator's sidebands past Nyquist fold back, as a DX7's do — at A4 with an index under 3 they
lie under −60 dB (measured: FM_sidebands_are_bessel); high notes at a high index alias audibly. No filter, no glide,
no mono mode (BasicSynth has them); FM is mono.

## MultiSampler and SFZ — Math (REQ-multisample-1…3)

A recorded instrument: many sounds, each a REGION of the keyboard and of velocity, read from an SFZ file (the open
text format samplers share). **Why not composed (rule 1):** `Sampler` plays one sound over the whole keyboard; this
plays whichever regions a note and its velocity fall in — several at once, in turn (round robin), by chance, choking
each other — each its own sound, root, tuning, loop and envelope. What it reuses: `ADSREnvelope` (one a voice), the
suite's balance pan (unity at centre), the Catmull-Rom read of `Wavetable` (W3). The library never opens a file: the
HOST reads the SFZ's text and decodes the sounds it names (`sfzSounds`), and hands both in.

```
(Z1) The text — parseSfz:  headers <control> <global> <group> <region>; opcodes `name=value` (a value runs to the next
     `name=` or header — so a sample path may hold spaces); `//` to the line's end and `/* … */` are comments.
     A region's opcode is its own, else its group's, else the global's; <control> default_path= prefixes `sample=`.
     Keys: a number, or a name c-1 … g9 (c4 = 60), sharps `#`, flats `b`;  `key=` sets lokey = hikey = pitch_keycenter.
(Z2) Which regions sound — MultiSampler::noteOn(n, v):  every region with lokey ≤ n ≤ hikey, lovel ≤ v ≤ hivel,
     lorand ≤ r < hirand (r a deterministic draw, one a note-on), and its turn:
     each region counts the notes that reached it, c ← c + 1;  it plays when (c − 1) mod seq_length = seq_position − 1
(Z3) Its pitch — the step a frame:  s = 2^((n − keycenter + transpose)/12 + tune/1200 + bend/12) · rate_sound / rate_out
(Z4) Its read — Catmull-Rom at the position x (as Wavetable (W3)), x ← x + s; from `offset`;
     loop_continuous: past loop_end it goes back by (loop_end − loop_start + 1); loop_sustain: so while held, then on;
     no_loop / one_shot: it ends at `end` (one_shot ignores the note-off)
(Z5) Its level:  g = 10^(volume/20) · (1 − t + t·(v/127)²) · e(t),  t = amp_veltrack (the registry's `velocity`, 0…1),
     e the region's ADSR (ampeg_attack, _decay in s; _sustain in %; _release in s);  L = g·min(1, 1 − pan/100),
     R = g·min(1, 1 + pan/100)  (the suite's balance: unity at centre)
(Z6) Choke — group / off_by:  a region of group G starting releases (fast, 6 ms) every voice whose region has off_by = G
```

| opcode / parameter | symbol | unit / range | default |
|---|---|---|---|
| `lokey` `hikey` `key` `pitch_keycenter` | — | 0 … 127 or a name | 0, 127, —, 60 |
| `lovel` `hivel` | — | 1 … 127 | 1, 127 |
| `lorand` `hirand` | — | 0 … 1 | 0, 1 |
| `seq_length` `seq_position` | — | 1 … 100 | 1, 1 |
| `tune` `transpose` | — | cents, semitones | 0, 0 |
| `volume` `pan` | — | dB, −100 … 100 | 0, 0 |
| `offset` `end` | — | frames | 0, the sound's last |
| `loop_mode` `loop_start` `loop_end` | — | no_loop · one_shot · loop_continuous · loop_sustain; frames | no_loop |
| `ampeg_attack` `_decay` `_sustain` `_release` | e | s, s, %, s | 0, 0, 100, 0.03 |
| `group` `off_by` | — | integers | 0, 0 |
| registry `volume` | — | dB | 0 |
| registry `velocity` | t | 0 … 1 (SFZ's amp_veltrack, as a fraction) | 1 |
| registry `voice.bend` | bend | semitones at full | 2 |

**Limits, said:** an unknown opcode is kept as a problem (said), not an error; trigger=release, crossfades
(xfin/xfout), filters and LFOs are not read yet; a sound read faster than its rate (pitched up) is not band-limited
— Catmull-Rom's images fall where the sound has little (a multi-sample's regions are a few keys wide).

## GranularSampler — Math (REQ-granular-1…2)

A recorded sound played as a CLOUD: many short windowed slices (grains), each read from a place in the sound at the
note's pitch, overlapping. **Why not composed (rule 1):** `Sampler` reads one sound start to end; a grain cloud reads
many short spans at once, each started on a schedule and windowed. What it reuses: `ADSREnvelope` (a voice's), the
Catmull-Rom read (Wavetable (W3)), the suite's balance pan, and a counter-based draw (MultiSampler (Z2)) for spray —
never a clock.

```
(G1) Schedule — GranularSampler::render: a voice starts a grain every D = fs/density samples, counted from its note
     (sample k₀ = 0, D, 2D, …) — the same however a host chops time; at most kGrains (48) at once, the oldest dropped
(G2) A grain j of a voice on note n, from its sound of L frames at rate r:
     place  p_j = clamp(position + scan·t_j + spray·u_j, 0, 1) · L       t_j its start, s;  u_j ∈ [−1, 1] a draw
     step   s_j = 2^((n − root + pitch + pspray·u'_j/100)/12) · r / fs   pspray in cents
     pan    q_j = spread · u''_j                                          (the suite's balance: L·min(1, 1 − q), R·min(1, 1 + q))
     length G = size·fs/1000 samples
(G3) Its sound at its k-th sample (k < G):  x(p_j + k·s_j) · w(k),  w(k) = ½(1 − cos(2πk/G))  (Hann: no click at either end)
     x read by Catmull-Rom; past the sound's ends, silence
(G4) The cloud:  y = Σ_j grain_j · g,  g = 1/max(1, ½·density·size/1000)   — the overlap's mean, so a cloud of a
     steady sound plays near its level however dense (Hann's mean is ½)
(G5) The voice:  y · e(t) · vol · (1 − v_s + v_s·vel),  e an ADSR
```

| parameter | symbol | unit / range | default |
|---|---|---|---|
| `size` | — | ms, 5 … 500 | 80 |
| `density` | — | grains a second, 1 … 200 | 30 |
| `position` | — | 0 … 1 of the sound | 0.3 |
| `scan` | — | sounds a second moved through (−1 … 1) | 0 |
| `spray` | — | 0 … 1 of the sound | 0.05 |
| `pitch`, `pspray` | — | semitones −24 … 24, cents 0 … 100 | 0, 5 |
| `spread` | — | 0 … 1 | 0.5 |
| `root` | — | the key that plays the sound as recorded | 60 |
| `attack/decay/sustain/release` | e | ms, ms, 0…1, ms | 50/0/1/300 |
| `volume`, `velocity` | vol, v_s | dB, 0…1 | −6, 0.5 |

**Limits, said:** a grain read faster than its sound's rate is not band-limited; a grain's draws come from a counter
per voice — the same note on the same sample sprays the same way every render.

## Bass808 — Math (REQ-808-1…2)

The 808's bass drum played as an instrument: its pitch the NOTE (a bassline), a short downward punch at each attack,
a long decay, sliding between held notes, saturated. **A composition (rule 1):** `Phasor` (the sine's phase),
`DecayEnvelope` (the damped decay and its choke), `Noise` (the click), `Biquad::design` (the tone's low-pass, run as a
transposed-direct-form-II section here — the meters' way); the glide is BasicSynth's (Y8), linear in semitones over a
constant time. What is its own: the punch, the drive's normalisation, and the mono voice's legato.

```
(B1) Pitch — Bass808::render, t samples since the attack:
     p(t) = g(t) + P · exp(−t/τ),  τ = T_p·fs/4.6  (the punch at 1 % of its depth after T_p)
     g(t) the glide: from the last pitch to the note, linear in semitones over T_g (0: at once)
     f(t) = 440·2^((p(t) + b − 69)/12)   (b the bend);  φ ← φ + f/fs  (Phasor, from 0 at every attack);  x = sin 2πφ
(B2) Amplitude — DecayEnvelope: attack A ms, decay D ms to −60 dB (its (D1)–(D2)); a note-off chokes it over R ms;
     the voice ends at the very sample the envelope does (−80 dB, its (D3)), the tone's memory cleared
(B3) Click — c(t) = C · n(t) · exp(−t/(0.0015·fs)),  n the library's Noise (a fixed seed: the same click every hit)
(B4) Drive — y = tanh(k·(x·e + c)) / tanh(k),  k = 0.01 + 20·dr  (dr = 0: y = x·e + c to 0.003 %; 1: near a square)
(B5) Tone — a low-pass at F Hz, Q 0.707 (Biquad::design), then the level: vol · (1 − v_s + v_s·vel)
(B6) Mono — one voice: a note while another is held glides there (legato: no new attack, no new punch); a note with
     none held is an attack; releasing the top key goes back to the one under it (gliding)
```

| parameter | symbol | unit / range | default |
|---|---|---|---|
| `punch`, `punch.time` | P, T_p | semitones 0 … 36, ms 1 … 200 | 12, 40 |
| `glide` | T_g | ms 0 … 1000 | 60 |
| `amp.attack`, `amp.decay`, `amp.release` | A, D, R | ms | 1, 900, 60 |
| `click` | C | 0 … 1 | 0.2 |
| `drive` | dr | 0 … 1 | 0.25 |
| `tone` | F | Hz 200 … 20000 | 4000 |
| `volume`, `velocity`, `voice.bend` | vol, v_s, b | dB, 0 … 1, semitones | −6, 0.4, 2 |

**Limits, said:** mono (an 808 bassline is); the drive is not oversampled — at high drive its harmonics fold above
about 4 kHz (the tone's low-pass is there for them).
