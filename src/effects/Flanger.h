/*
 *  Arstro DSP Library — Flanger: a short delay swept by an LFO and fed back (REQ-fx-flanger-1). The chorus's shape
 *  with a shorter delay and feedback: one library Delay per channel (interpolated between samples, REQ-delay-1),
 *  its length swept by a raised cosine from `delay` to `delay + depth`, what it reads fed back into what it writes,
 *  mixed with the dry sound — a comb whose teeth sweep. effects/README.md, Math — Flanger (FL1)–(FL4). Whole frames;
 *  no allocation after construction (up to kMaxChannels; beyond them a channel passes untouched).
 */
#pragma once
#include "../base/Sample.h"
#include "../simpleProcessor/Delay.h"
#include <array>
#include <cstddef>

namespace arstro
{
    class Flanger
    {
    public:
        static constexpr int kMaxChannels = 8;
        static constexpr double kMaxDelayMs = 10.0, kMaxDepthMs = 10.0;
        Flanger();
        void setDelayMs(Sample ms) { mDelayMs = ms < 0 ? 0 : ms > kMaxDelayMs ? kMaxDelayMs : ms; }
        void setDepthMs(Sample ms) { mDepthMs = ms < 0 ? 0 : ms > kMaxDepthMs ? kMaxDepthMs : ms; }
        void setRateHz(Sample hz) { mRate = hz < 0 ? 0 : hz; }
        void setFeedback(Sample fb) { mFb = fb < -0.95 ? -0.95 : fb > 0.95 ? 0.95 : fb; }
        /** The right channel's LFO ahead of the left's, degrees. */
        void setStereoDeg(Sample deg) { mStereo = deg / 360.0; }
        void setMix(Sample mix) { mMix = mix < 0 ? 0 : mix > 1 ? 1 : mix; }
        void process(Sample *const *io, int channels, int frames);
        void reset();
        /** The delay channel `c` used for its last sample, in samples (for tests). */
        Sample delaySamples(int c) const { return mUsed[(size_t)(c < 0 ? 0 : c >= kMaxChannels ? kMaxChannels - 1 : c)]; }

    private:
        std::array<Delay, kMaxChannels> mLine;
        std::array<double, kMaxChannels> mUsed{};
        double mDelayMs = 1, mDepthMs = 3, mRate = 0.25, mFb = 0.5, mStereo = 0.25, mMix = 0.5, mFs = 48000, mPhase = 0;
    };
}
