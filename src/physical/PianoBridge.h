/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  PianoBridge: shared soundboard modal bank + sympathetic-resonance feedback
 *  bus. One instance is shared by every PianoVoice in a PianoEngine. Runs mono
 *  (REQ-piano-13). See src/physical/README.md ## 8.
 */
#pragma once
#include "../base/SignalProcessor.h"
#include "StringResonator.h"
#include <array>

namespace arstro
{
    class PianoBridge : public SignalProcessor
    {
    public:
        static constexpr int kBodyModeCount = 8;

        enum PropertyIndex
        {
            couplingGainID,  // sympathetic-feedback scale into voices' strings
            radiationGainID, // audible soundboard output scale
            propertyCount
        };
        PianoBridge();

        // Multi-voice usage (PianoEngine): every voice calls accumulate() once
        // per sample, then the engine calls tick() once to finalize the sample.
        void accumulate(Sample contribution);
        void tick();

        // Previous sample's response, pre-scaled by couplingGain — add directly
        // to a voice's string excitation for sympathetic resonance (README ## 8).
        Sample feedback() const { return mLastResponse * mCouplingGain; }
        // Previous sample's response, pre-scaled by radiationGain — the bridge's
        // own audible contribution to the mixed output.
        Sample radiatedOutput() const { return mLastResponse * mRadiationGain; }

        void setCouplingGain(Sample g);
        void setRadiationGain(Sample g);

        void update() override;

    protected:
        // Single-input convenience path (unit-test / standalone use):
        // accumulate(in) then tick() then return radiatedOutput().
        Sample process(Sample in, int channel) override;

    private:
        std::array<StringResonator, kBodyModeCount> mModes;
        Sample mBus = 0.0;
        Sample mLastResponse = 0.0;
        Sample mCouplingGain = 0.0;
        Sample mRadiationGain = 0.0;
    };
}
