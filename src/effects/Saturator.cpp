#include "Saturator.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        double i0(double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        }
    }

    Saturator::Saturator() : mUp(kFactor, true)
    {
        // (SA4) the way down: a Kaiser-windowed sinc (β = 6) cut at the original Nyquist, 65 taps centred on the 33rd,
        // its gain 1 at DC — with the way up's 7.75 samples, 15 exactly
        const double c = (kDownTaps - 1) / 2.0, beta = 6.0, ib = i0(beta);
        double sum = 0.0;
        for (int m = 0; m < kDownTaps; ++m)
        {
            const double t = (m - c) / kFactor, s = std::fabs(t) < 1e-12 ? 1.0 : std::sin(kPi * t) / (kPi * t);
            const double u = 2.0 * m / (kDownTaps - 1) - 1.0;
            mDown[(size_t)m] = s * i0(beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / ib;
            sum += mDown[(size_t)m];
        }
        for (double &g : mDown) g /= sum;
        const double fs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        mDcR = std::exp(-2.0 * kPi * kDcHz / fs); // (SA5)
    }

    void Saturator::setDriveDb(Sample db) { mDrive = std::pow(10.0, db / 20.0); }
    void Saturator::setOutputDb(Sample db) { mOut = std::pow(10.0, db / 20.0); }

    void Saturator::reset()
    {
        for (auto &c : mCh) c = Channel{};
    }

    double Saturator::shape(int curve, double u)
    {
        switch (curve)
        {
        case Hard: return std::clamp(u, -1.0, 1.0);
        case Tube:
        {
            // tanh with a bias, its rest point moved back to 0 and its slope there to 1: even harmonics
            const double t = std::tanh(kTubeBias), s = 1.0 - t * t;
            return (std::tanh(u + kTubeBias) - t) / s;
        }
        case Fold: return std::sin(0.5 * kPi * u);
        default: return std::tanh(u);
        }
    }

    void Saturator::process(Sample *const *io, int channels, int frames)
    {
        for (int c = 0; c < std::min(channels, kMaxChannels); ++c)
        {
            Channel &ch = mCh[(size_t)c];
            for (int i = 0; i < frames; ++i)
            {
                const double x = io[c][i];
                // (SA6) the dry path, kLatency back
                ch.dry[(size_t)ch.dryAt] = x;
                ch.dryAt = (ch.dryAt + 1) % (kLatency + 1);
                const double dry = ch.dry[(size_t)ch.dryAt];
                // (SA1) up: four points from the interpolator's phases; (SA2) each through the curve
                std::copy_backward(ch.up.begin(), ch.up.end() - 1, ch.up.end());
                ch.up[0] = mDrive * x;
                for (int p = 0; p < kFactor; ++p)
                {
                    double u = 0.0;
                    for (int k = 0; k < TruePeakInterpolator::kTapsPerPhase; ++k) u += mUp.taps[(size_t)p][(size_t)k] * ch.up[(size_t)k];
                    const double s = shape(mCurve, u);
                    ch.ring[(size_t)ch.at] = ch.ring[(size_t)(ch.at + kDownTaps)] = s;
                    ch.at = (ch.at + 1) % kDownTaps;
                }
                // (SA4) down: the newest 65 points (the newest at the window's end)
                double d = 0.0;
                const double *w = &ch.ring[(size_t)ch.at]; // oldest first
                for (int m = 0; m < kDownTaps; ++m) d += mDown[(size_t)(kDownTaps - 1 - m)] * w[m];
                if (mCurve == Tube)
                {
                    // (SA5) its DC blocked
                    const double y = d - ch.dcX + mDcR * ch.dcY;
                    ch.dcX = d;
                    ch.dcY = y;
                    d = y;
                }
                io[c][i] = (Sample)(mOut * (mMix * d + (1.0 - mMix) * dry)); // (SA6)
            }
        }
    }
}
