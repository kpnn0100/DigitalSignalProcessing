/*
 *  Gyrus Space DSP Library
 *  Author: kpnn0100
 *  Organization: Gyrus Space
 *
 *  Description:
 *  This is an open-source Digital Signal Processing (DSP) library developed by Gyrus Space.
 *  It provides various functions and utilities for processing digital signals and audio data.
 *  The library is free to use and open for contributions from the community.
 *
 *  License: MIT License
 *  GitHub Repository: https://github.com/kpnn0100/DigitalSignalProcessing
 *
 *  Disclaimer: This library is provided as-is without any warranties. The author and organization
 *  shall not be held liable for any damages or liabilities arising from the use of this library.
 */

#pragma once
#include "../simpleProcessor/Delay.h"
#include "../simpleProcessor/Gain.h"
#include "../base/Block.h"
#include "../base/SignalProcessor.h"
#include "../util/Util.h"
#include "../equalizer/LowPassFilter.h"
#include "../base/FeedbackBlock.h"
#include "../equalizer/HighPassFilter.h"
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
        double mDecayGain;
        double mLastOutput = 0.0;
        int mDiffusion;
		Delay mFeedback[diffuseCount];
		BasicReverb<diffuseCount,stepCount> bsReverb;
        void updateDiffuser();
        void setDelay(double delay);
    public:
        enum PropertyIndex {
            delayID,
            decayID,
            propertyCount
        };
        Reverb();
        void setDelayInMs(double msDelay);
		void setDecayInMs(double decay);
        void setDiffusion(int diff);
        void onPropertyUpdated() override;
		void onSampleRateChanged();
        double process(double in) override;
        void smoothUpdate(double ratio) override;
        void setLowCutFrequency(double frequency);
        void setHighCutFrequency(double frequency);
    };
}