#include "Repeater.h"
#include "../base/AudioConfig.h"

namespace gyrus_space
{
    Repeater::Repeater() : SignalProcessor(propertyCount)
    {
        setSmoothEnable(false);
        Sample sr = AudioConfig::instance().sampleRate();
        initProperty(delayID, 0.300 * sr); // 300 ms
        initProperty(feedbackID, 0.5);     // 50% volume per repeat
        initProperty(toneCutoffID, 4000.0);
        initProperty(mixID, 0.4);
        ensureChannels();
        update();
    }

    void Repeater::ensureChannels()
    {
        int n = AudioConfig::instance().channelCount();
        if ((int)mDelay.size() == n)
            return;
        Sample sr = AudioConfig::instance().sampleRate();
        int maxDelay = (int)(2.0 * sr) + 4; // up to 2 s echo spacing
        mDelay.clear();
        mDelay.resize(n);
        for (auto &d : mDelay)
        {
            d.setSmoothEnable(false);
            d.setMaxDelay(maxDelay);
            d.prepare();
        }
        mTone.clear();
        mTone.resize(n);
        for (auto &f : mTone)
        {
            f.setSmoothEnable(false);
            f.setCutoffFrequency(getProperty(toneCutoffID));
        }
    }
    void Repeater::onChannelCountChanged() { ensureChannels(); }

    void Repeater::setDelayMs(Sample ms) { setProperty(delayID, ms * 0.001 * AudioConfig::instance().sampleRate()); }
    void Repeater::setFeedback(Sample fraction)
    {
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 0.99) fraction = 0.99; // keep the loop stable
        setProperty(feedbackID, fraction);
    }
    void Repeater::setToneCutoffHz(Sample hz) { setProperty(toneCutoffID, hz); }
    void Repeater::setMix(Sample mix) { setProperty(mixID, mix); }

    void Repeater::update()
    {
        for (auto &f : mTone)
            f.setCutoffFrequency(getProperty(toneCutoffID));
    }

    Sample Repeater::process(Sample in, int channel)
    {
        if (channel < 0 || channel >= (int)mDelay.size())
            return in;
        Sample delaySamples = getProperty(delayID);
        if (delaySamples < 1.0)
            return in;

        Sample delayed = mDelay[channel].read(delaySamples);
        // Low-pass in the feedback path => each repeat is darker than the last.
        Sample filtered = mTone[channel].out(delayed, 0);
        Sample newSample = in + filtered * getProperty(feedbackID);
        mDelay[channel].write(newSample);

        Sample mix = getProperty(mixID);
        return (1.0 - mix) * in + mix * delayed;
    }
}
