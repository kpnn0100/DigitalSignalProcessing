#include "DecayEnvelope.h"
#include "../base/AudioConfig.h"
#include <cmath>

namespace arstro
{
    namespace
    {
        // (D2): the per-sample ratio that falls 60 dB in `ms`.
        Sample ratioFor(Sample ms)
        {
            const Sample n = ms * 0.001 * AudioConfig::instance().sampleRate();
            return std::pow(10.0, -3.0 / (n < 1 ? 1 : n));
        }
    }

    void DecayEnvelope::trigger(Sample peak)
    {
        mPeak = peak < 0 ? 0 : peak;
        mFloor = mPeak * 1e-4; // (D3): −80 dB of the peak ends the voice
        mRatio = ratioFor(mDecayMs);
        const Sample attackSamples = mAttackMs * 0.001 * AudioConfig::instance().sampleRate();
        if (attackSamples >= 1.0)
        {
            // (D1): a linear rise from wherever it is — a retrigger does not click to zero.
            mStage = Attack;
            mAttackInc = (mPeak - mLevel) / attackSamples;
            if (mAttackInc <= 0) { mStage = Decay; mLevel = mPeak; }
        }
        else
        {
            mStage = Decay;
            mLevel = mPeak;
        }
        if (mPeak <= 0) { mStage = Idle; mLevel = 0; }
    }

    void DecayEnvelope::choke(Sample ms)
    {
        if (mStage == Idle) return;
        mStage = Decay;
        mRatio = ratioFor(ms);
    }

    Sample DecayEnvelope::next()
    {
        switch (mStage)
        {
        case Idle:
            return 0.0;
        case Attack:
        {
            const Sample out = mLevel;
            mLevel += mAttackInc;
            if (mLevel >= mPeak) { mLevel = mPeak; mStage = Decay; }
            return out;
        }
        case Decay:
        {
            const Sample out = mLevel;
            mLevel *= mRatio; // (D2)
            if (mLevel < mFloor) { mLevel = 0.0; mStage = Idle; }
            return out;
        }
        }
        return 0.0;
    }
}
