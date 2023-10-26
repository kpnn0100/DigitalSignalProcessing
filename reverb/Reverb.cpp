/*
  ==============================================================================

    Reverb.cpp
    Created: 26 Sep 2023 7:12:53pm
    Author:  PC

  ==============================================================================
*/

#include "Reverb.h"
double randomInRange(double low, double high)
{
    // There are better randoms than this, and you should use them instead 😛
    double unitRand = rand() / double(RAND_MAX);
    return low + unitRand * (high - low);
}
namespace gyrus_space
{
        Reverb::Reverb() : SignalProcessor(propertyCount), bsReverb(50.0, 5)
    {
        mDelay = 0;
        mAbsorb = 0;
        mDiffusion = 0;
        initProperty(delayID, 50.0);
        setDecayInMs(500.0);
        setDelayInMs(100.0);
        bsReverb.configure(44100);
        onSampleRateChanged();
        updateDiffuser();
        setSmoothEnable(true);
    }

    void Reverb::updateDiffuser()
    {
    }


    void Reverb::setDelayInMs(double msDelay)
    {
        setProperty(delayID,msDelay);
    }

    inline void Reverb::setDelay(double delay)
    {

    }

    void Reverb::setDecayInMs(double decay)
    {
        setProperty(decayID, decay);
    }

    inline void Reverb::setDiffusion(int diff)
    {
        if (mDiffusion != diff)
        {
            mDiffusion = diff;
            callUpdate();
        }
    }

    void Reverb::onPropertyUpdated()
    {
        bsReverb.mDelay = getProperty(delayID);
        bsReverb.mRt60 = getProperty(decayID) / 1000.0;
        bsReverb.configure(mSampleRate);
    }

    void Reverb::onSampleRateChanged()
    {
        bsReverb.configure(mSampleRate);
    }

    double Reverb::process(double in)
    {
        // return mFilter.out(in);
        Array input;
        for (int i = 0; i < diffuseCount; i++)
        {
            input[i] = in;
        }
        input = bsReverb.process(input);
        double out = 0.0;
        for (int i = 0; i < diffuseCount; i++)
        {
            out += input[i]/(double)diffuseCount/4.0;
        }
        return out;
    }

    void Reverb::smoothUpdate(double ratio)
    {

    }

    void Reverb::setLowCutFrequency(double frequency)
    {
        bsReverb.setLowCutFrequency(frequency);
    }

    void Reverb::setHighCutFrequency(double frequency)
    {
        bsReverb.setHighCutFrequency(frequency);
    }
  
}