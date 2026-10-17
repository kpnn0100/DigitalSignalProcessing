#include "Tuner.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    Tuner::Tuner() : mFft(2 * kWindow)
    {
        mFs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        mRing.assign(kWindow, 0.0);
        mA.assign(2 * kWindow, 0.0);
        mB.assign(2 * kWindow, 0.0);
        mX.assign(kWindow, 0.0);
        mD.assign(kWindow / 2 + 1, 0.0);
        mRaw.assign(kWindow / 2 + 1, 0.0);
        mPrefix.assign(kWindow + 1, 0.0);
        for (auto &o : mOut) o.store(0.0);
        mOut[Level].store(-144.0);
    }

    void Tuner::reset()
    {
        std::fill(mRing.begin(), mRing.end(), 0.0);
        mAt = mSince = 0;
        mHeard = 0;
        for (auto &o : mOut) o.store(0.0);
        mOut[Level].store(-144.0);
    }

    void Tuner::process(Sample *const *io, int channels, int frames)
    {
        if (channels <= 0) return;
        for (int i = 0; i < frames; ++i)
        {
            double x = 0.0;
            for (int c = 0; c < channels; ++c) x += io[c][i];
            mRing[(size_t)mAt] = x / channels;
            mAt = (mAt + 1) % kWindow;
            ++mHeard;
            if (++mSince >= kHop)
            {
                mSince = 0;
                if (mHeard >= kWindow) estimate();
            }
        }
    }

    void Tuner::estimate()
    {
        const int W = kWindow, H = kWindow / 2, N = 2 * kWindow;
        for (int j = 0; j < W; ++j) mX[(size_t)j] = mRing[(size_t)((mAt + j) % W)];
        // (Y5) the level; too quiet, no pitch
        double e = 0.0;
        for (double v : mX) e += v * v;
        const double level = 10.0 * std::log10(e / W + 1e-30);
        mOut[Level].store(level, std::memory_order_relaxed);
        auto none = [&] {
            mOut[Frequency].store(0.0, std::memory_order_relaxed);
            mOut[Note].store(0.0, std::memory_order_relaxed);
            mOut[Cents].store(0.0, std::memory_order_relaxed);
        };
        if (level < kQuietDb) { none(); return; }
        // (Y1) d(τ) = Σ_{j<H} (x_j − x_{j+τ})² = e_0 + e(τ) − 2·r(τ):  r by the FFT (a against b, no wrap), e by prefixes
        for (int j = 0; j < N; ++j)
        {
            mA[(size_t)j] = j < H ? mX[(size_t)j] : 0.0;
            mB[(size_t)j] = j < W ? mX[(size_t)j] : 0.0;
        }
        mFft.forward(mA.data());
        mFft.forward(mB.data());
        for (int k = 0; k < N; ++k) mA[(size_t)k] = std::conj(mA[(size_t)k]) * mB[(size_t)k];
        mFft.inverse(mA.data());
        mPrefix[0] = 0.0;
        for (int j = 0; j < W; ++j) mPrefix[(size_t)j + 1] = mPrefix[(size_t)j] + mX[(size_t)j] * mX[(size_t)j];
        const double e0 = mPrefix[(size_t)H];
        for (int t = 0; t <= H; ++t) mRaw[(size_t)t] = mD[(size_t)t] = std::max(0.0, e0 + (mPrefix[(size_t)(t + H)] - mPrefix[(size_t)t]) - 2.0 * mA[(size_t)t].real());
        // (Y2) normalised by the running mean: d'(0) = 1, d'(τ) = d(τ)·τ / Σ_{1..τ} d
        double run = 0.0;
        mD[0] = 1.0;
        for (int t = 1; t <= H; ++t)
        {
            run += mD[(size_t)t];
            mD[(size_t)t] = run > 0.0 ? mD[(size_t)t] * t / run : 1.0;
        }
        // (Y3) the first dip under the threshold, followed to its bottom
        const int lo = std::max(2, (int)std::floor(mFs / kMaxHz)), hi = std::min(H - 1, (int)std::ceil(mFs / kMinHz));
        int tau = -1;
        for (int t = lo; t <= hi; ++t)
            if (mD[(size_t)t] < kThreshold)
            {
                while (t + 1 <= hi && mD[(size_t)(t + 1)] < mD[(size_t)t]) ++t;
                tau = t;
                break;
            }
        if (tau < 0) { none(); return; }
        // (Y4) a parabola through the dip and its neighbours — of d itself: d′'s running mean leans the fit
        const double a = mRaw[(size_t)(tau - 1)], b = mRaw[(size_t)tau], c = mRaw[(size_t)(tau + 1)], den = a - 2.0 * b + c;
        const double t = tau + (den != 0.0 ? 0.5 * (a - c) / den : 0.0);
        // (Y6) the frequency; the nearest note and its cents against the reference
        const double hz = mFs / t, semis = 12.0 * std::log2(hz / mRef);
        const double note = 69.0 + std::round(semis);
        mOut[Frequency].store(hz, std::memory_order_relaxed);
        mOut[Note].store(note, std::memory_order_relaxed);
        mOut[Cents].store(100.0 * (semis - std::round(semis)), std::memory_order_relaxed);
    }
}
