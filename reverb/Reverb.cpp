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
    Reverb::Reverb()
    {
        mDelay = 0;
        mAbsorb = 0;
        mDiffusion = 0;
        mFilter.setIsParallel(false);
        mFilter.add(&mMainDelayBlock);
        mMainDelayBlock.setSmoothEnable(true);
        mFeedBackBlock.setIsParallel(false);
        mFeedBackBlock.add(&mFeedBackDelay);
        mFeedBackDelay.setSmoothEnable(true);
        mMainDelayBlock.setMaxDelay(mSampleRate);
        mFeedBackDelay.setMaxDelay(mSampleRate);
        callUpdate();
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
            mAbsorb = absorb;
            callUpdate();
        }
    }

    void Reverb::update()
    {

    }

    double Reverb::process(double in)
    {
        double output = mMainDelayBlock.out(in + mAbsorb * mFeedBackBlock.out(mLastOutput));
        mLastOutput = output;
        return output;
    }
}