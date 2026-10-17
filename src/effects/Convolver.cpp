#include "Convolver.h"
#include "../base/AudioConfig.h"
#include "../spectral/TimeStretch.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        constexpr int B = Convolver::kBlock, N = 2 * Convolver::kBlock, K = Convolver::kBlock + 1; // K bins kept
    }

    Convolver::Convolver() : mFft(N)
    {
        mFs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        for (auto &ch : mCh)
        {
            ch.in.assign(N, 0.0);
            ch.out.assign(B, 0.0);
            ch.dry.assign(B, 0.0);
            ch.work.assign(N, 0.0);
        }
    }

    void Convolver::setLevelDb(Sample db) { mLevel = std::pow(10.0, db / 20.0); }

    void Convolver::setSample(const float *x, long long frames, int channels, double rate)
    {
        mParts = 0;
        mFrames = 0;
        mH.clear();
        if (!x || frames <= 0 || channels <= 0 || rate <= 0)
        {
            reset();
            return;
        }
        mRespCh = std::min(channels, kMaxChannels);
        // (CV1) each channel at the engine's rate, cut at kMaxSeconds
        std::vector<std::vector<float>> h((size_t)mRespCh);
        const long long most = (long long)(kMaxSeconds * mFs);
        for (int c = 0; c < mRespCh; ++c)
        {
            std::vector<float> one((size_t)frames);
            for (long long i = 0; i < frames; ++i) one[(size_t)i] = x[(size_t)(i * channels + c)];
            if (std::fabs(rate - mFs) > 1e-9)
                one = resample(one, rate / mFs, std::llround((double)frames * mFs / rate));
            if ((long long)one.size() > most) one.resize((size_t)most);
            h[(size_t)c] = std::move(one);
        }
        mFrames = (long long)h[0].size();
        // (CV2) unit energy: the loudest channel's Σ h² made 1
        double energy = 0.0;
        for (const auto &one : h)
        {
            double e = 0.0;
            for (float v : one) e += (double)v * v;
            energy = std::max(energy, e);
        }
        const double g = energy > 0.0 ? 1.0 / std::sqrt(energy) : 0.0;
        // (CV3) partitions of B, each zero-padded to 2B and transformed; B+1 bins kept (the rest are their mirror)
        mParts = (int)((mFrames + B - 1) / B);
        mH.assign((size_t)mRespCh, std::vector<std::complex<double>>((size_t)mParts * K));
        std::vector<std::complex<double>> buf(N);
        for (int c = 0; c < mRespCh; ++c)
            for (int p = 0; p < mParts; ++p)
            {
                std::fill(buf.begin(), buf.end(), 0.0);
                for (int i = 0; i < B; ++i)
                {
                    const long long at = (long long)p * B + i;
                    if (at < mFrames) buf[(size_t)i] = g * h[(size_t)c][(size_t)at];
                }
                mFft.forward(buf.data());
                std::copy(buf.begin(), buf.begin() + K, mH[(size_t)c].begin() + (ptrdiff_t)p * K);
            }
        for (auto &ch : mCh) ch.fdl.assign((size_t)mParts * K, 0.0);
        reset();
    }

    void Convolver::reset()
    {
        for (auto &ch : mCh)
        {
            std::fill(ch.in.begin(), ch.in.end(), 0.0);
            std::fill(ch.out.begin(), ch.out.end(), 0.0);
            std::fill(ch.dry.begin(), ch.dry.end(), 0.0);
            std::fill(ch.fdl.begin(), ch.fdl.end(), 0.0);
        }
        mPos = 0;
        mHead = 0;
    }

    void Convolver::block(int c)
    {
        Channel &ch = mCh[(size_t)c];
        // (CV4) the last 2B inputs, transformed, into the frequency-domain delay line
        for (int i = 0; i < N; ++i) ch.work[(size_t)i] = ch.in[(size_t)i];
        mFft.forward(ch.work.data());
        std::copy(ch.work.begin(), ch.work.begin() + K, ch.fdl.begin() + (ptrdiff_t)mHead * K);
        // (CV5) Y = Σ_p H_p · X_(k − p)
        const auto &H = mH[(size_t)std::min(c, mRespCh - 1)];
        std::fill(ch.work.begin(), ch.work.end(), 0.0);
        for (int p = 0; p < mParts; ++p)
        {
            const std::complex<double> *h = &H[(size_t)p * K], *X = &ch.fdl[(size_t)((mHead - p + mParts) % mParts) * K];
            for (int k = 0; k < K; ++k) ch.work[(size_t)k] += h[k] * X[k];
        }
        for (int k = 1; k < B; ++k) ch.work[(size_t)(N - k)] = std::conj(ch.work[(size_t)k]); // a real signal's mirror
        mFft.inverse(ch.work.data());
        // (CV6) overlap-save: the second half is this block's; the window slides by B
        for (int i = 0; i < B; ++i) ch.out[(size_t)i] = ch.work[(size_t)(B + i)].real();
        std::copy(ch.in.begin() + B, ch.in.end(), ch.in.begin());
    }

    void Convolver::process(Sample *const *io, int channels, int frames)
    {
        const int n = std::min(channels, kMaxChannels);
        for (int i = 0; i < frames; ++i)
        {
            for (int c = 0; c < n; ++c)
            {
                Channel &ch = mCh[(size_t)c];
                const double x = io[c][i], wet = mParts > 0 ? ch.out[(size_t)mPos] : 0.0, dry = ch.dry[(size_t)mPos];
                ch.dry[(size_t)mPos] = x;
                ch.in[(size_t)(B + mPos)] = x;
                io[c][i] = (Sample)(mMix * mLevel * wet + (1.0 - mMix) * dry); // (CV6) both B late
            }
            if (++mPos == B)
            {
                if (mParts > 0)
                    for (int c = 0; c < n; ++c) block(c);
                else
                    for (int c = 0; c < n; ++c) std::copy(mCh[(size_t)c].in.begin() + B, mCh[(size_t)c].in.end(), mCh[(size_t)c].in.begin());
                mHead = mParts > 0 ? (mHead + 1) % mParts : 0;
                mPos = 0;
            }
        }
    }
}
