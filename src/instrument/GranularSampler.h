/*
 *  Arstro DSP Library — GranularSampler: a recorded sound as a cloud of short grains (REQ-granular-1, -2).
 *
 *  Each voice starts a grain on a fixed schedule from its note (so a render is the same however a host chops time),
 *  each read from a place in the sound — moved by a scan and sprayed — at the note's pitch, Hann-windowed, panned;
 *  the cloud scaled by its overlap; an ADSR on the voice. instrument/README.md ## GranularSampler (G1)–(G5).
 *
 *  The library never opens a file: a host hands the sound in (`setSample`, copied — not on an audio thread). Spray
 *  draws come from a counter, never a clock. 16 voices; no allocation once the sound is given.
 */
#pragma once
#include "Instrument.h"
#include "../envelope/ADSREnvelope.h"
#include <memory>
#include <vector>

namespace arstro
{
    class GranularSampler : public Instrument
    {
    public:
        static constexpr int kVoices = 16;
        static constexpr int kGrains = 48; // (G1) a voice's grains at once at most
        struct Params
        {
            double size = 80.0;       // ms
            double density = 30.0;    // grains a second
            double position = 0.3;    // 0 … 1 of the sound
            double scan = 0.0;        // sounds a second the position moves through
            double spray = 0.05;      // 0 … 1 of the sound
            double pitch = 0.0;       // semitones
            double pspray = 5.0;      // cents
            double spread = 0.5;      // 0 … 1
            int root = 60;
            double attack = 50.0, decay = 0.0, sustain = 1.0, release = 300.0; // ms, ms, 0 … 1, ms
            double volumeDb = -6.0;
            double velocity = 0.5;    // sensitivity
        };
        GranularSampler();
        void setParams(const Params &p);
        const Params &params() const { return mP; }
        /** The sound: `frames` frames of `channels` interleaved floats at `rate` Hz — copied; mono plays on both sides. */
        void setSample(const float *interleaved, long long frames, int channels, double rate);
        /** Grains started by voice `v` since its note (for tests); −1 silent. */
        long long grainsStarted(int v) const;

        void noteOn(int note, int velocity) override;
        void noteOff(int note) override;
        void allNotesOff() override;
        void reset() override;
        void render(Sample *const *out, int channels, int frames) override;
        int activeVoices() const override;
        void pitchBend(Sample amount) override;

    private:
        struct Grain
        {
            bool on = false;
            double pos = 0.0, step = 1.0, gl = 1.0, gr = 1.0;
            long long k = 0, length = 0;
        };
        struct Voice
        {
            bool active = false, held = false;
            int note = -1;
            double vel = 1.0;
            long long age = 0, clock = 0, started = 0; // samples since its note; grains started
            unsigned long long draw = 0;              // (G2) its spray's counter
            Grain grains[kGrains];
            ADSREnvelope env;
        };
        double nextDraw(Voice &v) const;
        void spawn(Voice &v);
        Params mP;
        std::vector<float> mL, mR;
        long long mFrames = 0;
        double mRate = 48000.0;
        std::vector<std::unique_ptr<Voice>> mVoices;
        long long mAge = 0;
        double mBend = 0.0, mBendRange = 2.0;
    };
}
