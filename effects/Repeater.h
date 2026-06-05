/*
 *  Gyrus Space DSP Library — ESP32DigitalSynth
 *
 *  Repeater: feedback echo ("echo echo echo"). Each repeat is quieter by the
 *  feedback fraction and a bit darker because a low-pass sits INSIDE the feedback
 *  loop. Reuses simpleProcessor/Delay + equalizer/LowPassFilter, one per channel.
 *  Delay spacing is a SAMPLE COUNT (no wall-clock time).
 */
#pragma once
#include "../base/SignalProcessor.h"
#include "../simpleProcessor/Delay.h"
#include "../equalizer/LowPassFilter.h"
#include <vector>

namespace gyrus_space
{
    class Repeater : public SignalProcessor
    {
    public:
        enum PropertyIndex
        {
            delayID,      // echo spacing in samples
            feedbackID,   // per-repeat volume fraction (the "reduce %"), 0..<1
            toneCutoffID, // low-pass cutoff in the feedback path (Hz)
            mixID,        // dry/wet 0..1
            propertyCount
        };
        Repeater();

        void setDelayMs(Sample ms);
        void setFeedback(Sample fraction);
        void setToneCutoffHz(Sample hz);
        void setMix(Sample mix);

        void update() override;
        void onChannelCountChanged() override;

    protected:
        Sample process(Sample in, int channel) override;

    private:
        void ensureChannels();
        std::vector<Delay> mDelay;          // per channel
        std::vector<LowPassFilter> mTone;   // per channel, in feedback path
    };
}
