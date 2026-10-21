#include "Vowel.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    // (VW1) Peterson & Barney (1952), the men's averages: /ɑ/ (father), /ɛ/ (bet), /i/ (beet), /ɔ/ (bought), /u/ (boot)
    const double Vowel::kTable[kVowels][kFormants] = {
        {730.0, 1090.0, 2440.0}, {530.0, 1840.0, 2480.0}, {270.0, 2290.0, 3010.0}, {570.0, 840.0, 2410.0}, {300.0, 870.0, 2240.0}};

    namespace
    {
        constexpr double kBandwidth[Vowel::kFormants] = {80.0, 90.0, 120.0}; // (VW3) a voice's bandwidths, Hz
        constexpr double kWeight[Vowel::kFormants] = {1.0, 0.63, 0.32};      // (VW4) F2 −4 dB, F3 −10 dB under F1
        constexpr int kRedesign = 16;                                         // the bands follow a glide every 16 samples
    }

    Vowel::Vowel() { mFs = std::max(8000.0, (double)AudioConfig::instance().sampleRate()); }

    void Vowel::setOutputDb(Sample db) { mOut = std::pow(10.0, (double)db / 20.0); }

    void Vowel::setGlideMs(Sample ms) { mAlpha = ms <= 0 ? 1.0 : 1.0 - std::exp(-1.0 / (mFs * (double)ms / 1000.0)); }

    double Vowel::formantHz(double v, int k, double st)
    {
        // (VW2) between two vowels the formant moves in log frequency; `shift` moves all three by semitones
        v = std::clamp(v, 0.0, 4.0);
        const int a = std::min(3, (int)std::floor(v));
        const double f = v - a;
        const double lo = std::log(kTable[a][k]), hi = std::log(kTable[a + 1][k]);
        return std::exp(lo + f * (hi - lo)) * std::exp2(st / 12.0);
    }

    void Vowel::design()
    {
        for (int k = 0; k < kFormants; ++k)
        {
            const double hz = formantHz(mVowel, k, mShift);
            const double q = hz / (kBandwidth[k] / mRes); // (VW3) Q = f / bandwidth, narrowed by the resonance
            mC[(size_t)k] = Biquad::design(Biquad::BandPass, (Sample)hz, (Sample)q, 0.0, (Sample)mFs);
        }
        mDesigned = mVowel;
        mDirty = false;
    }

    void Vowel::reset()
    {
        for (auto &ch : mZ)
            for (auto &z : ch) z = {0.0, 0.0};
        mVowel = mVowelTarget;
        mDirty = true;
        mCount = 0;
    }

    void Vowel::process(Sample *const *io, int channels, int frames)
    {
        const int n = std::min(channels, kMaxChannels);
        for (int i = 0; i < frames; ++i)
        {
            mVowel += mAlpha * (mVowelTarget - mVowel); // the vowel's glide
            if (mDirty || (mCount == 0 && std::fabs(mVowel - mDesigned) > 1e-6)) design();
            mCount = (mCount + 1) % kRedesign;
            for (int c = 0; c < n; ++c)
            {
                const double x = io[c][i];
                double y = 0.0;
                for (int k = 0; k < kFormants; ++k)
                {
                    // (VW4) each band the RBJ band-pass, transposed direct form II
                    const Biquad::Coeffs &cf = mC[(size_t)k];
                    auto &z = mZ[(size_t)c][(size_t)k];
                    const double b = cf.b0 * x + z[0];
                    z[0] = cf.b1 * x - cf.a1 * b + z[1];
                    z[1] = cf.b2 * x - cf.a2 * b;
                    y += kWeight[k] * b;
                }
                io[c][i] = (Sample)(mOut * ((1.0 - mMix) * x + mMix * y)); // (VW5)
            }
        }
    }
}
