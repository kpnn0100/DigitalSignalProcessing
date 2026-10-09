/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  Compressor: feed-forward dynamic range compressor.
 *  Per-channel peak follower; attack/release stored as SAMPLE COUNTS.
 *
 *  Sidechain (REQ-fx-sidechain-1): with `setSidechain(true)` the detector follows a KEY signal
 *  — another track's audio, handed in before each block with `setKey` — instead of the input, so a
 *  kick can duck a bass. No key while sidechained = a silent key = no gain reduction.
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
        void setSidechain(bool on) { mSidechain = on; }
        bool sidechain() const { return mSidechain; }
        /** The key for the NEXT processBlock calls — `channels` buffers of at least that block's
         *  frames, valid until then (nullptr = silence). Channel c reads key[min(c, channels − 1)]. */
        void setKey(const Sample *const *key, int channels) { mKey = key; mKeyChannels = key ? channels : 0; }

        void update() override;
        void onChannelCountChanged() override;
        void processBlock(Sample *buf, int frames, int channel) override;

    protected:
        Sample process(Sample in, int channel) override;

    private:
        /** (C1)–(C4): the detector follows `level`, the gain applies to `in`. */
        Sample compress(Sample in, Sample level, int channel);
        void ensureChannels();
        bool mSidechain = false;
        const Sample *const *mKey = nullptr;
        int mKeyChannels = 0;
        std::vector<Sample> mEnv; // per-channel detector level
        Sample mAtkCoeff = 0.0;
        Sample mRelCoeff = 0.0;
    };
}
