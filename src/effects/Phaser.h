/*
 *  Arstro DSP Library — Phaser: notches swept by an LFO (REQ-fx-phaser-1). A chain of first-order all-passes, their
 *  break frequency swept by a sine in log frequency, fed back on itself, mixed with the dry sound: where the chain
 *  has turned an odd number of half-turns the two cancel. effects/README.md, Math — Phaser (PH1)–(PH5). Whole frames;
 *  no allocation (up to kMaxChannels; beyond them a channel passes untouched).
 */
#pragma once
#include "../base/Sample.h"
#include <array>
#include <cstddef>

namespace arstro
{
    class Phaser
    {
    public:
        static constexpr int kMaxStages = 12, kMaxChannels = 8;
        Phaser();
        /** 2 … 12, even (an odd count rounds up). */
        void setStages(int n);
        void setRateHz(Sample hz) { mRate = hz < 0 ? 0 : hz; }
        void setCenterHz(Sample hz) { mCenter = hz; }
        void setDepthOct(Sample oct) { mDepth = oct < 0 ? 0 : oct; }
        void setFeedback(Sample fb) { mFb = fb < -0.95 ? -0.95 : fb > 0.95 ? 0.95 : fb; }
        /** The right channel's LFO ahead of the left's, degrees. */
        void setStereoDeg(Sample deg) { mStereo = deg / 360.0; }
        void setMix(Sample mix) { mMix = mix < 0 ? 0 : mix > 1 ? 1 : mix; }
        void process(Sample *const *io, int channels, int frames);
        void reset();
        /** The break frequency channel `c` used for its last sample, Hz (for tests). */
        Sample breakHz(int c) const { return mBreak[(size_t)(c < 0 ? 0 : c >= kMaxChannels ? kMaxChannels - 1 : c)]; }
        /** (PH1) a stage's coefficient for a break frequency: a = (t − 1)/(t + 1), t = tan(π·hz/fs). */
        static double coefficient(double hz, double fs);

    private:
        int mStages = 4;
        double mRate = 0.5, mCenter = 800, mDepth = 2, mFb = 0.3, mStereo = 0.25, mMix = 0.5, mFs = 48000, mPhase = 0;
        std::array<std::array<double, kMaxStages>, kMaxChannels> mZ{};
        std::array<double, kMaxChannels> mLast{}, mBreak{};
    };
}
