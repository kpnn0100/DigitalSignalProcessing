#include "Tuner.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    Tuner::Tuner() : mYin(kWindow)
    {
        mFs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        mRing.assign(kWindow, 0.0);
        mX.assign(kWindow, 0.0);
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
        const int W = kWindow;
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
        // (Y1)…(Y4): the period, by the library's YIN (shared with the transcriber, REQ-analysis-3)
        const double t = mYin.period(mX.data(), mFs, kMinHz, kMaxHz, kThreshold);
        if (t <= 0.0) { none(); return; }
        // (Y6) the frequency; the nearest note and its cents against the reference
        const double hz = mFs / t, semis = 12.0 * std::log2(hz / mRef);
        const double note = 69.0 + std::round(semis);
        mOut[Frequency].store(hz, std::memory_order_relaxed);
        mOut[Note].store(note, std::memory_order_relaxed);
        mOut[Cents].store(100.0 * (semis - std::round(semis)), std::memory_order_relaxed);
    }
}
