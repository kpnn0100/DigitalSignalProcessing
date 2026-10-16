#include "AdaptiveResampler.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace arstro
{
    AdaptiveResampler::AdaptiveResampler(int channels, int maxOut, double maxStep)
        : mKernel(1.0, 16), mChannels(std::max(1, channels)), mMaxOut(std::max(1, maxOut)), mR((int)std::ceil(mKernel.halfWidth())),
          mMaxStep(std::max(1e-3, maxStep))
    {
        // held: 2R behind and ahead of a block, plus the most a block can read
        const int cap = 2 * mR + (int)std::ceil(mMaxOut * mMaxStep) + 2;
        mBuf.assign((size_t)cap * (size_t)mChannels, 0.0f);
        mW.assign((size_t)(2 * mR), 0.0);
        reset();
    }

    void AdaptiveResampler::reset()
    {
        // (A4) R frames of silence behind frame 0
        std::fill(mBuf.begin(), mBuf.end(), 0.0f);
        mBase = -mR;
        mHeld = mR;
        mPos = 0;
        mPhase = 0.0;
    }

    int AdaptiveResampler::inputNeeded(int outFrames, double step) const
    {
        outFrames = std::min(std::max(0, outFrames), mMaxOut);
        if (outFrames == 0) return 0;
        step = std::min(std::max(step, 1e-9), mMaxStep);
        // (A3) the last frame read: n + ⌊φ + (N−1)·s⌋ + R
        const long long last = mPos + (long long)std::floor(mPhase + (outFrames - 1) * step) + mR;
        return (int)std::max(0LL, last - (mBase + mHeld - 1));
    }

    void AdaptiveResampler::process(const float *in, int inFrames, float *out, int outFrames, double step)
    {
        outFrames = std::min(std::max(0, outFrames), mMaxOut);
        step = std::min(std::max(step, 1e-9), mMaxStep);
        const int C = mChannels;
        // the new frames after what is held
        const int cap = (int)(mBuf.size() / (size_t)C);
        inFrames = std::max(0, std::min(inFrames, cap - mHeld));
        if (inFrames > 0) std::memcpy(&mBuf[(size_t)mHeld * C], in, sizeof(float) * (size_t)inFrames * (size_t)C);
        mHeld += inFrames;
        for (int j = 0; j < outFrames; ++j)
        {
            // (A1) t_j = φ + j·s; (A2) the 2R frames around n + ⌊t_j⌋, weighted by h(f − d), normalised
            const double t = mPhase + j * step, ft = std::floor(t), f = t - ft;
            const long long centre = mPos + (long long)ft;
            double sum = 0.0;
            for (int d = -mR + 1; d <= mR; ++d) sum += (mW[(size_t)(d + mR - 1)] = mKernel(f - d));
            const double norm = sum != 0.0 ? 1.0 / sum : 0.0;
            const long long first = centre - mR + 1 - mBase; // its first frame, in mBuf
            for (int c = 0; c < C; ++c)
            {
                double acc = 0.0;
                for (int k = 0; k < 2 * mR; ++k)
                {
                    const long long at = first + k;
                    if (at >= 0 && at < mHeld) acc += mW[(size_t)k] * mBuf[(size_t)at * C + c]; // a frame not given: silence
                }
                out[(size_t)j * C + c] = (float)(acc * norm);
            }
        }
        // (A1) on: P ← P + N·s; then drop what no later output reaches (frames below n − R + 1)
        const double t = mPhase + outFrames * step, ft = std::floor(t);
        mPos += (long long)ft;
        mPhase = t - ft;
        const long long keepFrom = mPos - mR + 1;
        const int drop = (int)std::max(0LL, std::min<long long>(keepFrom - mBase, mHeld));
        if (drop > 0)
        {
            std::memmove(mBuf.data(), &mBuf[(size_t)drop * C], sizeof(float) * (size_t)(mHeld - drop) * (size_t)C);
            mHeld -= drop;
            mBase += drop;
        }
    }
}
