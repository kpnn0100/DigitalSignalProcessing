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
        // README ## 8: plate modal density is constant in Hz, so these are spaced
        // uniformly in frequency across kBodyLowHz..kBodyHighHz. PianoBridge is a
        // SINGLE shared instance, so 128 modes cost ~10% of the string resonators —
        // the count is limited by realism bookkeeping, not by CPU.
        static constexpr int kBodyModeCount = 128;

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
        // MODAL response only: the sympathetic loop is unaffected by the M10 body term.
        Sample feedback() const { return mLastResponse * mCouplingGain; }
        // The bridge's audible contribution: the modal resonance (radiationGain) PLUS the
        // M10 broadband radiativity (README ## 8.2), the board's direct transmission of the
        // string, low-passed by the board's HF radiation rolloff and scaled by the series mix.
        Sample radiatedOutput() const { return mLastResponse * mRadiationGain + mBodyDirect * mBodyDirectGain; }

        void setCouplingGain(Sample g);
        void setRadiationGain(Sample g);
        // README ## 8.2 (M10): gain of the broadband radiativity path. 0 = pure parallel
        // modal coloration (M0–M9); >0 routes the string through the board in series.
        void setBodyDirectGain(Sample g) { mBodyDirectGain = g; }

        void update() override;
        void onSampleRateChanged() override { update(); } // README ## 8.2: refresh mBodyCoeff

    protected:
        // Single-input convenience path (unit-test / standalone use):
        // accumulate(in) then tick() then return radiatedOutput().
        Sample process(Sample in, int channel) override;

    private:
        std::array<StringResonator, kBodyModeCount> mModes;
        std::array<Sample, kBodyModeCount> mModeWeight{}; // radiation(f)/sqrt(M)
        Sample mBus = 0.0;
        Sample mLastResponse = 0.0;
        Sample mCouplingGain = 0.0;
        Sample mRadiationGain = 0.0;
        // README ## 8.2 (M10): broadband radiativity — a one-pole low-pass of the string
        // bus at the board's HF radiation corner, its direct-transmission path.
        Sample mBodyLp = 0.0;         // LPF state
        Sample mBodyDirect = 0.0;     // this sample's radiativity output
        Sample mBodyCoeff = 0.0;      // one-pole coefficient (from the radiation corner)
        Sample mBodyDirectGain = 0.0; // series mix × velocity→signal, set by the engine
    };
}
