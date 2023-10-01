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

    Reverb::Reverb() : bsReverb(50.0, 5)
    {
        mDelay = 0;
        mAbsorb = 0;
        mDiffusion = 0;
        mCurrentDelayInMs = 50.0;
        mLastDelayInMs = 50.0;
        setDecayInMs(500.0);
        setDelayInMs(100.0);
        bsReverb.configure(44100);
        onSampleRateChanged();
        updateDiffuser();
        setSmoothEnable(true);
    }

    void Reverb::setDelayInMs(double msDelay)
    {
        if (mDelayInMs != msDelay)
        {
            mDelayInMs = msDelay;
            mLastDelayInMs = mCurrentDelayInMs;
            callUpdate();
        }

        
        // bsReverb.setDelayInMs(msDelay);
        // setDelay(msDelay / 1000 * mSampleRate);
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
            callUpdate();
        }
    }

    void Reverb::setDecayInMs(double decay)
    {
        if (mDecayInMs != decay)
        {
            mDecayInMs = decay;
            mLastDecayInMs = mCurrentDecayInMs;


            // bsReverb.setDecayInMs(decay);
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

		// How long does our signal take to go around the feedback loop?
		double typicalLoopMs = mDelayInMs*1.5;
		// How many times will it do that during our RT60 period?
		double loopsPerRt60 = mDecayInMs/typicalLoopMs;
		// This tells us how many dB to reduce per loop
		double dbPerCycle = -60/loopsPerRt60;

		mDecayGain = std::pow(10, dbPerCycle*0.05);
        updateDiffuser();
    }

    void Reverb::onSampleRateChanged()
    {
        bsReverb.configure(mSampleRate);
        for (int i = 0; i < diffuseCount; i++)
        {
            mFeedback[i].setMaxDelay(mSampleRate);
        }
        setDelayInMs(mDelayInMs);
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
        mCurrentDelayInMs = mLastDelayInMs * (1 - ratio) + mDelayInMs * ratio;
        if (mBufferCounter == mBufferSize)
        {
            mLastDelayInMs = mDelayInMs;
        }
        bsReverb.mDelay = mCurrentDelayInMs;


        mCurrentDecayInMs = mLastDecayInMs * (1 - ratio) + mDecayInMs * ratio;
        if (mBufferCounter == mBufferSize)
        {
            mLastDecayInMs = mDecayInMs;
        }
        bsReverb.mRt60 = mCurrentDecayInMs / 1000.0;


        bsReverb.configure(mSampleRate);

    }
  
}