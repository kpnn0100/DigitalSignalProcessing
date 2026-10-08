#include "StateVariableFilter.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        // (S1): the prewarped integrator gain. Clamped below Nyquist, where tan() runs away.
        Sample gainFor(Sample hz)
        {
            const Sample fs = AudioConfig::instance().sampleRate();
            const Sample f = std::clamp(hz, (Sample)10.0, (Sample)(0.45 * fs));
            return std::tan(M_PI * f / fs);
        }
    }

    StateVariableFilter::StateVariableFilter() : StateVariableFilter(LowPass, 1000.0, 0.0) {}

    StateVariableFilter::StateVariableFilter(Mode mode, Sample cutoffHz, Sample resonance)
        : SignalProcessor(propertyCount), mMode(mode)
    {
        initProperty(cutoffID, cutoffHz);
        initProperty(resonanceID, std::clamp(resonance, (Sample)0.0, (Sample)1.0));
        ensureChannels();
        update();
    }

    void StateVariableFilter::ensureChannels()
    {
        const int n = AudioConfig::instance().channelCount();
        if ((int)mIc1.size() != n)
        {
            mIc1.assign(n, 0.0);
            mIc2.assign(n, 0.0);
        }
    }
    void StateVariableFilter::onChannelCountChanged() { ensureChannels(); }
    void StateVariableFilter::onSampleRateChanged() { update(); }

    void StateVariableFilter::setCutoff(Sample hz) { setProperty(cutoffID, hz); }
    void StateVariableFilter::setResonance(Sample r) { setProperty(resonanceID, std::clamp(r, (Sample)0.0, (Sample)1.0)); }

    void StateVariableFilter::reset()
    {
        std::fill(mIc1.begin(), mIc1.end(), 0.0);
        std::fill(mIc2.begin(), mIc2.end(), 0.0);
    }

    Sample StateVariableFilter::qFor(Sample resonance)
    {
        // (S5): exponential, so equal knob travel is an equal ratio of Q — 0.707 (Butterworth,
        // no peak) at 0 up to 32 (a ringing whistle, still stable) at 1.
        return 0.70710678 * std::pow(2.0, 5.5 * std::clamp(resonance, (Sample)0.0, (Sample)1.0));
    }

    void StateVariableFilter::update()
    {
        mG = gainFor(getProperty(cutoffID));
        mK = 1.0 / qFor(getProperty(resonanceID));
    }

    Sample StateVariableFilter::run(Sample v0, int channel, Sample g)
    {
        // (S2)…(S4): Simper's TPT SVF, solved for one sample.
        Sample &ic1 = mIc1[channel], &ic2 = mIc2[channel];
        const Sample a1 = 1.0 / (1.0 + g * (g + mK));
        const Sample a2 = g * a1;
        const Sample a3 = g * a2;
        const Sample v3 = v0 - ic2;
        const Sample v1 = a1 * ic1 + a2 * v3;
        const Sample v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = arstroFlush(2.0 * v1 - ic1);
        ic2 = arstroFlush(2.0 * v2 - ic2);
        switch (mMode)
        {
        case LowPass: return v2;
        case BandPass: return v1;
        case HighPass: return v0 - mK * v1 - v2;
        case Notch: return v0 - mK * v1;
        }
        return v2;
    }

    Sample StateVariableFilter::process(Sample in, int channel)
    {
        if (channel < 0 || channel >= (int)mIc1.size())
            return in;
        return run(in, channel, mG);
    }

    Sample StateVariableFilter::tick(Sample in, int channel, Sample cutoffHz)
    {
        if (channel < 0 || channel >= (int)mIc1.size())
            return in;
        return run(in, channel, gainFor(cutoffHz));
    }
}
