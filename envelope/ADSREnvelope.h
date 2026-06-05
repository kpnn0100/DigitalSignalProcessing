/*
 *  Gyrus Space DSP Library — ESP32DigitalSynth
 *
 *  ADSREnvelope: Attack-Decay-Sustain-Release amplitude envelope.
 *
 *  NOT an effect. It is a reusable building block OWNED by SignalGenerator and
 *  applied to the generated waveform. Stage lengths are stored as SAMPLE COUNTS
 *  (ms -> samples at the setter); the state machine advances one sample per
 *  process() call. No wall-clock time anywhere.
 */
#pragma once
#include "../base/SignalProcessor.h"
#include <vector>

namespace gyrus_space
{
    class ADSREnvelope : public SignalProcessor
    {
    public:
        enum Stage { Idle, Attack, Decay, Sustain, Release };
        enum PropertyIndex
        {
            attackID,  // attack length  (samples)
            decayID,   // decay length   (samples)
            sustainID, // sustain LEVEL  (linear 0..1)
            releaseID, // release length (samples)
            propertyCount
        };

        ADSREnvelope();

        // Setters take musical units; converted to samples via AudioConfig.
        void setAttackMs(Sample ms);
        void setDecayMs(Sample ms);
        void setSustain(Sample level);
        void setReleaseMs(Sample ms);

        void noteOn(Sample velocity); // velocity 0..1 scales the peak; -> Attack
        void noteOff();               // -> Release from the current level
        bool isFinished() const;      // all channels reached Idle after Release

        void onChannelCountChanged() override;
        void update() override; // caches ADSR params for the hot path

        // Non-virtual hot path: advance one sample for `channel`, return the gain.
        // Used directly by SignalGenerator/Oscillator block loops (no virtual call,
        // no per-sample getProperty).
        Sample nextValue(int channel);

    protected:
        // Multiplies the input by the current per-channel envelope value and
        // advances that channel's state machine by one sample.
        Sample process(Sample in, int channel) override;

    private:
        void ensureChannels();
        // cached params (samples / level), refreshed in update()
        Sample mAtk = 0, mDec = 0, mSus = 0.7, mRel = 0;
        struct ChannelState
        {
            Stage stage = Idle;
            Sample level = 0.0;            // current envelope value
            Sample releaseStartLevel = 0.0; // level captured at noteOff
            long sampleInStage = 0;        // sample counter within the stage
        };
        std::vector<ChannelState> mState;
        Sample mPeak = 1.0; // velocity-scaled target peak
    };
}
