/*
 *  Arstro DSP Library — Piano: the physical piano as an Instrument (REQ-inst-piano-1).
 *
 *  `PianoEngine` (src/physical) — the PianoVoice pool on one shared PianoBridge, the three pedals,
 *  the live voicing bank — behind the host's interface (REQ-inst-1). Composition, not new physics:
 *  every equation is the engine's and its voice's (src/physical/README.md); this adds only the
 *  mapping a host needs, (P1)–(P3) in this folder's README:
 *    (P1) velocity: the hammer's 0 … 1 is MIDI's v / 127;
 *    (P2) volume: a gain AFTER the engine's soft limiter, g = 10^(dB/20) — the limiter's own drive
 *         stays the engine's Drive (its Tune row), so the volume never changes the tone;
 *    (P3) a host's stop is every key up (the pedals stay, tails ring); its seek is silence now.
 *  Deterministic: the same notes at the same samples from a reset give the same output.
 */
#pragma once
#include "Instrument.h"
#include "../physical/PianoEngine.h"
#include <array>

namespace arstro
{
    class Piano : public Instrument
    {
    public:
        struct Params
        {
            std::array<double, PianoEngine::TuneCount> tune{}; // the engine's Tune rows, in its units
            bool sustain = false;   // the sustain (damper) pedal
            bool soft = false;      // the soft pedal (una corda)
            double volumeDb = 0.0;  // (P2)
            Params()
            {
                for (int i = 0; i < PianoEngine::TuneCount; ++i) tune[(size_t)i] = PianoEngine::tuneSpec(i).def;
            }
        };

        Piano();
        /** Apply what changed (a Tune row re-voices the sounding notes, as the engine does). */
        void setParams(const Params &p);
        const Params &params() const { return mP; }

        void noteOn(int note, int velocity) override;
        void noteOff(int note) override;
        void allNotesOff() override;
        void reset() override;
        void render(Sample *const *out, int channels, int frames) override;
        int activeVoices() const override;

    private:
        PianoEngine mEngine;
        Params mP;
        double mGain = 1.0;
    };
}
