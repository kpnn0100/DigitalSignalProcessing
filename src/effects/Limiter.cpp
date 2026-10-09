#include "Limiter.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    Limiter::Limiter()
    {
        prepare();
        setCeilingDb(-0.3);
        setReleaseMs(60.0);
        setLookaheadMs(2.0);
    }

    void Limiter::prepare()
    {
        const AudioConfig &cfg = AudioConfig::instance();
        mSampleRate = cfg.sampleRate();
        mCap = (int)std::ceil(kMaxLookaheadMs * 0.001 * mSampleRate) + 1;
        mDelay.assign((size_t)std::max(1, cfg.channelCount()), std::vector<Sample>((size_t)mCap, 0.0));
        mDqVal.assign((size_t)mCap, 1.0);
        mDqIdx.assign((size_t)mCap, 0);
        mBox.assign((size_t)mCap, 1.0);
        setReleaseMs(mReleaseMs);
        mL = -1; // force the window to be re-cut at this rate
        setLookaheadMs(mLookMs);
    }

    void Limiter::setGainDb(Sample db) { mDrive = std::pow(10.0, db / 20.0); }
    void Limiter::setCeilingDb(Sample db) { mCeiling = std::pow(10.0, std::min<Sample>(db, 0.0) / 20.0); }

    void Limiter::setReleaseMs(Sample ms)
    {
        mReleaseMs = std::max<Sample>(ms, 0.0);
        const double n = mReleaseMs * 0.001 * mSampleRate;
        mRelCoeff = n > 0.0 ? std::exp(-1.0 / n) : 0.0; // (L3)
    }

    void Limiter::setLookaheadMs(Sample ms)
    {
        mLookMs = std::clamp<double>(ms, 0.0, kMaxLookaheadMs);
        const int L = std::min(mCap - 1, (int)std::lround(mLookMs * 0.001 * mSampleRate));
        if (L == mL) return;
        mL = L;
        reset(); // a new window: start clean rather than average two lengths
    }

    void Limiter::reset()
    {
        for (auto &d : mDelay) std::fill(d.begin(), d.end(), 0.0);
        std::fill(mBox.begin(), mBox.end(), 1.0);
        mBoxSum = (Sample)(mL + 1);
        mBoxPos = mPos = mDqHead = mDqSize = 0;
        mN = 0;
        mRel = mLast = 1.0;
    }

    void Limiter::process(Sample *const *io, int channels, int frames)
    {
        const int nc = std::min(channels, (int)mDelay.size());
        const int span = mL + 1;
        for (int i = 0; i < frames; ++i, ++mN)
        {
            // (L1) the gain this frame needs, all channels linked
            Sample peak = 0.0;
            for (int c = 0; c < nc; ++c) peak = std::max(peak, std::fabs(io[c][i] * mDrive));
            const Sample r = peak > mCeiling ? mCeiling / peak : 1.0;
            // (L2) the minimum over the last L + 1 frames — a monotonic deque in a ring
            while (mDqSize > 0 && mDqVal[(size_t)((mDqHead + mDqSize - 1) % mCap)] >= r) --mDqSize;
            const int tail = (mDqHead + mDqSize) % mCap;
            mDqVal[(size_t)tail] = r;
            mDqIdx[(size_t)tail] = mN;
            ++mDqSize;
            while (mDqIdx[(size_t)mDqHead] < mN - mL) { mDqHead = (mDqHead + 1) % mCap; --mDqSize; }
            const Sample m = mDqVal[(size_t)mDqHead];
            // (L3) released: never above the minimum, rising back toward 1
            mRel = std::min(m, 1.0 - (1.0 - mRel) * mRelCoeff);
            // (L4) the box average of the last L + 1 released minima — re-summed exactly once a lap
            mBoxSum += mRel - mBox[(size_t)mBoxPos];
            mBox[(size_t)mBoxPos] = mRel;
            if (++mBoxPos == span)
            {
                mBoxPos = 0;
                mBoxSum = 0.0;
                for (int k = 0; k < span; ++k) mBoxSum += mBox[(size_t)k];
            }
            const Sample g = mBoxSum / span;
            mLast = g;
            // (L5) the frame from L ago, at that gain; the clamp only ever meets rounding error
            const int read = (mPos - mL + mCap) % mCap;
            bool over = false;
            for (int c = 0; c < nc; ++c)
            {
                std::vector<Sample> &d = mDelay[(size_t)c];
                d[(size_t)mPos] = io[c][i] * mDrive;
                const Sample y = d[(size_t)read] * g;
                over |= std::fabs(y) > mCeiling * (1.0 + 1e-12);
                io[c][i] = std::clamp(y, -mCeiling, mCeiling);
            }
            mClamped += over ? 1 : 0;
            mPos = (mPos + 1) % mCap;
        }
    }
}
