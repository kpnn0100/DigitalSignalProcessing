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

        struct Osc
        {
            Wave wave = Saw;
            int octave = 0;        // −3..+3
            Sample semi = 0.0;     // −12..+12 semitones
            Sample fine = 0.0;     // −100..+100 cents
            Sample level = 0.8;    // 0..1
            int voices = 1;        // unison 1..5
            Sample detune = 15.0;  // unison spread, cents
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
        };

        static constexpr int kVoices = 16;

        BasicSynth();
        ~BasicSynth() override;

        void setParams(const Params &p);
        const Params &params() const { return mParams; }

        void noteOn(int note, int velocity) override;
        void noteOff(int note) override;
        void allNotesOff() override;
        void reset() override;
        void render(Sample *const *out, int channels, int frames) override;
        int activeVoices() const override;

        /** The frequency oscillator `which` (0/1) plays for `note` — the pitch formula, exposed for tests. */
        Sample oscFrequency(int which, int note) const;
        /** The cutoff a voice on `note` uses at filter-envelope value `env` (0..1). */
        Sample cutoffFor(int note, Sample env) const;

    private:
        struct Voice;
        void applyTo(Voice &v);
        std::vector<std::unique_ptr<Voice>> mVoices;
        std::vector<Sample> mScratch;
        Params mParams;
        long mClock = 0; // note-on counter: the oldest voice is stolen first
    };
}
