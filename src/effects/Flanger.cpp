#include "Flanger.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
    }

    Flanger::Flanger()
    {
        mFs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        const int most = (int)std::ceil((kMaxDelayMs + kMaxDepthMs) * mFs / 1000.0) + 4;
        for (auto &d : mLine)
        {
            d.setSmoothEnable(false);
            d.setMaxDelay(most);
            d.prepare();
        }
    }

    void Flanger::reset()
    {
        const int most = (int)std::ceil((kMaxDelayMs + kMaxDepthMs) * mFs / 1000.0) + 4;
        for (int c = 0; c < kMaxChannels; ++c)
            for (int i = 0; i <= most; ++i) mLine[(size_t)c].write(0.0);
        mPhase = 0.0;
    }

    void Flanger::process(Sample *const *io, int channels, int frames)
    {
        const int n = std::min(channels, kMaxChannels);
        const double step = mRate / mFs;
        for (int i = 0; i < frames; ++i)
        {
            for (int c = 0; c < n; ++c)
            {
                // (FL1) the sweep: a raised cosine from `delay` to `delay + depth`, the odd channels `stereo` ahead
                const double ph = mPhase + ((c & 1) ? mStereo : 0.0);
                const double ms = mDelayMs + mDepthMs * 0.5 * (1.0 - std::cos(2.0 * kPi * ph));
                const double D = std::max(1.0, ms * mFs / 1000.0);
                mUsed[(size_t)c] = D;
                // (FL2) the line read D samples back (read(D − 1) before a write is D back, REQ-delay-1), fed back
                Delay &line = mLine[(size_t)c];
                const double x = io[c][i], d = line.read(D - 1.0);
                line.write(x + mFb * d);
                io[c][i] = (Sample)((1.0 - mMix) * x + mMix * d); // (FL3)
            }
            mPhase += step;
            mPhase -= std::floor(mPhase);
        }
    }
}
