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
        int numberOfDiffuser = diffuserList.size();
        for (int i = 0; i <numberOfDiffuser;i++)
        {
            float delay = mDelay /(i%3 +i + i%5);
            delayOfDiffuseList[i].setDelay( delay);
            std::cout<<delay<<std::endl;
        }
    }

    Reverb::Reverb()
    {
        mDelay = 0;
        mAbsorb = 0;
        mDiffusion = 0;
        mFilter.setIsParallel(false);
        
        mFilter.add(&mMainDelayBlock);
        mMainDelayBlock.setSmoothEnable(true);
        int diffuserCount = 4;
        diffuserList.resize(diffuserCount);
        delayOfDiffuseList.resize(diffuserCount);
        gainOfDiffuse.resize(diffuserCount);

        mDiffuseBlock.setIsParallel(true);
        mDiffuseBlock.setNeedAverage(true);
        for (int i = 0; i <diffuserList.size();i++)
        {
            delayOfDiffuseList[i].setMaxDelay(8000);
            // gainOfDiffuse[i].setGain(-1.0 + 2.0*(i%2));
            gainOfDiffuse[i].setGain(1.0);
            diffuserList[i].add(&delayOfDiffuseList[i]);
            diffuserList[i].add(&gainOfDiffuse[i]);

            mDiffuseBlock.add(&diffuserList[i]);    
        }

        mFeedBackBlock.setIsParallel(false);
        // mFeedBackBlock.add(&mFeedBackDelay);
        mFeedBackBlock.add(&mDiffuseBlock);
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

            updateDiffuser();
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