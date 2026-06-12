/*
  ==============================================================================

    Reverb.cpp
    Created: 26 Sep 2023 7:12:53pm
    Author:  PC

    ESP32DigitalSynth: migrated to the channel-aware base. One BasicReverb
    network is held per channel so left/right reverberate independently (no bleed).
  ==============================================================================
*/

#include "Reverb.h"
#include "../base/AudioConfig.h"
Sample randomInRange(Sample low, Sample high)
{
    // There are better randoms than this, and you should use them instead 😛
    Sample unitRand = rand() / Sample(RAND_MAX);
    return low + unitRand * (high - low);
}
namespace arstro
{
    Reverb::Reverb() : SignalProcessor(propertyCount)
    {
        mDelay = 0;
        mAbsorb = 0;
        mDiffusion = 0;
        initProperty(delayID, 100.0);
        initProperty(decayID, 500.0);
        ensureChannels();
        update();
        setSmoothEnable(false);
    }

    void Reverb::ensureChannels()
    {
        int n = AudioConfig::instance().channelCount();
        if ((int)bsReverb.size() == n)
            return;
        bsReverb.clear();
        for (int c = 0; c < n; ++c)
            bsReverb.emplace_back(50.0, 5.0);
        update();
    }

    void Reverb::onChannelCountChanged()
    {
        ensureChannels();
    }

    void Reverb::setDelayInMs(Sample msDelay)
    {
        setProperty(delayID, msDelay);
    }

    void Reverb::setDecayInMs(Sample decay)
    {
        setProperty(decayID, decay);
    }

    void Reverb::setMix(Sample wet)
    {
        if (wet < 0.0) wet = 0.0;
        if (wet > 1.0) wet = 1.0;
        mWet = wet;
        mDry = 1.0 - wet;
    }

    void Reverb::setWidth(Sample width)
    {
        if (width < 0.0) width = 0.0;
        if (width > 1.0) width = 1.0;
        if (mWidth != width)
        {
            mWidth = width;
            callUpdate(); // re-derive per-channel decorrelated delays
        }
    }

    void Reverb::setDiffusion(int diff)
    {
        if (mDiffusion != diff)
        {
            mDiffusion = diff;
            callUpdate();
        }
    }

    void Reverb::update()
    {
        Sample sr = AudioConfig::instance().sampleRate();
        Sample base = getProperty(delayID);
        Sample rt60 = getProperty(decayID) / 1000.0;
        int n = (int)bsReverb.size();
        for (int c = 0; c < n; ++c)
        {
            // Spread channels across [-1, +1] and decorrelate the base delay by
            // width so a mono input produces decorrelated per-channel tails.
            Sample offsetNorm = (n <= 1) ? 0.0 : (2.0 * c / (n - 1) - 1.0);
            bsReverb[c].mDelay = base * (1.0 + mWidth * kStereoSpread * offsetNorm);
            bsReverb[c].mRt60 = rt60;
            bsReverb[c].configure(sr);
        }
    }

    void Reverb::onSampleRateChanged()
    {
        Sample sr = AudioConfig::instance().sampleRate();
        for (auto &r : bsReverb)
            r.configure(sr);
    }

    Sample Reverb::process(Sample in, int channel)
    {
        if (channel < 0 || channel >= (int)bsReverb.size())
            return in;
        Array input;
        for (int i = 0; i < diffuseCount; i++)
            input[i] = in;
        input = bsReverb[channel].process(input);
        // Average the diffuse channels to get the wet reverb signal, then mix
        // with the full-level dry signal at this level (BasicReverb stays wet).
        Sample wet = 0.0;
        for (int i = 0; i < diffuseCount; i++)
            wet += input[i];
        wet /= (Sample)diffuseCount;
        return mDry * in + mWet * wet;
    }

    void Reverb::smoothUpdate(Sample ratio)
    {
    }

    void Reverb::setLowCutFrequency(Sample frequency)
    {
        for (auto &r : bsReverb)
            r.setLowCutFrequency(frequency);
    }

    void Reverb::setHighCutFrequency(Sample frequency)
    {
        for (auto &r : bsReverb)
            r.setHighCutFrequency(frequency);
    }
}
