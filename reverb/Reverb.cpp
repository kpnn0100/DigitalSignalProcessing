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
    void Reverb::updateDiffuser()
    {
    }

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

    void Reverb::setDelayInMs(double msDelay)
    {
        setProperty(delayID,msDelay);
    }

    inline void Reverb::setDelay(double delay)
    {
        if (mDelay != delay)
        {
            mDelay = delay;
            for (int c = 0; c < diffuseCount; ++c)
            {
                double r = c * 1.0 / diffuseCount;
                mFeedback[c].setDelay( std::pow(2, r) * delay);
            }
        }
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

    void Reverb::update()
    {
    }

    void Reverb::onSampleRateChanged()
    {
        bsReverb.configure(mSampleRate);
        for (int i = 0; i < diffuseCount; i++)
        {
            mFeedback[i].setMaxDelay(mSampleRate);
        }
        setDelayInMs(getProperty(delayID));
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
        bsReverb.mDelay = getProperty(delayID);
        bsReverb.mRt60 = getProperty(decayID) / 1000.0;
        bsReverb.configure(mSampleRate);
    }
  
}