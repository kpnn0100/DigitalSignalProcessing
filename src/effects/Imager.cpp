#include "Imager.h"
#include <algorithm>

namespace arstro
{
    namespace
    {
        constexpr double kButterworthQ = 0.70710678118654752;
    }

    Imager::Imager()
        : mSideHp{Biquad(Biquad::HighPass, 120.0, kButterworthQ), Biquad(Biquad::HighPass, 120.0, kButterworthQ)},
          mMidLp{Biquad(Biquad::LowPass, 120.0, kButterworthQ), Biquad(Biquad::LowPass, 120.0, kButterworthQ)},
          mMidHp{Biquad(Biquad::HighPass, 120.0, kButterworthQ), Biquad(Biquad::HighPass, 120.0, kButterworthQ)}
    {
        for (Biquad *b : {&mSideHp[0], &mSideHp[1], &mMidLp[0], &mMidLp[1], &mMidHp[0], &mMidHp[1]}) b->setSmoothEnable(false);
    }

    void Imager::setMonoHz(Sample hz)
    {
        mHz = hz;
        for (Biquad *b : {&mSideHp[0], &mSideHp[1], &mMidLp[0], &mMidLp[1], &mMidHp[0], &mMidHp[1]}) b->setFrequency(hz);
    }

    void Imager::reset()
    {
        for (Biquad *b : {&mSideHp[0], &mSideHp[1], &mMidLp[0], &mMidLp[1], &mMidHp[0], &mMidHp[1]}) b->reset();
    }

    void Imager::process(Sample *const *io, int channels, int frames)
    {
        if (!mMono && mW == 1.0) return; // as it is: the input, to the bit
        for (int pair = 0; pair + 1 < channels; pair += 2)
        {
            Sample *L = io[pair], *R = io[pair + 1];
            const int k = pair / 2; // the filters' channel for this pair
            for (int i = 0; i < frames; ++i)
            {
                // (IM1) mid and side
                double m = 0.5 * ((double)L[i] + (double)R[i]), s = 0.5 * ((double)L[i] - (double)R[i]);
                if (mMono)
                {
                    // (IM2) the side's lows away; the mid through the matching all-pass, so the two stay in phase
                    s = mSideHp[1].out(mSideHp[0].out(s, k), k);
                    m = mMidLp[1].out(mMidLp[0].out(m, k), k) + mMidHp[1].out(mMidHp[0].out(m, k), k);
                }
                s *= mW; // (IM3)
                L[i] = (Sample)(m + s); // (IM4)
                R[i] = (Sample)(m - s);
            }
        }
    }
}
