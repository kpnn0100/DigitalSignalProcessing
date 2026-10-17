/*
 *  Arstro DSP Library — PitchShifter: a sound moved up or down, its length kept, in real time (REQ-fx-pitch-1). Two
 *  taps on one library Delay per channel (interpolated, REQ-delay-1), their delays sweeping through a window at
 *  1 − r samples a sample (r = 2^(semitones/12)), half a window apart, crossfaded sin² / cos² so each jumps back
 *  only while silent. Its taps sit kLatency-ish late on average: latency() says the window's half and the dry path is
 *  delayed to match; at no shift the output IS the input that late, to the bit. effects/README.md, Math —
 *  PitchShifter (PS1)–(PS5). Whole frames; no allocation after construction (up to kMaxChannels; beyond, untouched).
 */
#pragma once
#include "../base/Sample.h"
#include "../simpleProcessor/Delay.h"
#include <array>
#include <cstddef>

namespace arstro
{
    class PitchShifter
    {
    public:
        static constexpr int kMaxChannels = 8;
        static constexpr double kMinWindowMs = 10.0, kMaxWindowMs = 100.0;
        PitchShifter();
        void setSemitones(Sample st) { mSemis = st < -24 ? -24 : st > 24 ? 24 : st; }
        void setCents(Sample ct) { mCents = ct < -100 ? -100 : ct > 100 ? 100 : ct; }
        void setWindowMs(Sample ms);
        void setMix(Sample mix) { mMix = mix < 0 ? 0 : mix > 1 ? 1 : mix; }
        void process(Sample *const *io, int channels, int frames);
        void reset();
        /** 1 + W/2 samples: the taps' mean delay (W the window, an even number of samples). */
        int latency() const { return 1 + mW / 2; }
        /** The ratio it shifts by, 2^((semitones + cents/100)/12). */
        double ratio() const;
        /** The window's phase φ (0 … 1) after the last sample (for tests). */
        double phase() const { return mPhase; }

    private:
        std::array<Delay, kMaxChannels> mLine;
        double mSemis = 0, mCents = 0, mMix = 1, mFs = 48000, mPhase = 0;
        int mW = 1920; // the window in samples, even
    };
}
