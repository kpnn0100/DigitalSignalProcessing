#include "DeEsser.h"
#include "../base/AudioConfig.h"
#include "../equalizer/Biquad.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        double rate() { return std::max(8000.0, (double)AudioConfig::instance().sampleRate()); }
    }

    DeEsser::DeEsser()
    {
        auto c = [](double ms) { return std::exp(-1.0 / (ms * rate() / 1000.0)); };
        mAtk = c(kAttackMs);
        mRel = c(kReleaseMs);
        design();
    }

    void DeEsser::setFrequency(Sample hz)
    {
        mF = hz;
        design();
    }

    void DeEsser::design()
    {
        // (DE1) the band: a Butterworth high-pass at f (RBJ, Q = 1/√2)
        const auto c = Biquad::design(Biquad::HighPass, mF, 0.7071067811865476, 0.0, rate());
        mB0 = c.b0;
        mB1 = c.b1;
        mB2 = c.b2;
        mA1 = c.a1;
        mA2 = c.a2;
        mShelfCut = -1.0; // (DE4) the shelf redesigned at its new corner on the next sample
    }

    void DeEsser::reset()
    {
        std::fill(mZ1, mZ1 + kMaxChannels, 0.0);
        std::fill(mZ2, mZ2 + kMaxChannels, 0.0);
        std::fill(mY1, mY1 + kMaxChannels, 0.0);
        std::fill(mY2, mY2 + kMaxChannels, 0.0);
        mEnv = mCut = 0.0;
    }

    void DeEsser::process(Sample *const *io, int channels, int frames)
    {
        const int filtered = std::min(channels, kMaxChannels);
        double h[kMaxChannels];
        for (int i = 0; i < frames; ++i)
        {
            // (DE1) each channel's band; (DE2) its level, the loudest channel's
            double l = 0.0;
            for (int c = 0; c < filtered; ++c)
            {
                const double x = io[c][i], y = mB0 * x + mZ1[c];
                mZ1[c] = mB1 * x - mA1 * y + mZ2[c];
                mZ2[c] = mB2 * x - mA2 * y;
                h[c] = y;
                l = std::max(l, std::fabs(y));
            }
            mEnv = l > mEnv ? mAtk * (mEnv - l) + l : mRel * (mEnv - l) + l;
            // (DE3) the band held at the threshold, never cut by more than the range
            mCut = std::clamp(20.0 * std::log10(mEnv + 1e-9) - mT, 0.0, mRange);
            if (mSplit && mCut == 0.0)
            {
                // (DE4) at 0 dB the shelf is the identity, whose state is zero: held there while nothing is cut, so a
                // cut growing from 0 starts the shelf from its own past (no click), and an idle de-esser costs nothing
                std::fill(mY1, mY1 + filtered, 0.0);
                std::fill(mY2, mY2 + filtered, 0.0);
            }
            else if (mSplit)
            {
                // (DE4) the shelf, its gain −cut
                if (mCut != mShelfCut)
                {
                    const auto c = Biquad::design(Biquad::HighShelf, mF, 0.7071067811865476, -mCut, rate());
                    mS[0] = c.b0;
                    mS[1] = c.b1;
                    mS[2] = c.b2;
                    mS[3] = c.a1;
                    mS[4] = c.a2;
                    mShelfCut = mCut;
                }
                for (int c = 0; c < filtered; ++c)
                {
                    const double x = io[c][i], y = mS[0] * x + mY1[c];
                    mY1[c] = mS[1] * x - mS[3] * y + mY2[c];
                    mY2[c] = mS[2] * x - mS[4] * y;
                    h[c] = y;
                }
            }
            if (mCut == 0.0) continue; // nothing cut: the input, to the bit
            const double g = std::pow(10.0, -mCut / 20.0);
            for (int c = 0; c < channels; ++c)
            {
                if (mSplit && c < filtered) io[c][i] = (Sample)h[c]; // (DE4) split: the shelf's
                else io[c][i] = (Sample)(io[c][i] * g);              // (DE5) wide
            }
        }
    }
}
