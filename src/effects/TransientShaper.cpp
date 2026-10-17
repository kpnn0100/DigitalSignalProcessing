#include "TransientShaper.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    TransientShaper::TransientShaper()
    {
        const double fs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        auto c = [&](double ms) { return std::exp(-1.0 / (ms * fs / 1000.0)); };
        mFa = c(kFastAttackMs);
        mFr = c(kFastReleaseMs);
        mSa = c(kSlowAttackMs);
        mSr = c(kSlowReleaseMs);
    }

    void TransientShaper::reset()
    {
        mFast = mRise = mFall = mGain = 0.0;
    }

    void TransientShaper::process(Sample *const *io, int channels, int frames)
    {
        constexpr double eps = 1e-5; // (TS4) −100 dB: silence reads as no difference
        const double out = mOut;
        for (int i = 0; i < frames; ++i)
        {
            // (TS1) the level: the loudest channel
            double l = 0.0;
            for (int c = 0; c < channels; ++c) l = std::max(l, std::fabs((double)io[c][i]));
            // (TS2) the fast envelope
            mFast = l > mFast ? mFa * (mFast - l) + l : mFr * (mFast - l) + l;
            // (TS3) two slow ones that follow it: one only up (down at once), one only down (up at once)
            mRise = mFast > mRise ? mSa * (mRise - mFast) + mFast : mFast;
            mFall = mFast < mFall ? mSr * (mFall - mFast) + mFast : mFast;
            // (TS4) the differences in dB, scaled, limited
            const double da = 20.0 * std::log10((mFast + eps) / (mRise + eps));
            const double ds = 20.0 * std::log10((mFall + eps) / (mFast + eps));
            mGain = std::clamp(mA * da + mS * ds, -kLimitDb, kLimitDb);
            if (mGain == 0.0 && out == 0.0) continue; // nothing to do: the input untouched, to the bit
            const double g = std::pow(10.0, (mGain + out) / 20.0); // (TS5)
            for (int c = 0; c < channels; ++c) io[c][i] = (Sample)(io[c][i] * g);
        }
    }
}
