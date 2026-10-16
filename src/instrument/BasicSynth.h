/*
 *  Arstro DSP Library — BasicSynth: a two-oscillator subtractive polysynth (REQ-synth2-1…5).
 *
 *  Per voice, the classic order:
 *
 *      Osc 1 ┐
 *      Osc 2 ┼─ sum ─▶ StateVariableFilter ─▶ × amp ADSR × velocity × volume ─▶ out
 *      Noise ┘              ▲ cutoff · 2^(env·filterADSR + keytrack·(note−60)/12)
 *
 *  A COMPOSITION of existing modules (arstro.dsp.implement rule 1): `Oscillator` (band-limited,
 *  unison), `StateVariableFilter` (per-sample `tick` for the envelope sweep), two `ADSREnvelope`s,
 *  `Noise`. The only new logic is the wiring, the pitch/cutoff formulas, and voice allocation.
 *
 *  One subtlety the composition forces: `Oscillator` owns an ADSR (the library's voicing model). A
 *  synth whose amplitude envelope comes AFTER its filter must not let that inner envelope shape the
 *  sound, so each oscillator's own envelope is a gate — attack 0, decay 0, sustain 1, release 0 —
 *  opened at note-on and closed only when the amp envelope has finished, so the release tail is
 *  never cut. Math and constants: instrument/README.md ## BasicSynth.
 *
 *  Expression (REQ-synth2-7, REQ-synth2-8): pitch bend (± `bendRange` semitones), the mod wheel (vibrato, `vibrato`
 *  semitones deep at full wheel, `vibratoRate` Hz), and a mono / legato mode with constant-time glide. Every pitch
 *  that moves within a block is re-set on an ABSOLUTE grid of kControl samples counted from `reset` — so a render is
 *  the same however a host chops time (README (Y7)–(Y10)).
 */
#pragma once
#include "Instrument.h"
#include "../generator/Oscillator.h"
#include "../generator/Noise.h"
#include "../envelope/ADSREnvelope.h"
#include "../equalizer/StateVariableFilter.h"
#include <memory>
#include <vector>

namespace arstro
{
    class BasicSynth : public Instrument
    {
    public:
        enum Wave { Sine = 0, Saw, Square, Triangle };
        enum Mode { Poly = 0, Mono, Legato };      // REQ-synth2-8

        struct Osc
        {
            Wave wave = Saw;
            int octave = 0;        // −3..+3
            Sample semi = 0.0;     // −12..+12 semitones
            Sample fine = 0.0;     // −100..+100 cents
            Sample level = 0.8;    // 0..1
            int voices = 1;        // unison 1..5
            Sample detune = 15.0;  // unison spread, cents
            Sample width = 0.0;    // REQ-synth2-6: stereo width, cents (each channel detuned apart by it)
        };
        struct Env
        {
            Sample attack, decay, sustain, release; // ms, ms, 0..1, ms
        };
        struct Params
        {
            Osc osc1{Saw, 0, 0.0, 0.0, 0.8, 1, 15.0};
            Osc osc2{Square, -1, 0.0, 7.0, 0.5, 1, 15.0};
            Sample noise = 0.0;                                    // 0..1
            StateVariableFilter::Mode filterMode = StateVariableFilter::LowPass;
            Sample cutoff = 2400.0;                                // Hz
            Sample resonance = 0.25;                               // 0..1
            Sample envAmount = 2.0;                                // octaves the filter ADSR opens it by (−8..+8)
            Sample keytrack = 0.5;                                 // 0..1: cutoff follows the note
            Env filterEnv{3.0, 350.0, 0.25, 300.0};
            Env ampEnv{3.0, 120.0, 0.8, 250.0};
            Sample volumeDb = -12.0;                               // −60..+6
            Sample velocity = 0.8;                                 // 0..1: how much velocity scales level
            // REQ-synth2-7, -8: expression and glide (appended: a parameter's index is its id in a plugin host)
            Mode mode = Poly;
            Sample glide = 0.0;                                    // ms: a mono / legato note's slide from the last
            Sample bendRange = 2.0;                                // semitones at full bend
            Sample vibrato = 0.5;                                  // semitones of vibrato at full mod wheel
            Sample vibratoRate = 5.5;                              // Hz
        };

        static constexpr int kVoices = 16;
        static constexpr int kControl = 32;   // (Y7) samples between the pitch's updates, from `reset`

        BasicSynth();
        ~BasicSynth() override;

        void setParams(const Params &p);
        const Params &params() const { return mParams; }

        void noteOn(int note, int velocity) override;
        void noteOff(int note) override;
        void allNotesOff() override;
        void pitchBend(Sample amount) override;    // −1 … +1: ± bendRange semitones (REQ-synth2-7)
        void modWheel(Sample amount) override;     // 0 … 1: the vibrato's depth
        /** The pitch voice `v` plays now (semitones, the note plus glide, bend and vibrato) — for tests; −1 silent. */
        Sample voicePitch(int v) const;
        void reset() override;
        void render(Sample *const *out, int channels, int frames) override;
        int activeVoices() const override;

        /** The frequency oscillator `which` (0/1) plays for `pitch` (semitones) — the pitch formula, for tests. */
        Sample oscFrequency(int which, Sample pitch) const;
        /** The cutoff a voice on `note` uses at filter-envelope value `env` (0..1). */
        Sample cutoffFor(int note, Sample env) const;

    private:
        struct Voice;
        void applyTo(Voice &v);
        void retune(Voice &v);                      // (Y7) its pitch now — set on the oscillators when it moved
        Sample modulation() const;                  // (Y9) bend and vibrato, semitones, at mPos
        void monoOn(int note, int velocity);        // (Y10)
        void monoOff(int note);
        std::vector<std::unique_ptr<Voice>> mVoices;
        std::vector<Sample> mScratch;
        std::vector<Sample> mRamp;     // REQ-synth2-4 amended: the volume across a block, ramped
        Sample mVolGain = -1.0;        // the volume the last block ended at (−1: none yet — the next block starts there)
        Params mParams;
        long mClock = 0; // note-on counter: the oldest voice is stolen first
        long long mPos = 0;              // samples rendered since `reset`: the control grid and the vibrato's phase
        Sample mBend = 0.0, mMod = 0.0;  // −1…+1, 0…1
        int mStack[16] = {0};            // (Y10) the keys held in a mono mode, the last on top
        int mStackSize = 0;
    };
}
