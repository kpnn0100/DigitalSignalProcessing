#include "Phaser.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
    }

    Phaser::Phaser()
    {
        mFs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        mBreak.fill(mCenter);
    }

    void Phaser::setStages(int n) { mStages = std::clamp(n + (n & 1), 2, kMaxStages); }

    double Phaser::coefficient(double hz, double fs)
    {
        const double t = std::tan(kPi * hz / fs);
        return (t - 1.0) / (t + 1.0);
    }

    void Phaser::reset()
    {
        for (auto &z : mZ) z.fill(0.0);
        mLast.fill(0.0);
        mPhase = 0.0;
    }

    void Phaser::process(Sample *const *io, int channels, int frames)
    {
        const int n = std::min(channels, kMaxChannels);
        const double step = mRate / mFs, lo = 20.0, hi = 0.45 * mFs;
        for (int i = 0; i < frames; ++i)
        {
            for (int c = 0; c < n; ++c)
            {
                // (PH2) the sweep: a sine in log frequency, the odd channels `stereo` ahead
                const double ph = mPhase + ((c & 1) ? mStereo : 0.0);
                const double hz = std::clamp(mCenter * std::exp2(mDepth * std::sin(2.0 * kPi * ph)), lo, hi);
                mBreak[(size_t)c] = hz;
                const double a = coefficient(hz, mFs);
                // (PH3) fed back: the chain's last output, a sample on; (PH1) the stages
                const double x = io[c][i];
                double v = x + mFb * mLast[(size_t)c];
                auto &z = mZ[(size_t)c];
                for (int s = 0; s < mStages; ++s)
                {
                    const double y = a * v + z[(size_t)s];
                    z[(size_t)s] = v - a * y;
                    v = y;
                }
                mLast[(size_t)c] = v;
                io[c][i] = (Sample)((1.0 - mMix) * x + mMix * v); // (PH4)
            }
            mPhase += step;
            mPhase -= std::floor(mPhase);
        }
    }
}
