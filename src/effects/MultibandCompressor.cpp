#include "MultibandCompressor.h"
#include "../base/AudioConfig.h"
#include "../equalizer/Biquad.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    MultibandCompressor::MultibandCompressor()
    {
        for (auto &b : mBand) b.assign((size_t)kMaxBlock, 0.0);
        for (int k = 0; k < kBands; ++k) { setThresholdDb(k, -20.0); setRatio(k, 3.0); setGainDb(k, 0.0); }
        setAttackMs(10.0);
        setReleaseMs(150.0);
        size(AudioConfig::instance().channelCount());
        design();
    }

    void MultibandCompressor::size(int channels)
    {
        channels = std::max(1, channels);
        if (channels == mChannels) return;
        mChannels = channels;
        for (Section *group : {mLp1, mHp1, mLp2, mHp2, mApLp, mApHp})
            for (int i = 0; i < 2; ++i)
            {
                group[i].z1.assign((size_t)channels, 0.0);
                group[i].z2.assign((size_t)channels, 0.0);
            }
    }

    void MultibandCompressor::design()
    {
        const Sample fs = std::max((Sample)8000, (Sample)AudioConfig::instance().sampleRate());
        auto set = [&](Section *s, Biquad::Type t, Sample hz) {
            const Biquad::Coeffs c = Biquad::design(t, hz, 0.70710678118654752, 0.0, fs);
            for (int i = 0; i < 2; ++i) { s[i].b0 = c.b0; s[i].b1 = c.b1; s[i].b2 = c.b2; s[i].a1 = c.a1; s[i].a2 = c.a2; }
        };
        set(mLp1, Biquad::LowPass, mF1);
        set(mHp1, Biquad::HighPass, mF1);
        set(mLp2, Biquad::LowPass, mF2);
        set(mHp2, Biquad::HighPass, mF2);
        set(mApLp, Biquad::LowPass, mF2); // (MB2) the low band's all-pass: LP_f2 + HP_f2
        set(mApHp, Biquad::HighPass, mF2);
    }

    void MultibandCompressor::setSplits(Sample lowHz, Sample highHz)
    {
        const Sample nyq = 0.45 * std::max((Sample)8000, (Sample)AudioConfig::instance().sampleRate());
        mF1 = std::clamp(lowHz, (Sample)20.0, nyq / 1.5);
        mF2 = std::clamp(highHz, mF1 * 1.5, nyq); // a fifth above at least
        design();
    }

    void MultibandCompressor::setThresholdDb(int band, Sample db)
    {
        if (band >= 0 && band < kBands) mComp[(size_t)band].setThresholdDb(db);
    }
    void MultibandCompressor::setRatio(int band, Sample ratio)
    {
        if (band >= 0 && band < kBands) mComp[(size_t)band].setRatio(std::max((Sample)1.0, ratio));
    }
    void MultibandCompressor::setGainDb(int band, Sample db)
    {
        if (band >= 0 && band < kBands) mComp[(size_t)band].setMakeupGain(std::pow(10.0, db / 20.0));
    }

    void MultibandCompressor::setAttackMs(Sample ms) { for (auto &c : mComp) c.setAttackMs(ms); }
    void MultibandCompressor::setReleaseMs(Sample ms) { for (auto &c : mComp) c.setReleaseMs(ms); }

    void MultibandCompressor::reset()
    {
        for (Section *group : {mLp1, mHp1, mLp2, mHp2, mApLp, mApHp})
            for (int i = 0; i < 2; ++i)
            {
                std::fill(group[i].z1.begin(), group[i].z1.end(), 0.0);
                std::fill(group[i].z2.begin(), group[i].z2.end(), 0.0);
            }
    }

    void MultibandCompressor::process(Sample *const *io, int channels, int frames)
    {
        size(std::max(channels, AudioConfig::instance().channelCount()));
        for (int at = 0; at < frames; at += kMaxBlock)
        {
            const int n = std::min(kMaxBlock, frames - at);
            for (int c = 0; c < channels; ++c)
            {
                Sample *x = io[c] + at;
                for (int i = 0; i < n; ++i)
                {
                    // (MB1)–(MB2) the split
                    const double lo = mLp1[1].run(mLp1[0].run(x[i], c), c);
                    const double rest = mHp1[1].run(mHp1[0].run(x[i], c), c);
                    const double loAp = mApLp[1].run(mApLp[0].run(lo, c), c) + mApHp[1].run(mApHp[0].run(lo, c), c);
                    mBand[0][(size_t)i] = loAp;
                    mBand[1][(size_t)i] = mLp2[1].run(mLp2[0].run(rest, c), c);
                    mBand[2][(size_t)i] = mHp2[1].run(mHp2[0].run(rest, c), c);
                }
                // (MB3) each band compressed alone; (MB4) summed, the output gain
                for (int k = 0; k < kBands; ++k) mComp[(size_t)k].processBlock(mBand[k].data(), n, c);
                for (int i = 0; i < n; ++i) x[i] = (mBand[0][(size_t)i] + mBand[1][(size_t)i] + mBand[2][(size_t)i]) * mOut;
            }
        }
    }
}
