#include "Shaper.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kPumpTau = 0.18; // (SH2) the pump's recovery, a fraction of its cycle — tuned to a sidechain's feel
    }

    double Shaper::beatsOfRate(int rate)
    {
        // (SH1) a note value in beats (a beat a quarter note): straight, triplet (⅔), dotted (3/2)
        static constexpr double kBeats[kRates] = {4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75};
        return kBeats[std::clamp(rate, 0, kRates - 1)];
    }

    Shaper::Shaper()
    {
        mFs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        mSpb = 60.0 / 120.0 * mFs;
        mGain.fill(1.0);
    }

    void Shaper::setSmoothMs(Sample ms)
    {
        // (SH4) a one-pole glide: α = 1 − e^(−1/(fs·τ)), τ the time constant in seconds
        mAlpha = ms <= 0 ? 1.0 : 1.0 - std::exp(-1.0 / (mFs * (double)ms / 1000.0));
    }

    void Shaper::setTransport(long long sample, double samplesPerBeat, double beatOffset)
    {
        mSample = sample;
        mSpb = samplesPerBeat > 1.0 ? samplesPerBeat : 1.0;
        mOffset = beatOffset;
    }

    void Shaper::reset()
    {
        mGain.fill(1.0);
        mPlaced = false;
    }

    double Shaper::shapeAt(Shape s, double phi)
    {
        switch (s) // (SH2) each 0 at the start of its cycle — where the beat falls, the sound is lowest
        {
        case Shape::Sine: return 0.5 - 0.5 * std::cos(2.0 * kPi * phi);
        case Shape::Triangle: return 1.0 - std::fabs(2.0 * phi - 1.0);
        case Shape::SawDown: return 1.0 - phi; // open on the beat, closing through the cycle
        case Shape::SawUp: return phi;
        case Shape::Square: return phi < 0.5 ? 0.0 : 1.0;
        case Shape::Pump: return (1.0 - std::exp(-phi / kPumpTau)) / (1.0 - std::exp(-1.0 / kPumpTau));
        }
        return 1.0;
    }

    void Shaper::process(Sample *const *io, int channels, int frames)
    {
        const int n = std::min(channels, kMaxChannels);
        for (int i = 0; i < frames; ++i)
        {
            // (SH1) the beat at this sample, from the song's place — (sample + i) whole, the same however cut
            const double beat = mOffset + (double)(mSample + i) / mSpb;
            const double cycle = beat / mBeats + mPhase;
            for (int c = 0; c < n; ++c)
            {
                double phi = cycle + ((c & 1) ? mStereo : 0.0);
                phi -= std::floor(phi);
                const double g = 1.0 - mDepth * (1.0 - shapeAt(mShape, phi)); // (SH3)
                double &gs = mGain[(std::size_t)c];
                gs = mPlaced ? gs + mAlpha * (g - gs) : g; // (SH4)
                io[c][i] = (Sample)(io[c][i] * gs);        // (SH5)
            }
            mPlaced = true;
        }
        mSample += frames; // a host that does not say where the song is: its own count goes on
    }
}
