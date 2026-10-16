/*
 *  Arstro DSP Library — meters (REQ-meter-1…4): what a mastering engineer reads off a mix.
 *
 *  LoudnessMeter    ITU-R BS.1770-4 loudness: K-weighted mean square, momentary (400 ms), short-term (3 s) and
 *                   integrated (gated: absolute −70 LUFS, relative −10 LU) — REQ-meter-1.
 *  TruePeakMeter    the inter-sample peak, 4× oversampled through a polyphase interpolator (BS.1770-4 Annex 2)
 *                   — REQ-meter-2.
 *  CorrelationMeter stereo correlation, −1 … +1, smoothed over 300 ms — REQ-meter-3.
 *  SpectrumAnalyser 1/6-octave band levels 20 Hz – 20 kHz: a bank of the library's Biquad band-passes — a
 *                   COMPOSITION, no new filter math — REQ-meter-4.
 *
 *  All four are analysis, not processing: they take blocks of samples (float, as hosts mix) and change nothing.
 *  They read the rate from AudioConfig when built or reset, allocate only then, and process with no allocation,
 *  no lock and no I/O, so they run on an audio thread. The math, and where each line of it is used, is in
 *  meter/README.md ## Math.
 */
#pragma once
#include "../base/AudioConfig.h"
#include "../equalizer/Biquad.h"
#include <array>
#include <memory>
#include <vector>

namespace arstro
{
    /** One second-order section in transposed direct form II, coefficients given (a0 = 1). */
    struct MeterSection
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        double run(double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    /** BS.1770-4's K-weighting at any rate: the pre-filter (a high shelf) then the RLB high-pass. */
    struct KWeighting
    {
        MeterSection shelf, highPass;
        void design(double sampleRate);
        double run(double x) { return highPass.run(shelf.run(x)); }
    };

    class LoudnessMeter
    {
    public:
        static constexpr double kSilence = -144.0;   // what a reading with no energy says (LUFS)
        explicit LoudnessMeter(int channels = 2);
        void reset();                                 // forget everything; re-read the rate
        void process(const float *left, const float *right, int frames); // right may be null (mono)
        double momentary() const;                     // LUFS, the last 400 ms
        double shortTerm() const;                     // LUFS, the last 3 s
        double integrated() const;                    // LUFS, gated, since reset
        long long blocks() const { return mBlocks; }  // gating blocks seen (400 ms, 75 % overlap)

    private:
        static double lufs(double meanSquare);
        double meanOfLast(int subBlocks) const;
        int mChannels;
        std::array<KWeighting, 2> mK;
        int mSubLen = 4800;                           // 100 ms in samples
        int mSubFill = 0;
        double mAcc = 0.0;                            // the sub-block's weighted energy so far
        std::array<double, 30> mRing{};               // the last 30 sub-blocks' mean squares (3 s)
        int mRingAt = 0, mRingCount = 0;
        long long mBlocks = 0;
        // the integrated loudness without keeping every block: a histogram, 0.01 LU a bin from −70 to +10 LUFS,
        // each bin counting its blocks and summing their mean squares (so the gated mean is exact but for the bin
        // the relative gate falls in)
        static constexpr int kBins = 8000;
        std::vector<long long> mBinCount;
        std::vector<double> mBinSum;
    };

    class TruePeakMeter
    {
    public:
        static constexpr int kFactor = 4, kTapsPerPhase = 16;
        explicit TruePeakMeter(int channels = 2);
        void reset();
        void process(const float *left, const float *right, int frames);
        double peak() const { return mPeak; }         // linear, the most since reset
        double peakDb() const;                        // dBTP
        double lastBlockDb() const;                   // dBTP of the last block processed (a meter's needle)

    private:
        int mChannels;
        std::array<std::array<double, kTapsPerPhase>, kFactor> mH{}; // the interpolator, one row a phase
        std::array<std::array<double, kTapsPerPhase>, 2> mHist{};    // each channel's last inputs, newest first
        double mPeak = 0.0, mBlockPeak = 0.0;
    };

    class CorrelationMeter
    {
    public:
        CorrelationMeter();
        void reset();
        void process(const float *left, const float *right, int frames);
        double correlation() const;                   // −1 … +1; 0 when there is nothing to compare

    private:
        double mAlpha = 0.0;                          // the one-pole's coefficient for 300 ms
        double mLR = 0.0, mLL = 0.0, mRR = 0.0;
    };

    class SpectrumAnalyser
    {
    public:
        static constexpr int kBands = 61;             // 1/6 octave, 19.7 Hz … 20.2 kHz, centred on 1 kHz
        SpectrumAnalyser();
        ~SpectrumAnalyser();
        void reset();
        void process(const float *left, const float *right, int frames); // their mean (a mono spectrum)
        static double bandHz(int band);               // a band's centre
        double bandDb(int band) const;                // its level, dB re full-scale sine; −144 above the rate's reach

    private:
        std::vector<std::unique_ptr<Biquad>> mBank;   // one band-pass per band (null above 0.45 fs)
        std::array<double, kBands> mPower{};
        double mAlpha = 0.0;                          // 300 ms, as the correlation's
    };
}
