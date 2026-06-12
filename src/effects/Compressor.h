/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  Compressor: feed-forward dynamic range compressor.
 *  Per-channel peak follower; attack/release stored as SAMPLE COUNTS.
 */
#pragma once
#include "../base/SignalProcessor.h"
#include <vector>

namespace arstro
{
    class Compressor : public SignalProcessor
    {
    public:
        enum PropertyIndex
        {
            thresholdDbID, // dB
            ratioID,       // e.g. 4 -> 4:1
            attackID,      // samples
            releaseID,     // samples
            makeupID,      // linear gain
            propertyCount
        };
        Compressor();

        void setThresholdDb(Sample db);
        void setRatio(Sample ratio);
        void setAttackMs(Sample ms);
        void setReleaseMs(Sample ms);
        void setMakeupGain(Sample linear);

        void update() override;
        void onChannelCountChanged() override;

    protected:
        Sample process(Sample in, int channel) override;

    private:
        void ensureChannels();
        std::vector<Sample> mEnv; // per-channel detector level
        Sample mAtkCoeff = 0.0;
        Sample mRelCoeff = 0.0;
    };
}
