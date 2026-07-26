#include "StringResonator.h"
#include "../base/AudioConfig.h"
#include <cmath>
#include <algorithm>

namespace arstro
{
    StringResonator::StringResonator() : SignalProcessor(propertyCount)
    {
        setSmoothEnable(false); // frequency/decay jump immediately; no click risk (struck notes, not tone knobs)
        initProperty(frequencyID, 440.0);
        initProperty(decayID, 1.0);
        ensureChannels();
        update();
    }

    void StringResonator::ensureChannels()
    {
        int n = AudioConfig::instance().channelCount();
        if ((int)mY1.size() != n)
        {
            mY1.assign(n, 0.0);
            mY2.assign(n, 0.0);
        }
    }

    void StringResonator::onChannelCountChanged() { ensureChannels(); }

    void StringResonator::reset()
    {
        // Re-added at M7. This existed before M2, which deleted it once it had lost
        // its only caller rather than keep dead code alive for coverage. It has one
        // again: LongitudinalBank (README ## 12) must clear its modes when a
        // voice-stolen note is restruck, for the same reason StringPartialBank does.
        std::fill(mY1.begin(), mY1.end(), (Sample)0);
        std::fill(mY2.begin(), mY2.end(), (Sample)0);
    }

    void StringResonator::setFrequencyHz(Sample hz) { setProperty(frequencyID, hz); }
    void StringResonator::setDecaySeconds(Sample t60) { setProperty(decayID, t60); }

    void StringResonator::setImpulseNormalized(bool impulse)
    {
        mImpulseNormalized = impulse;
        update(); // G depends on it
    }


    void StringResonator::update()
    {
        Sample sr = AudioConfig::instance().sampleRate();
        Sample f = getProperty(frequencyID);
        Sample t60 = getProperty(decayID);
        // Numerical safety clamps (not part of the physical model): keep the pole
        // below Nyquist and the decay time positive so theta/r never go singular.
        Sample nyquist = 0.49 * sr;
        if (f < 1.0) f = 1.0;
        if (f > nyquist) f = nyquist;
        if (t60 < 0.001) t60 = 0.001;

        Sample theta = 2.0 * M_PI * f / sr;
        mCosTheta = std::cos(theta);
        mR = std::pow(10.0, -3.0 / (t60 * sr));
        // See setImpulseNormalized(): struck partials must not get quieter merely
        // because they ring longer, which (1-r^2) would impose.
        mG = mImpulseNormalized ? std::sin(theta) : (1.0 - mR * mR) * std::sin(theta);

        if (std::isnan(mR) || std::isinf(mR)) mR = 0.0;
        if (std::isnan(mG) || std::isinf(mG)) mG = 0.0;
    }

    Sample StringResonator::process(Sample in, int channel)
    {
        if (channel < 0 || channel >= (int)mY1.size())
            return 0.0;
        Sample y = 2.0 * mR * mCosTheta * mY1[channel] - mR * mR * mY2[channel] + mG * in;
        y = arstroFlush(y);
        mY2[channel] = mY1[channel];
        mY1[channel] = y;
        return y;
    }
}
