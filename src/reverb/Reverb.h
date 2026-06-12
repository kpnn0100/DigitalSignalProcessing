/*
 *  Arstro DSP Library
 *  Author: kpnn0100
 *  Organization: Arstro
 *
 *  Description:
 *  This is an open-source Digital Signal Processing (DSP) library developed by Arstro.
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
#include <vector>
namespace arstro
{
    class Reverb : public SignalProcessor
    {
    private:
        const static int diffuseCount = 4;
		const static int stepCount = 4;
		using Array = std::array<Sample, diffuseCount>;
        Sample mDelay;
        Sample mAbsorb;
        Sample mDecayGain;
        Sample mLastOutput = 0.0;
        int mDiffusion;
        Sample mDry = 0.7; // dry/wet handled at this level so the dry path is full level
        Sample mWet = 0.3;
        // Stereo width: 0 = every channel identical (mono), 1 = max per-channel
        // delay decorrelation. See reverb/README.md (natural stereo).
        Sample mWidth = 0.6;
        static constexpr Sample kStereoSpread = 0.15; // ±15% base-delay spread at width 1
		// One FDN network per channel; base delay is decorrelated by mWidth so a
		// mono input yields decorrelated L/R tails (natural stereo).
		std::vector<BasicReverb<diffuseCount,stepCount>> bsReverb;
        void ensureChannels();
    public:
        enum PropertyIndex {
            delayID,
            decayID,
            propertyCount
        };
        Reverb();
        void setDelayInMs(Sample msDelay);
		void setDecayInMs(Sample decay);
        void setMix(Sample wet); // 0 = fully dry, 1 = fully wet
        void setWidth(Sample width); // 0 = mono (identical channels), 1 = max stereo spread
        void setDiffusion(int diff);
        void update() override;
		void onSampleRateChanged() override;
        void onChannelCountChanged() override;
        Sample process(Sample in, int channel) override;
        void smoothUpdate(Sample ratio) override;
        void setLowCutFrequency(Sample frequency);
        void setHighCutFrequency(Sample frequency);
    };
}