#include "Gate.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    Gate::Gate() { coeffs(); }

    void Gate::coeffs()
    {
        const double fs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        auto c = [&](double ms) { return std::exp(-1.0 / std::max(1.0, ms * fs / 1000.0)); };
        mDetA = c(1.0);   // (GT1) the detector: 1 ms up, 50 ms down
        mDetR = c(50.0);
        mOpen = c(mAttackMs);   // (GT4)
        mClose = c(mReleaseMs);
        mHold = (long long)std::llround(mHoldMs * fs / 1000.0);
    }

    void Gate::reset()
    {
        mEnv = 0.0;
        mGain = 0.0;
        mHoldLeft = 0;
    }

    void Gate::process(Sample *const *io, int channels, int frames)
    {
        for (int i = 0; i < frames; ++i)
        {
            // (GT1) the level: the loudest channel (or the key's)
            double l = 0.0;
            if (mSidechain)
            {
                for (int c = 0; c < mKeyChannels; ++c) l = std::max(l, std::fabs((double)mKey[c][i]));
            }
            else
                for (int c = 0; c < channels; ++c) l = std::max(l, std::fabs((double)io[c][i]));
            mEnv = l > mEnv ? mDetA * (mEnv - l) + l : mDetR * (mEnv - l) + l;
            // (GT2) the curve; (GT3) the hold
            const double L = 20.0 * std::log10(mEnv + 1e-9);
            double target = 0.0;
            if (L >= mT) mHoldLeft = mHold;
            else if (mHoldLeft > 0) --mHoldLeft;
            else target = std::max(-(double)mRange, (L - mT) * (mR - 1.0));
            // (GT4) the gain smoothed in dB — opening at the attack, closing at the release
            mGain = target + (target > mGain ? mOpen : mClose) * (mGain - target);
            const double g = std::pow(10.0, mGain / 20.0); // (GT5)
            for (int c = 0; c < channels; ++c) io[c][i] = (Sample)(io[c][i] * g);
        }
    }
}
