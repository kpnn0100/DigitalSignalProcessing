#include "Bitcrusher.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    Bitcrusher::Bitcrusher() { mFs = std::max(8000.0, (double)AudioConfig::instance().sampleRate()); }

    void Bitcrusher::setBits(Sample bits) { mQ = std::pow(2.0, std::clamp((double)bits, 1.0, 24.0) - 1.0); }
    void Bitcrusher::setOutputDb(Sample db) { mOut = std::pow(10.0, db / 20.0); }

    void Bitcrusher::reset()
    {
        mHeld.fill(0.0);
        mPhase = 1.0;
        mTaken = 0;
    }

    void Bitcrusher::process(Sample *const *io, int channels, int frames)
    {
        const int n = std::min(channels, kMaxChannels);
        const double step = std::min(1.0, mRate / mFs);
        for (int i = 0; i < frames; ++i)
        {
            // (BC2) the hold: a new value when the phase passes 1 (every sample at or above the sample rate)
            const bool take = mPhase >= 1.0;
            if (take)
            {
                mPhase -= 1.0;
                ++mTaken;
            }
            mPhase += step;
            for (int c = 0; c < n; ++c)
            {
                const double x = io[c][i];
                if (take) mHeld[(size_t)c] = std::round(x * mQ) / mQ; // (BC1) the grid
                io[c][i] = (Sample)(mOut * (mMix * mHeld[(size_t)c] + (1.0 - mMix) * x)); // (BC3)
            }
        }
    }
}
