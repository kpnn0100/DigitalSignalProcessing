/*
 *  Arstro DSP Library — Saturator: a curve driven, four times oversampled (REQ-fx-saturator-1). Soft (tanh), hard (a
 *  clip), tube (tanh with a bias: even harmonics, its DC blocked) or fold (a sine folder); up through the true-peak
 *  meter's interpolator (README meter (T1)–(T2), on-samples), down through a Kaiser-windowed sinc of the same kind —
 *  a path exactly kLatency samples late, its dry path delayed to match, the latency reported for compensation.
 *  effects/README.md, Math — Saturator (SA1)–(SA6). Whole frames; no allocation (up to kMaxChannels; beyond them a
 *  channel passes untouched).
 */
#pragma once
#include "../base/Sample.h"
#include "../meter/Meters.h"
#include <array>

namespace arstro
{
    class Saturator
    {
    public:
        enum Curve { Soft = 0, Hard, Tube, Fold };
        static constexpr int kFactor = 4, kDownTaps = 65, kLatency = 15, kMaxChannels = 8;
        static constexpr double kTubeBias = 0.25, kDcHz = 5.0;
        Saturator();
        void setDriveDb(Sample db);
        void setCurve(int curve) { mCurve = curve < 0 ? 0 : curve > Fold ? Fold : curve; }
        void setMix(Sample mix) { mMix = mix < 0 ? 0 : mix > 1 ? 1 : mix; }
        void setOutputDb(Sample db);
        void process(Sample *const *io, int channels, int frames);
        void reset();
        int latency() const { return kLatency; }
        /** (SA2) the curve itself, at the oversampled rate's point u = drive·x. */
        static double shape(int curve, double u);

    private:
        TruePeakInterpolator mUp;
        std::array<double, kDownTaps> mDown{}; // (SA4) the decimator's taps
        struct Channel
        {
            TruePeakInterpolator::History up{};        // its last inputs, newest first
            std::array<double, 2 * kDownTaps> ring{}; // its shaped points, twice over (a window always whole)
            int at = 0;
            std::array<double, kLatency + 1> dry{};   // its inputs, kLatency back
            int dryAt = 0;
            double dcX = 0, dcY = 0;                  // (SA5)
        };
        std::array<Channel, kMaxChannels> mCh{};
        int mCurve = Soft;
        double mDrive = 1.9952623149688795, mMix = 1, mOut = 1, mDcR = 0;
    };
}
