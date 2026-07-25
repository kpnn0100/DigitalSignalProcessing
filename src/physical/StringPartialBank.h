/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  StringPartialBank: one physical string = a pitch-dependent number of
 *  inharmonic partials, excited at a strike position (mode-shape comb filter)
 *  and damped per-partial (frequency-dependent loss + damper coupling). See
 *  src/physical/README.md ## 2, 3, 4.
 *
 *  The two-pole recurrence of README ## 1 is INLINED here over flat coefficient
 *  arrays rather than instantiating one StringResonator per partial: at 64
 *  partials x 3 unison x 8 voices the per-partial virtual dispatch and property
 *  machinery cost far more than the five arithmetic ops it guarded, and the
 *  projected cost breached REQ-piano-17. StringResonator remains the soundboard's
 *  mode type (## 8) and the reference implementation — a unit test asserts this
 *  loop reproduces it exactly.
 */
#pragma once
#include "../base/SignalProcessor.h"
#include "StringResonator.h"
#include <array>
#include <vector>

namespace arstro
{
    class StringPartialBank : public SignalProcessor
    {
    public:
        // Storage capacity. The ACTIVE count is pitch-dependent (README ## 2) —
        // this is only the ceiling, chosen against the REQ-piano-17 budget.
        static constexpr int kMaxPartials = 64;

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

        // 0 = damper lifted, 1 = fully engaged; ramps linearly over setDamperEngageMs().
        void setDamperEngagement(Sample target01);
        void setDamperEngageMs(Sample ms);

        // Zeroes every partial's resonator history and lifts the damper — call
        // before striking a reused (voice-stolen) string.
        void reset();

        // Current damper ramp position, 0 (lifted) .. 1 (fully engaged). Callers
        // driving this bank with an external signal besides the hammer (i.e.
        // PianoVoice's sympathetic-resonance feedback) must gate that input by
        // (1 - damperValue()) themselves — see README ## 8.
        Sample damperValue() const { return mDamperValue; }

        // Read-only introspection of the computed partial series, so tests can
        // assert README ## 2 / ## 3's formulas exactly instead of inferring them
        // statistically from rendered audio. Out-of-range indices return 0.
        int partialCount() const { return mActiveCount; } // pitch-dependent (README ## 2)
        Sample partialFrequencyHz(int index) const;
        Sample partialDecaySeconds(int index) const; // natural T60, before the damper

        void update() override;
        void onChannelCountChanged() override;

    protected:
        Sample process(Sample forceIn, int channel) override;

    private:
        void recomputeEffectivePartials(); // damper changed -> only the pole radii move
        void ensureChannels();

        // Per-partial, shared across channels. Only [0, mActiveCount) is live.
        std::array<Sample, kMaxPartials> mFreqN{};    // f_n
        std::array<Sample, kMaxPartials> mBaseT60N{}; // natural T60_n (pre-damper)
        std::array<Sample, kMaxPartials> mCosTheta{};
        std::array<Sample, kMaxPartials> mDrive{}; // sin(theta)*g_n*(2/N) — damper-independent
        std::array<Sample, kMaxPartials> mA1{};    // 2*r*cos(theta)
        std::array<Sample, kMaxPartials> mA2{};    // r^2
        int mActiveCount = 1;

        // Per-channel recurrence history, flat: [channel*kMaxPartials + partial].
        // Sized in ensureChannels(); never resized from the audio thread.
        std::vector<Sample> mY1;
        std::vector<Sample> mY2;

        // Damper ramp state (own linear ramp — see README rule-1 note on why this
        // isn't the generic per-property block-smoothing ramp).
        Sample mDamperValue = 0.0;
        Sample mDamperTarget = 0.0;
        Sample mDamperStep = 0.0;
        Sample mDamperEngageMs = 20.0;

        static constexpr Sample kDamperLossGain = 40.0;

        // README ## 3: T60 = ln(1000)/alpha, and the high-frequency anchor the
        // brightnessDecay parameter is defined at.
        static constexpr Sample kT60Constant = 6.907755278982137; // ln(1000), full precision:
        // truncating this made the inlined recurrence disagree with StringResonator
        // (which computes r as pow(10,-3/x)) by ~1e-8 after 2000 samples of IIR
        // recursion. At full precision exp(-ln1000/x) and pow(10,-3/x) agree bitwise.
        static constexpr Sample kLossRefHz = 5000.0;
        static constexpr Sample kMinAlpha = 1e-6; // keeps T60 finite if both anchors are ~0 loss

        // README ## 2: the highest partial kept, as a fraction of the sample rate.
        static constexpr Sample kNyquistFraction = 0.45;
    };
}
