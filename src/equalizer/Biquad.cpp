#include "Biquad.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    Biquad::Biquad() : Biquad(Peak, 1000.0, 0.7071, 0.0) {}

    Biquad::Biquad(Type type, Sample hz, Sample q, Sample gainDb) : SignalProcessor(propertyCount), mType(type)
    {
        initProperty(frequencyID, hz);
        initProperty(qID, q);
        initProperty(gainDbID, gainDb);
        ensureChannels();
        update();
    }

    void Biquad::ensureChannels()
    {
        const int n = AudioConfig::instance().channelCount();
        if ((int)mZ1.size() != n)
        {
            mZ1.assign(n, 0.0);
            mZ2.assign(n, 0.0);
        }
    }
    void Biquad::onChannelCountChanged() { ensureChannels(); }
    void Biquad::onSampleRateChanged() { update(); }

    void Biquad::setType(Type type)
    {
        mType = type;
        update();
    }
    void Biquad::setFrequency(Sample hz) { setProperty(frequencyID, hz); }
    void Biquad::setQ(Sample q) { setProperty(qID, q); }
    void Biquad::setGainDb(Sample db) { setProperty(gainDbID, db); }

    void Biquad::reset()
    {
        std::fill(mZ1.begin(), mZ1.end(), 0.0);
        std::fill(mZ2.begin(), mZ2.end(), 0.0);
    }

    Biquad::Coeffs Biquad::design(Type type, Sample hz, Sample q, Sample gainDb, Sample sampleRate)
    {
        // README ## Math — the RBJ cookbook, eq. (B1)…(B8).
        const Sample fs = sampleRate > 0 ? sampleRate : 48000.0;
        const Sample f = std::clamp(hz, (Sample)10.0, (Sample)(0.49 * fs));
        const Sample Q = std::clamp(q, (Sample)0.1, (Sample)40.0);
        const Sample w0 = 2.0 * M_PI * f / fs;
        const Sample cw = std::cos(w0), sw = std::sin(w0);
        const Sample alpha = sw / (2.0 * Q);
        const Sample A = std::pow(10.0, gainDb / 40.0);
        const Sample sqA2a = 2.0 * std::sqrt(A) * alpha;

        Sample b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch (type)
        {
        case LowPass:
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case HighPass:
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case BandPass: // constant 0 dB peak gain
            b0 = alpha; b1 = 0; b2 = -alpha;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case Notch:
            b0 = 1; b1 = -2 * cw; b2 = 1;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case Peak:
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
            break;
        case LowShelf:
            b0 = A * ((A + 1) - (A - 1) * cw + sqA2a);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - sqA2a);
            a0 = (A + 1) + (A - 1) * cw + sqA2a;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - sqA2a;
            break;
        case HighShelf:
            b0 = A * ((A + 1) + (A - 1) * cw + sqA2a);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - sqA2a);
            a0 = (A + 1) - (A - 1) * cw + sqA2a;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - sqA2a;
            break;
        }
        Coeffs c;
        c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0;
        c.a1 = a1 / a0; c.a2 = a2 / a0;
        return c;
    }

    Sample Biquad::magnitudeDb(const Coeffs &c, Sample hz, Sample sampleRate)
    {
        // README ## Math (B9): evaluate H(z) on the unit circle, z = e^{jω}.
        const Sample w = 2.0 * M_PI * hz / sampleRate;
        const Sample c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        const Sample nr = c.b0 + c.b1 * c1 + c.b2 * c2, ni = -(c.b1 * s1 + c.b2 * s2);
        const Sample dr = 1.0 + c.a1 * c1 + c.a2 * c2, di = -(c.a1 * s1 + c.a2 * s2);
        const Sample mag2 = (nr * nr + ni * ni) / (dr * dr + di * di);
        return 10.0 * std::log10(std::max(mag2, (Sample)1e-30));
    }

    Sample Biquad::magnitudeDbAt(Sample hz)
    {
        const Sample fs = AudioConfig::instance().sampleRate();
        return magnitudeDb(design(mType, getPropertyTargetValue(frequencyID), getPropertyTargetValue(qID),
                                  getPropertyTargetValue(gainDbID), fs),
                           hz, fs);
    }

    void Biquad::update()
    {
        mC = design(mType, getProperty(frequencyID), getProperty(qID), getProperty(gainDbID),
                    AudioConfig::instance().sampleRate());
    }

    Sample Biquad::process(Sample in, int channel)
    {
        if (channel < 0 || channel >= (int)mZ1.size())
            return in;
        // README ## Math (B10): transposed direct form II.
        Sample &z1 = mZ1[channel], &z2 = mZ2[channel];
        const Sample y = mC.b0 * in + z1;
        z1 = arstroFlush(mC.b1 * in - mC.a1 * y + z2);
        z2 = arstroFlush(mC.b2 * in - mC.a2 * y);
        return y;
    }
}
