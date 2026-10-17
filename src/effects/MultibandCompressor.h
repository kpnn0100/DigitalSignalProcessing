/*
 *  Arstro DSP Library — MultibandCompressor: three bands, each compressed alone (REQ-fx-multiband-1).
 *
 *  Linkwitz-Riley 4th-order crossovers (Biquad::design's Butterworth pairs) split low / mid / high, the low band run
 *  through the upper crossover's all-pass so the three sum flat; each band its own library Compressor; an output gain.
 *  effects/README.md, Math — MultibandCompressor (MB1)–(MB4). A whole frame at a time, channel by channel; no
 *  allocation after construction (blocks of up to kMaxBlock, longer ones in pieces).
 */
#pragma once
#include "Compressor.h"
#include "../base/Sample.h"
#include <array>
#include <cmath>
#include <vector>

namespace arstro
{
    class MultibandCompressor
    {
    public:
        static constexpr int kBands = 3, kMaxBlock = 1024;
        MultibandCompressor();
        void setSplits(Sample lowHz, Sample highHz);
        void setThresholdDb(int band, Sample db);
        void setRatio(int band, Sample ratio);
        void setGainDb(int band, Sample db);
        void setAttackMs(Sample ms);
        void setReleaseMs(Sample ms);
        void setOutputDb(Sample db) { mOut = std::pow(10.0, db / 20.0); }
        Sample lowSplit() const { return mF1; }
        Sample highSplit() const { return mF2; }
        void process(Sample *const *io, int channels, int frames);
        void reset();

    private:
        struct Section
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
            std::vector<double> z1, z2; // per channel
            double run(double x, int c)
            {
                const double y = b0 * x + z1[(size_t)c];
                z1[(size_t)c] = b1 * x - a1 * y + z2[(size_t)c];
                z2[(size_t)c] = b2 * x - a2 * y;
                return y;
            }
        };
        void design();
        void size(int channels);
        // (MB1) each a Linkwitz-Riley pair: two identical Butterworth sections
        Section mLp1[2], mHp1[2], mLp2[2], mHp2[2], mApLp[2], mApHp[2];
        std::array<Compressor, kBands> mComp;
        Sample mF1 = 200.0, mF2 = 2500.0, mOut = 1.0;
        int mChannels = 0;
        std::vector<Sample> mBand[kBands];
    };
}
