#include "PitchShifter.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
    }

    PitchShifter::PitchShifter()
    {
        mFs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        const int most = (int)std::ceil(kMaxWindowMs * mFs / 1000.0) + 8;
        for (auto &d : mLine)
        {
            d.setSmoothEnable(false);
            d.setMaxDelay(most);
            d.prepare();
        }
        setWindowMs(40.0);
    }

    void PitchShifter::setWindowMs(Sample ms)
    {
        const double w = std::clamp((double)ms, kMinWindowMs, kMaxWindowMs);
        mW = 2 * std::max(1, (int)std::lround(w * mFs / 2000.0)); // (PS1) even: its half is whole
    }

    double PitchShifter::ratio() const { return std::exp2((mSemis + mCents / 100.0) / 12.0); }

    void PitchShifter::reset()
    {
        const int most = (int)std::ceil(kMaxWindowMs * mFs / 1000.0) + 8;
        for (auto &d : mLine)
            for (int i = 0; i <= most; ++i) d.write(0.0);
        mPhase = 0.0;
    }

    void PitchShifter::process(Sample *const *io, int channels, int frames)
    {
        const int n = std::min(channels, kMaxChannels);
        const double r = ratio(), W = (double)mW, L = (double)latency();
        const double step = (1.0 - r) / W; // (PS2) the delay moves 1 − r a sample: φ moves that over W
        const bool still = r == 1.0;
        for (int i = 0; i < frames; ++i)
        {
            // (PS3) the two taps: delays 1 + W·φ and 1 + W·frac(φ + ½), weights sin²(πφ) and cos²(πφ)
            const double pa = mPhase, pb = pa + 0.5 >= 1.0 ? pa - 0.5 : pa + 0.5;
            const double wa = std::sin(kPi * pa) * std::sin(kPi * pa), wb = 1.0 - wa;
            for (int c = 0; c < n; ++c)
            {
                Delay &line = mLine[(size_t)c];
                const double x = io[c][i];
                // (PS4) at no shift: the input L late, to the bit; else the taps (read(D − 1) is D back)
                const double dry = line.read(L - 1.0);
                const double wet = still ? dry : wa * line.read(W * pa) + wb * line.read(W * pb);
                line.write(x);
                io[c][i] = (Sample)(mMix * wet + (1.0 - mMix) * dry); // (PS5)
            }
            mPhase += step;
            mPhase -= std::floor(mPhase);
        }
    }
}
