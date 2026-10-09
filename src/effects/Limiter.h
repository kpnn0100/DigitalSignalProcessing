/*
 *  Arstro DSP Library — Limiter: a brickwall lookahead limiter (REQ-fx-limiter-1).
 *
 *  The output never exceeds the ceiling — by construction, not by tuning (README §Limiter, (L1)–(L5)):
 *  the audio is delayed by the lookahead L, and the gain applied to each delayed frame is an average
 *  of L + 1 values each at most the gain THAT frame needed, so the gain glides down over L samples
 *  before a peak and the peak still fits. Channels are linked (one gain for all), so the stereo image
 *  does not move. Its latency is L samples.
 *
 *  Not a SignalProcessor: linking needs every channel of a frame at once, and SignalProcessor runs one
 *  channel's block at a time. Not a composition: the library's Delay is fractional and modulated;
 *  this needs an integer delay, a sliding-window minimum and a running box average — none exists.
 *  Allocates only in the constructor and `prepare` (sized for the longest lookahead).
 */
#pragma once
#include "../base/Sample.h"
#include <vector>

namespace arstro
{
    class Limiter
    {
    public:
        static constexpr double kMaxLookaheadMs = 10.0;

        Limiter();                       // sized for AudioConfig's sample rate and channel count
        void prepare();                  // re-size after a sample-rate or channel change (allocates)

        void setGainDb(Sample db);       // drive into it
        void setCeilingDb(Sample db);    // dBFS, ≤ 0
        void setReleaseMs(Sample ms);
        void setLookaheadMs(Sample ms);  // 0 … kMaxLookaheadMs; a change clears its memory

        int latency() const { return mL; }        // samples
        Sample lastGain() const { return mLast; } // the gain applied to the last frame (linear)
        /** Frames where the final clamp did more than round — (L1)–(L4) failing. Zero, always. */
        long long clamped() const { return mClamped; }

        /** In place; channels beyond those prepared pass through untouched. */
        void process(Sample *const *io, int channels, int frames);
        void reset();

    private:
        Sample mDrive = 1.0, mCeiling = 1.0, mRelCoeff = 0.0, mLast = 1.0;
        double mLookMs = 2.0;
        int mCap = 1, mL = 0, mPos = 0, mBoxPos = 0;
        long long mN = 0, mClamped = 0;
        std::vector<std::vector<Sample>> mDelay;  // per channel, a ring of mCap
        std::vector<Sample> mDqVal;               // the window minimum's deque, a ring of mCap
        std::vector<long long> mDqIdx;
        int mDqHead = 0, mDqSize = 0;
        Sample mRel = 1.0;                        // (L3) the released minimum
        std::vector<Sample> mBox;                 // (L4) its last L + 1 values
        Sample mBoxSum = 1.0;
        double mSampleRate = 48000.0, mReleaseMs = 60.0;
    };
}
