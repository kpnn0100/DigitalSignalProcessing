/*
  ==============================================================================

    Reverb.h
    Created: 26 Sep 2023 7:12:53pm
    Author:  PC

  ==============================================================================
*/

#pragma once
#include "../simpleProcessor/Delay.h"
#include "../simpleProcessor/Gain.h"
#include "../base/Block.h"
#include "../base/SignalProcessor.h"
#include "../util/Util.h"
#include "../equalizer/SchroederAllpass.h"
namespace gyrus_space
{
    class Reverb : public SignalProcessor
    {
    private:
        const static int diffuseCount = 8;
        float mDelay;
        float mAbsorb;
        float mLastOutput = 0.0;
        int mDiffusion;
        Block mFilter;
        Delay mMainDelayBlock;
        Block mFeedBackBlock;
        Delay mFeedBackDelay;
        
        SchroederAllpass allPass[diffuseCount];
        void updateDiffuser();
    public:
        Reverb();
        void setDelayInMs(float msDelay);
        void setDelay(float delay);
        void setDiffusion(int diff);
        void setAbsorb(double absorb);
        void update() override;
        double process(double in) override;
    };
}