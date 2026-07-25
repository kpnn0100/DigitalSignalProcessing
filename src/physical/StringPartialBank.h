/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  StringPartialBank: one physical string = N inharmonic StringResonator
 *  partials, excited at a strike position (mode-shape comb filter) and damped
 *  per-partial (frequency-dependent loss + damper coupling). See
 *  src/physical/README.md ## 2, 3, 4.
 */
#pragma once
#include "../base/SignalProcessor.h"
#include "StringResonator.h"
#include <array>

namespace arstro
{
    class StringPartialBank : public SignalProcessor
    {
    public:
        static constexpr int kPartialCount = 12;

        enum PropertyIndex
        {
            fundamentalID,     // Hz, f0
            inharmonicityID,   // B
            baseDecayID,       // seconds, T60 of the fundamental
            brightnessDecayID, // seconds, T60 at kLossRefHz -> solves c3/c1 (README ## 3)
            strikePositionID,  // 0..0.5, beta
            propertyCount
        };
        StringPartialBank();

        void setFundamentalHz(Sample hz);
        void setInharmonicity(Sample b);
        void setBaseDecaySeconds(Sample t60);
        void setBrightnessDecaySeconds(Sample t60AtRef);
        void setStrikePosition(Sample beta);

        // Read-only introspection of the computed partial series, so tests can
        // assert README ## 2 / ## 3's formulas exactly instead of inferring them
        // statistically from rendered audio. Out-of-range indices return 0.
        int partialCount() const { return kPartialCount; }
        Sample partialFrequencyHz(int index) const;
        Sample partialDecaySeconds(int index) const; // natural T60, before the damper

        // 0 = damper lifted, 1 = fully engaged; ramps linearly over setDamperEngageMs().
        void setDamperEngagement(Sample target01);
        void setDamperEngageMs(Sample ms);

        // Zeroes every partial's resonator history and lifts the damper — call
        // before striking a reused (voice-stolen) string. See StringResonator::reset().
        void reset();

        // Current damper ramp position, 0 (lifted) .. 1 (fully engaged). Callers
        // that drive this bank with an external signal besides the hammer (i.e.
        // PianoVoice's sympathetic-resonance feedback) must gate that input by
        // (1 - damperValue()) themselves — see README ## 8's "damped strings gate
        // sympathetic feedback too" note for why this can't be handled internally
        // by the per-partial decay alone.
        Sample damperValue() const { return mDamperValue; }

        void update() override;

    protected:
        Sample process(Sample forceIn, int channel) override;

    private:
        void recomputeEffectivePartials();

        std::array<StringResonator, kPartialCount> mPartials;
        std::array<Sample, kPartialCount> mFreqN{};
        std::array<Sample, kPartialCount> mBaseT60N{};
        std::array<Sample, kPartialCount> mGainN{};

        // Damper ramp state (own linear ramp — see README rule-1 note on why this
        // isn't the generic per-property block-smoothing ramp).
        Sample mDamperValue = 0.0;
        Sample mDamperTarget = 0.0;
        Sample mDamperStep = 0.0;
        Sample mDamperEngageMs = 20.0;

        static constexpr Sample kDamperLossGain = 40.0;
        static constexpr Sample kExciteNorm = 2.0 / (Sample)kPartialCount;

        // README ## 3: T60 = ln(1000)/alpha, and the high-frequency anchor the
        // brightnessDecay parameter is defined at.
        static constexpr Sample kT60Constant = 6.907755; // ln(1000)
        static constexpr Sample kLossRefHz = 5000.0;
        static constexpr Sample kMinAlpha = 1e-6; // keeps T60 finite if both anchors are ~0 loss
    };
}
