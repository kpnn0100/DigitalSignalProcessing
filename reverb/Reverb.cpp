/*
  ==============================================================================

    Reverb.cpp
    Created: 26 Sep 2023 7:12:53pm
    Author:  PC

  ==============================================================================
*/

#include "Reverb.h"
namespace gyrus_space
{
    void Reverb::updateDiffuser()
    {
        for (int i = 0; i < diffuseCount; i++)
        {
            allPass[i].setMaxDelay(48000);
            allPass[i].setDelayInMs(20.0*i);
            allPass[i].setChannelCount(8);
            mFilter.add(allPass + i);
        }
    }

    Reverb::Reverb()
    {
        mDelay = 0;
        mAbsorb = 0;
        mDiffusion = 0;
        mFilter.setIsParallel(false);
        mLowPassFilter.setCutoffFrequency(1000);
        updateDiffuser();  
        mFilter.add(&mLoopbackBlock);
        mFilter.add(&mLowPassFilter);
        mLoopbackBlock.setForwardProcessor(&mMainDelayBlock);
        mLoopbackBlock.setFeedbackProcessor(&mFeedBackBlock);

        mMainDelayBlock.setSmoothEnable(true);
        mFeedBackBlock.setIsParallel(false);


        mFeedBackDelay.setSmoothEnable(true);
        mMainDelayBlock.setMaxDelay(mSampleRate);
        mFeedBackDelay.setMaxDelay(mSampleRate);

    }

    void Reverb::setDelayInMs(float msDelay)
    {
        setDelay(msDelay / 1000 * mSampleRate);
    }

    inline void Reverb::setDelay(float delay)
    {
        if (mDelay != delay)
        {
            mDelay = delay;


            mMainDelayBlock.setDelay(delay);
            mFeedBackDelay.setDelay(delay);
            callUpdate();
        }

    }

    inline void Reverb::setDiffusion(int diff)
    {
        if (mDiffusion != diff)
        {
            mDiffusion = diff;
            callUpdate();
        }
    }

    void Reverb::setAbsorb(double absorb)
    {
        if (mAbsorb != absorb)
        {
            mLoopbackBlock.setFeedbackGain(absorb);
            mAbsorb = absorb;
            callUpdate();
        }
    }

    void Reverb::update()
    {
        updateDiffuser();  
    }

    double Reverb::process(double in)
    {
        return mFilter.out(in);
    }
}