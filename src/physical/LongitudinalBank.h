/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  LongitudinalBank: the string's LONGITUDINAL modes — the ones that run along
 *  the wire rather than across it — plus the quasi-static tension response.
 *  Driven by the SQUARE of the string's slope (README ## 12.2), which is what
 *  makes its output land at 2*f_i and f_i +/- f_j: frequencies the transverse
 *  series (## 2) predicts no partial at all. Those are the phantom partials, and
 *  they are the metallic growl of the bottom octaves.
 *
 *  Runs mono (REQ-piano-13) — driven by PianoVoice on channel 0 only.
 *  See src/physical/README.md ## 12.
 */
#pragma once
#include "../base/SignalProcessor.h"
#include "StringResonator.h"
#include "../equalizer/LowPassFilter.h"
#include <array>

namespace arstro
{
    class LongitudinalBank : public SignalProcessor
    {
    public:
        // README ## 12.1: modes at m*f_long,1. A handful is enough — longitudinal
        // energy is concentrated low and heavily damped, and this bank exists per
        // VOICE (## 12.4), so the count is a realism knob, not a cost driver.
        static constexpr int kMaxModes = 8;

        enum PropertyIndex
        {
            fundamentalID,      // Hz, the TRANSVERSE f0 — everything else derives from it
            tensionCouplingID,  // kappa: displacement^2 -> tension modulation (## 12.2)
            propertyCount
        };
        LongitudinalBank();

        void setFundamentalHz(Sample hz);
        void setTensionCoupling(Sample kappa);

        /** README ## 12.4: how strongly this note carries longitudinal energy at
         *  all, 1.0 through the bottom two octaves falling to ~0 by the treble.
         *  Public and static so tests assert the law rather than infer it. */
        static Sample registerGainFor(Sample f0Hz);
        /** README ## 12.1: f_long,1 = c_L/(2L), fitted from real string scaling. */
        static Sample firstModeHz(Sample f0Hz);

        /** True when this note is above the crossover and the bank is switched off
         *  entirely — no resonators run, which is what keeps M7 off the treble's
         *  CPU budget (## 12.4). */
        bool isActive() const { return mActive; }
        int activeModeCount() const { return mActive ? mModeCount : 0; }

        // Zeroes every mode's history and the DC-blocker state.
        void reset();

        void update() override;

    protected:
        /** `in` is the summed transverse modal VELOCITY — the string's slope, since
         *  y_n = omega_n*q_n weights mode n by n exactly as d(y)/dx does (README
         *  ## 12.2). NOT the strike-point displacement, whose 1/n weighting leaves
         *  the longitudinal modes undriven. Squared, DC-blocked and fed to the
         *  modes; returns the radiated longitudinal contribution. */
        Sample process(Sample transverseSlope, int channel) override;

    private:
        std::array<StringResonator, kMaxModes> mModes;
        std::array<Sample, kMaxModes> mModeWeight{};
        LowPassFilter mDcTracker; // HP(x) = x - LPF(x), README ## 12.2
        int mModeCount = 0;
        bool mActive = false;
        Sample mRegisterGain = 0.0;

        // ## 12.1: longitudinal (bar) wave speed of steel, and the string-length
        // fit it is divided by. Combined into one anchor frequency so the fit is
        // one constant rather than two that can drift apart:
        //   f_long,1 = c_L/(2L(f0)),  L(f0) = 0.62 m * (f_ref/f0)^0.52
        // giving 4194 Hz at C4 and 1300 Hz at A0 (real bass strings: ~1.3 kHz).
        static constexpr Sample kRegisterRefHz = 261.6;
        static constexpr Sample kFirstModeAtRefHz = 4194.0;
        static constexpr Sample kLengthExponent = 0.52;

        // ## 12.3: longitudinal modes are heavily damped — they couple hard to the
        // bridge and are gone in a fraction of a second. That is why they read as
        // an ATTACK colour (a clang) rather than as a pitched tail.
        static constexpr Sample kFirstModeT60 = 0.35;
        static constexpr Sample kT60Exponent = 0.5;
        static constexpr Sample kT60Min = 0.05;
        static constexpr Sample kT60Max = 1.0;

        // ## 12.3: the non-resonant (quasi-static) share of the response. Without
        // it the bank would only pass combination frequencies that happen to land
        // on a longitudinal resonance, and most phantom partials do not.
        static constexpr Sample kDirectWeight = 0.35;

        // ## 12.4 register weighting.
        static constexpr Sample kCrossoverHz = 130.8; // C3
        static constexpr Sample kRegisterExponent = 1.5;
        // Below this the bank is switched off entirely rather than run at a
        // negligible gain — the whole point of the crossover is that the treble
        // pays no CPU for an effect it cannot produce.
        static constexpr Sample kMinAudibleGain = 0.06;

        // ## 12.2: DC blocker corner. Well below the lowest f_long,1 (1300 Hz) so
        // it removes the static tension rise without touching the modes.
        static constexpr Sample kDcBlockerHz = 60.0;

        // Highest mode kept, as a fraction of the sample rate (mirrors ## 2).
        static constexpr Sample kNyquistFraction = 0.45;
    };
}
