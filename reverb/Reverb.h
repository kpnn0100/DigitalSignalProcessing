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
#include "../diffuser/MultiChannelAllPass.h"
#include "../equalizer/LowPassFilter.h"
#include "../base/FeedbackBlock.h"
#include "ClassForReverb.h"
namespace gyrus_space
{
    class Reverb : public SignalProcessor
    {
    private:
        const static int diffuseCount = 4;
		const static int stepCount = 4;
		using Array = std::array<double, diffuseCount>;
        double mDelay;
        double mAbsorb;

		double mDelayInMs;
        double mLastDelayInMs;
        double mCurrentDelayInMs;


		double mDecayGain;

		double mDecayInMs;
        double mLastDecayInMs;
        double mCurrentDecayInMs;

        double mLastOutput = 0.0;
        int mDiffusion;
		Delay mFeedback[diffuseCount];
		BasicReverb<diffuseCount,stepCount> bsReverb;
        void updateDiffuser();
    public:
        Reverb();
        void setDelayInMs(double msDelay);
        void setDelay(double delay);
		void setDecayInMs(double decay);
        void setDiffusion(int diff);
        void setAbsorb(double absorb);
        void update() override;
		void onSampleRateChanged();
        double process(double in) override;
        void smoothUpdate(double ratio) override;
    };
}