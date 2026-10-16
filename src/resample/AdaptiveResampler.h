#pragma once
/*
 *  AdaptiveResampler — a stream read `step` input frames an output frame, the step free to change every block with no
 *  click (REQ-resample-2): what holds one device's sound to another's clock (Solaris R-DEV-2). The kernel is
 *  REQ-resample-1's (SincKernel, c = 1, 32 taps), its weights normalised. src/resample/README.md (A1)–(A4).
 *
 *  Sized when built (it allocates); `process` allocates nothing, locks nothing — an audio thread may run it.
 *  Interleaved float, any channel count.
 */
#include "SincKernel.h"
#include <vector>

namespace arstro
{
    class AdaptiveResampler
    {
    public:
        /** For blocks of up to `maxOut` output frames at steps up to `maxStep`. */
        AdaptiveResampler(int channels, int maxOut, double maxStep = 1.01);
        int channels() const { return mChannels; }
        /** (A4) The frames it holds beyond the one it plays: R — a constant delay. */
        int lookahead() const { return mR; }
        /** (A5) The frames given and not yet read past, fractional: what it holds of a buffer's fill. */
        double buffered() const { return (double)(mBase + mHeld) - ((double)mPos + mPhase); }
        /** (A3) The input frames the next `process(…, outFrames, step)` reads — exactly. */
        int inputNeeded(int outFrames, double step) const;
        /** (A1)–(A2) `inFrames` (= inputNeeded(outFrames, step)) new frames in, `outFrames` out. A step outside
         *  (0, maxStep] is clamped to it; a block past what it was sized for is cut to `maxOut`. */
        void process(const float *in, int inFrames, float *out, int outFrames, double step);
        /** Back to the start: silence behind frame 0, the position at 0. */
        void reset();

    private:
        SincKernel mKernel;
        int mChannels, mMaxOut, mR;
        double mMaxStep;
        std::vector<float> mBuf;  // held frames, interleaved: absolute frames mBase … mBase + mHeld − 1
        long long mBase = 0;      // the absolute index of mBuf's first frame
        int mHeld = 0;
        long long mPos = 0;       // (A1) n: the read position's whole frames
        double mPhase = 0.0;      // (A1) φ: its fraction, 0 ≤ φ < 1
        std::vector<double> mW;   // one output's 2R weights
    };
}
