/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  Chorus: modulated short delay. Reuses simpleProcessor/Delay (one per channel).
 *  Per-channel LFO phase gives true stereo width from a single instance.
 *  LFO rate and delay are sample-count based (no wall-clock time).
 */
#pragma once
#include "../base/SignalProcessor.h"
#include "../simpleProcessor/Delay.h"
#include <vector>

namespace arstro
{
    class Chorus : public SignalProcessor
    {
    public:
        enum PropertyIndex
        {
            rateID,      // LFO rate as phase increment per sample (cycles/sample)
            depthID,     // modulation depth in samples
            baseDelayID, // center delay in samples
            mixID,       // dry/wet 0..1
            propertyCount
        };
        Chorus();

        void setRateHz(Sample hz);
        void setDepthMs(Sample ms);
        void setBaseDelayMs(Sample ms);
        void setMix(Sample mix);

        void onChannelCountChanged() override;

    protected:
        Sample process(Sample in, int channel) override;

    private:
        void ensureChannels();
        std::vector<Delay> mDelay;  // per channel
        std::vector<Sample> mPhase; // per-channel LFO phase [0,1)
    };
}
