# synth/ — Synth engine, voices, polyphony

Ties the library together into a playable instrument: 8-note polyphony, each note voiced by
3 unison oscillators through an ADSR, then a shared effects chain, then the output block.

> Status: DESIGN. 🆕 = new class.

---

## 🆕 `Voice` — one playing note

```cpp
class Voice {
    UnisonOscillator osc[3];   // generator/ — 3 oscillators, each ≤5 detuned saws, 2ch
                               // each owns its OWN ADSR envelope (part of SignalGenerator)
public:
    void noteOn(int midiNote, double velocity); // sets freqs; osc[i].noteOn(vel) for all 3
    void noteOff();                              // osc[i].noteOff() for all 3
    bool isActive() const;                       // any oscillator's envelope not finished
    double render(int channel);                  // sum of the 3 (already-enveloped) oscs
};
```

- MIDI note → frequency (Hz) → per-oscillator phase increments (sample domain).
- **The envelope is part of each generator** (see [`base/`](../base/README.md) /
  [`envelope/`](../envelope/README.md)), so `Voice` has no separate envelope stage — it
  just triggers `noteOn/noteOff` on its oscillators. Velocity scales each oscillator's
  envelope peak. ADSR params are forwarded to all 3 oscillators (shared per voice).
- `render(channel)` is called once per channel per sample; per-channel state lives in the
  oscillators (and their envelopes), via the channel-aware base.

## 🆕 `VoiceManager` — 8-note polyphony

```cpp
class VoiceManager {
    Voice voices[8];
public:
    void noteOn(int midiNote, double velocity); // allocate a free/oldest voice
    void noteOff(int midiNote);                  // release matching voice(s)
    double render(int channel);                  // sum of active voices' render()
};
```

- Fixed pool of **8** voices (matches the test scenario).
- Allocation policy: free voice first, else steal oldest (voice stealing). Tracked by
  **sample-count age**, not time.
- `noteOff` maps the note back to its voice and triggers release.

## 🆕 `SynthEngine` — top-level

```cpp
class SynthEngine {
    VoiceManager voiceManager;
    Block        effects;     // effects/ chain: comp→drive→chorus→repeater→reverb
    OutputBlock  output;      // output/
public:
    void noteOn(int note, double velocity);
    void noteOff(int note);
    void setParameter(int paramId, double value);  // routed from UART (host/)
    void renderBlock(/* buffer */);                 // fills one AudioConfig::bufferSize block
};
```

Signal flow per sample, per channel:

```
VoiceManager.render(ch) ──▶ effects.out(x, ch) ──▶ OutputBlock.writeSample(ch, y)
```

- `setParameter` is the bridge from the UART protocol to every tunable in the graph
  (oscillator detune/voice count, ADSR, each effect, master). Parameter IDs are shared with
  host + GUI (see [`host/`](../../host/README.md)).
- `renderBlock` renders `AudioConfig::bufferSize()` frames at a time — the unit the ESP32
  audio task and the perf measurement operate on.

## Effects: shared vs per-voice

Effects are applied to the **summed** voice output (one effects chain total), not per voice
— matches the scenario and keeps the ESP32 cost bounded. The ADSR envelope is **inside each
oscillator** (part of `SignalGenerator`), so it is effectively per voice but lives in the
sound source, never in the effects chain.

```
8 voices ─sum─▶ [Compressor→Overdrive→Chorus→Repeater→Reverb] ─▶ OutputBlock ─▶ UART
```

## Reuse map

| Need | Reuse |
|---|---|
| Oscillators / unison | `generator/` |
| Envelope | `envelope/ADSREnvelope` |
| Effects chain composition | `base/Block` + `effects/` |
| Output quantization | `output/OutputBlock` |
| Per-channel rendering | channel-aware `base/SignalProcessor` |

## Open questions for review

1. **Voice stealing:** steal oldest (default) vs. quietest vs. drop new note. Confirm.
2. **Per-voice vs shared effects:** shared (proposed, matches scenario & CPU budget) —
   agree?
3. **Note→frequency:** standard 12-TET, A4=440. Confirm tuning.
