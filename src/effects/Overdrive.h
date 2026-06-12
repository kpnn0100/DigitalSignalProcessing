/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  Overdrive: tanh waveshaper with a post tone low-pass.
 *  Reuses equalizer/LowPassFilter (one per channel) for the tone control.
 */
#pragma once
#include "../base/SignalProcessor.h"
#include "../equalizer/LowPassFilter.h"
#include <vector>

namespace arstro
{
    class Overdrive : public SignalProcessor
    {
    public:
        enum PropertyIndex
        {
            driveID, // pre-gain into the shaper
            toneID,  // post low-pass cutoff (Hz)
            levelID, // output trim
            propertyCount
        };
        Overdrive();

        void setDrive(Sample drive);
        void setToneHz(Sample hz);
        void setLevel(Sample level);

        void update() override;
        void onChannelCountChanged() override;

    protected:
        Sample process(Sample in, int channel) override;

    private:
        void ensureChannels();
        std::vector<LowPassFilter> mTone; // one per channel
    };
}
