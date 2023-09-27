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
namespace gyrus_space
{
    class Reverb : public SignalProcessor
    {
    private:
        float mDelay;
        float mAbsorb;
        float mLastOutput = 0.0;
        int mDiffusion;
        Block mFilter;
        Delay mMainDelayBlock;
        Block mFeedBackBlock;
        Block mDiffuseBlock;
        Delay mFeedBackDelay;

        std::vector<Block> diffuserList;
        std::vector<Delay> delayOfDiffuseList;
        std::vector<Gain> gainOfDiffuse;
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