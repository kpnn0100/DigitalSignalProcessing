/*
 *  Arstro DSP Library — Convolver: a reverb that is a recorded room (REQ-fx-convolution-1). An impulse response,
 *  handed in as a sample (the host decodes the file; the library never opens one), resampled to the engine's rate
 *  (REQ-resample-1's resample), cut at kMaxSeconds, scaled to unit energy, convolved by uniform partitioned
 *  overlap-save through the library FFT — kBlock-sample partitions, so the wet path is exactly kBlock samples late:
 *  latency() says so and the dry path is delayed to match. A mono response serves both channels; a stereo one, each
 *  its own. effects/README.md, Math — Convolver (CV1)–(CV6). setSample allocates (a host calls it before playing);
 *  process never does. Up to kMaxChannels; beyond them a channel passes untouched.
 */
#pragma once
#include "../base/Sample.h"
#include "../spectral/FFT.h"
#include <array>
#include <complex>
#include <vector>

namespace arstro
{
    class Convolver
    {
    public:
        static constexpr int kBlock = 256, kMaxChannels = 2;
        static constexpr double kMaxSeconds = 10.0;
        Convolver();
        /** The impulse response: `frames` interleaved frames of `channels` at `rate` Hz (nullptr or 0: none). */
        void setSample(const float *interleaved, long long frames, int channels, double rate);
        void setMix(Sample mix) { mMix = mix < 0 ? 0 : mix > 1 ? 1 : mix; }
        void setLevelDb(Sample db);
        void process(Sample *const *io, int channels, int frames);
        void reset();
        int latency() const { return kBlock; }
        /** The response's length as used, in samples at the engine's rate (for tests). */
        long long responseFrames() const { return mFrames; }

    private:
        void block(int c);
        FFT mFft;
        double mFs = 48000, mMix = 0.3, mLevel = 1.0;
        int mParts = 0, mRespCh = 0, mPos = 0, mHead = 0;
        long long mFrames = 0;
        std::vector<std::vector<std::complex<double>>> mH; // (CV3) [response channel][part·(B+1) + k]
        struct Channel
        {
            std::vector<double> in, out, dry;               // 2B input window, B wet, B dry (late by B)
            std::vector<std::complex<double>> fdl, work;    // (CV4) P spectra of B+1 bins; 2B scratch
        };
        std::array<Channel, kMaxChannels> mCh;
    };
}
