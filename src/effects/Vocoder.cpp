#include "Vocoder.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    Vocoder::Vocoder()
    {
        mFs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        for (auto *path : {&mMod, &mCar})
            for (auto &band : *path)
                for (auto &s : band)
                {
                    s.setSmoothEnable(false);
                    s.setType(Biquad::BandPass);
                }
        setAttackMs(mAttackMs);
        setReleaseMs(mReleaseMs);
        design();
    }

    void Vocoder::setBands(int n)
    {
        mBands = std::clamp(n, 4, kMaxBands);
        design();
    }
    void Vocoder::setLowHz(Sample hz)
    {
        mLo = std::clamp((double)hz, 20.0, 0.4 * mFs);
        design();
    }
    void Vocoder::setHighHz(Sample hz)
    {
        mHi = std::clamp((double)hz, 40.0, 0.45 * mFs);
        design();
    }
    void Vocoder::setAttackMs(Sample ms)
    {
        mAttackMs = std::max(0.1, (double)ms);
        mA = std::exp(-1.0 / (mAttackMs * mFs / 1000.0));
    }
    void Vocoder::setReleaseMs(Sample ms)
    {
        mReleaseMs = std::max(1.0, (double)ms);
        mR = std::exp(-1.0 / (mReleaseMs * mFs / 1000.0));
    }
    void Vocoder::setOutputDb(Sample db) { mOut = std::pow(10.0, db / 20.0); }

    double Vocoder::centerHz(int b) const
    {
        const double hi = std::max(mHi, mLo * 1.5);
        return mLo * std::pow(hi / mLo, (b + 0.5) / mBands);
    }

    void Vocoder::design()
    {
        // (VO1) N bands, log-spaced from lo to hi; each band's Q its width: r = (hi/lo)^(1/N), Q = √r / (r − 1)
        const double hi = std::max(mHi, mLo * 1.5), r = std::pow(hi / mLo, 1.0 / mBands), q = std::sqrt(r) / (r - 1.0);
        for (int b = 0; b < mBands; ++b)
            for (auto *path : {&mMod, &mCar})
                for (auto &s : (*path)[(size_t)b])
                {
                    s.setFrequency(centerHz(b));
                    s.setQ(q);
                }
    }

    void Vocoder::reset()
    {
        for (auto *path : {&mMod, &mCar})
            for (auto &band : *path)
                for (auto &s : band) s.reset();
        for (auto &e : mEnv) e.fill(0.0);
    }

    void Vocoder::process(Sample *const *io, int channels, int frames)
    {
        const int n = std::min(channels, kMaxChannels);
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < n; ++c)
            {
                const double m = io[c][i];
                const double k = mKeyChannels > 0 ? (double)mKey[std::min(c, mKeyChannels - 1)][i] : 0.0;
                double y = 0.0;
                for (int b = 0; b < mBands; ++b)
                {
                    // (VO2) the modulator's band, (VO3) its envelope
                    auto &mod = mMod[(size_t)b];
                    const double mb = mod[1].out(mod[0].out(m, c), c), l = std::fabs(mb);
                    double &e = mEnv[(size_t)b][(size_t)c];
                    e = l > e ? mA * (e - l) + l : mR * (e - l) + l;
                    // (VO4) the carrier's band, times it
                    auto &car = mCar[(size_t)b];
                    y += car[1].out(car[0].out(k, c), c) * e;
                }
                io[c][i] = (Sample)(mMix * mOut * y + (1.0 - mMix) * m); // (VO5)
            }
    }
}
