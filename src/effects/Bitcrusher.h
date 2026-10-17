/*
 *  Arstro DSP Library — Bitcrusher: fewer bits, a lower rate (REQ-fx-bitcrusher-1). Each sample rounded to a grid of
 *  2^(bits−1) steps a side; held for fs/rate samples at a time (a sample-and-hold, its images left in on purpose — the
 *  sound of it). effects/README.md, Math — Bitcrusher (BC1)–(BC3). Whole frames; no allocation (up to kMaxChannels;
 *  beyond them a channel passes untouched).
 */
#pragma once
#include "../base/Sample.h"
#include <array>

namespace arstro
{
    class Bitcrusher
    {
    public:
        static constexpr int kMaxChannels = 8;
        Bitcrusher();
        void setBits(Sample bits);
        /** Hz; at or above the sample rate, no hold. */
        void setRate(Sample hz) { mRate = hz; }
        void setMix(Sample mix) { mMix = mix < 0 ? 0 : mix > 1 ? 1 : mix; }
        void setOutputDb(Sample db);
        void process(Sample *const *io, int channels, int frames);
        void reset();
        /** New values taken since the start (for tests). */
        long long taken() const { return mTaken; }

    private:
        double mQ = 128, mRate = 96000, mMix = 1, mOut = 1, mFs = 48000, mPhase = 1.0;
        std::array<double, kMaxChannels> mHeld{};
        long long mTaken = 0;
    };
}
