#include "Chorus.h"
#include "../base/AudioConfig.h"
#include <cmath>

namespace gyrus_space
{
    Chorus::Chorus() : SignalProcessor(propertyCount)
    {
        setSmoothEnable(false);
        Sample sr = AudioConfig::instance().sampleRate();
        initProperty(rateID, 1.5 / sr);        // 1.5 Hz
        initProperty(depthID, 0.003 * sr);     // 3 ms depth
        initProperty(baseDelayID, 0.012 * sr); // 12 ms center
        initProperty(mixID, 0.5);
        ensureChannels();
    }

    void Chorus::ensureChannels()
    {
        int n = AudioConfig::instance().channelCount();
        if ((int)mDelay.size() == n)
            return;
        Sample sr = AudioConfig::instance().sampleRate();
        int maxDelay = (int)(0.060 * sr) + 4; // 60 ms headroom
        mDelay.clear();
        mDelay.resize(n);
        for (auto &d : mDelay)
        {
            d.setSmoothEnable(false);
            d.setMaxDelay(maxDelay);
            d.prepare();
        }
        mPhase.assign(n, 0.0);
        // Offset channel phases for stereo width.
        for (int c = 0; c < n; ++c)
            mPhase[c] = (Sample)c / (Sample)n * 0.5;
    }
    void Chorus::onChannelCountChanged() { ensureChannels(); }

    void Chorus::setRateHz(Sample hz) { setProperty(rateID, hz / AudioConfig::instance().sampleRate()); }
    void Chorus::setDepthMs(Sample ms) { setProperty(depthID, ms * 0.001 * AudioConfig::instance().sampleRate()); }
    void Chorus::setBaseDelayMs(Sample ms) { setProperty(baseDelayID, ms * 0.001 * AudioConfig::instance().sampleRate()); }
    void Chorus::setMix(Sample mix) { setProperty(mixID, mix); }

    Sample Chorus::process(Sample in, int channel)
    {
        if (channel < 0 || channel >= (int)mDelay.size())
            return in;
        Sample lfo = std::sin(2.0 * M_PI * mPhase[channel]);
        Sample delaySamples = getProperty(baseDelayID) + getProperty(depthID) * lfo;
        if (delaySamples < 1.0)
            delaySamples = 1.0;

        Sample delayed = mDelay[channel].read(delaySamples);
        mDelay[channel].write(in);

        // advance LFO phase (sample-count based)
        mPhase[channel] += getProperty(rateID);
        if (mPhase[channel] >= 1.0)
            mPhase[channel] -= 1.0;

        Sample mix = getProperty(mixID);
        return (1.0 - mix) * in + mix * delayed;
    }
}
