/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  PianoVoice: a single struck-note physical-modeling piano voice. Ties
 *  together all 7 anatomy stages (src/physical/README.md): hammer contact
 *  (HammerExciter) -> unison inharmonic strings (StringPartialBank) -> shared
 *  bridge push/pull (PianoBridge, sympathetic resonance) -> damper + pedals +
 *  secondary mechanical noise.
 *
 *  Mono physical core (REQ-piano-13): generate(channel) runs the full physics
 *  step only on channel 0 and replicates the cached result to other channels.
 *  Callers MUST advance channel 0 before any other channel for a given sample
 *  (the natural/only sensible render order — see PianoEngine).
 */
#pragma once
#include "../base/SignalGenerator.h"
#include "StringPartialBank.h"
#include "HammerExciter.h"
#include "PianoBridge.h"
#include "../equalizer/LowPassFilter.h"
#include <array>
#include <cstdint>

namespace arstro
{
    class PianoVoice : public SignalGenerator
    {
    public:
        static constexpr int kMaxUnison = 3;

        enum PropertyIndex { propertyCount }; // no properties of its own — all params live on owned sub-objects

        PianoVoice();

        void setFrequency(Sample hz) override;

        void setUnisonCount(int count);
        void setUnisonDetuneCents(Sample cents);
        void setInharmonicity(Sample b);   // overrides the register-dependent default
        void setBaseDecaySeconds(Sample t60); // overrides the pitch-derived default
        void setBrightnessDecaySeconds(Sample t60AtRef);

        /** The pitch -> fundamental-T60 curve of README ## 3, applied by
         *  setFrequency() unless setBaseDecaySeconds() has overridden it. Public
         *  and static so it is directly testable. */
        static Sample defaultBaseDecaySeconds(Sample f0Hz);

        /** The README ## 11 per-register scaling laws, applied by setFrequency()
         *  unless the corresponding setter has latched an override. Public and
         *  static so tests assert the laws themselves rather than inferring them
         *  from rendered audio. */
        static Sample defaultModalMass(Sample f0Hz);        // ## 11.1
        static Sample defaultHammerMass(Sample f0Hz);       // ## 11.2
        static Sample defaultHammerStiffness(Sample f0Hz);  // ## 11.3
        static Sample defaultStrikePosition(Sample f0Hz);   // ## 11.4
        static int    defaultUnisonCount(Sample f0Hz);      // ## 11.5

        void setStrikePosition(Sample beta);
        void setModalMass(Sample m);
        void setDamperEngageMs(Sample ms);

        void setHammerMass(Sample m);
        void setHammerStiffness(Sample k);
        void setHammerNonlinearExponent(Sample p);
        void setHammerHysteresisLoss(Sample eps);

        void setUnaCorda(bool on) { mUnaCorda = on; }
        // Sustain/sostenuto gate: while true, noteOff() does not engage the damper.
        void setDamperHeld(bool held) { mDamperHeld = held; }
        // Shared soundboard/bridge for sympathetic resonance (REQ-piano-6). Optional —
        // null means the voice still works standalone (no cross-string coupling).
        void setBridge(PianoBridge *bridge) { mBridge = bridge; }

        void noteOn(Sample velocity);
        void noteOff();

        void onSampleRateChanged() override;

    protected:
        Sample generate(int channel) override;

    private:
        Sample computeDefaultInharmonicity(Sample f0Hz) const;
        void applyUnisonFrequencies();
        void applyRegisterScaling(); // README ## 11
        void recomputeNoiseCoeffs();
        Sample nextWhiteNoise();

        std::array<StringPartialBank, kMaxUnison> mStrings;
        HammerExciter mHammer;
        LowPassFilter mThumpFilter;
        LowPassFilter mDamperNoiseFilter;
        PianoBridge *mBridge = nullptr;

        int mUnisonCount = 2;
        Sample mUnisonDetuneCents = 0.6;
        // README ## 11: every register-scaled parameter follows the same
        // override-latch pattern §3's T60_1 established — the law applies from
        // setFrequency() until a caller sets the value explicitly, after which
        // the caller owns it.
        bool mInharmonicityOverridden = false;
        bool mBaseDecayOverridden = false;
        bool mUnisonOverridden = false;
        bool mStrikePositionOverridden = false;
        bool mModalMassOverridden = false;
        bool mHammerMassOverridden = false;
        bool mHammerStiffnessOverridden = false;
        bool mUnaCorda = false;
        bool mDamperHeld = false;

        Sample mHammerBaseStiffness = 0.0; // set per-note by ## 11.3 from the ctor on
        Sample mLastSample = 0.0;

        // Secondary mechanical noise (README ## 10): amplitude decays exponentially
        // each sample from a trigger event; coefficients derived from AudioConfig
        // sample rate in recomputeNoiseCoeffs().
        Sample mThumpAmp = 0.0;
        Sample mDamperNoiseAmp = 0.0;
        Sample mThumpDecayCoeff = 0.0;
        Sample mDamperNoiseDecayCoeff = 0.0;
        uint32_t mNoiseState = 0x9E3779B9u;

        // Velocity -> signal transduction scale. NOT a physics fudge: the bank
        // outputs modal VELOCITY in the normalised unit system anchored by
        // kModalMassAtRef (README ## 6, ## 11.1), and converting that to a
        // line-level signal is a radiation/transduction constant. It is the ONE
        // remaining free scalar — M6 deleted voicingGain, its last companion.
        // Recalibrated at M6 because ## 11.1 moved the unit system's anchor: set so
        // the loudest note on the keyboard peaks just under full scale at velocity
        // 1.0 (C6, 0.88). Note the keyboard is no longer flat in PEAK (15.7x spread)
        // but in LOUDNESS: 300 ms RMS spans 6x with its maximum in the mid register,
        // falling off toward both ends, which is a real piano's contour at constant
        // key velocity. The peak spread is crest factor — the treble's 5 partials
        // align where the bass's 64 do not — and is a property of the instrument.
        static constexpr Sample kVelocityToSignal = 0.0116;

        // README ## 11 — the per-register scaling laws. Reference pitch for all of
        // them is C4, where the modal mass is 1.0 by definition (## 11.1).
        static constexpr Sample kRegisterRefHz = 261.6;
        static constexpr Sample kModalMassExponent = 1.62;    // ## 11.1, fits real scaling +-22%
        static constexpr Sample kModalMassMin = 1e-3;
        static constexpr Sample kModalMassMax = 200.0;
        static constexpr Sample kHammerMassAtRef = 3.7;       // ## 11.2, = 7 g / 1.9 g
        static constexpr Sample kHammerMassExponent = 0.21;
        // ## 11.3 — the physical law: harder felt toward the treble.
        static constexpr Sample kHammerRefStiffness = 1.0e12; // K at C4
        static constexpr Sample kStiffnessExponent = 0.75;
        // ...and the numerical validity limit that overrides it in the top 2.5
        // octaves. The contact ODE is integrated explicitly with a one-sample
        // feedback delay, so once a contact gets short enough (~1.7 ms at 48 kHz)
        // it stops being resolved and starts CREATING energy — measured 201x energy
        // gain at C8 with the physical K. Contact time scales as (m_red/K)^(1/(p+1)),
        // so holding K/m_red below a constant bounds it. kStiffnessStabilityRatio is
        // that constant with a 3.6x safety margin; a unit test asserts the resulting
        // energy bound on all 88 keys, at every velocity.
        static constexpr Sample kStiffnessStabilityRatio = 3.0e12;
        static constexpr Sample kStrikePosAtRef = 0.125;      // ## 11.4
        static constexpr Sample kStrikePosExponent = 0.227;
        static constexpr Sample kStrikePosMin = 1.0 / 15.0;
        static constexpr Sample kStrikePosMax = 0.125;
        // ## 11.5 — placed BETWEEN the boundary notes, not on them: A1 = 55.00 and
        // A#1 = 58.27, F2 = 87.31 and F#2 = 92.50. A threshold sitting exactly on a
        // note's frequency decides that note by floating-point luck.
        static constexpr Sample kUnisonSingleMaxHz = 56.6;    // A0..A1 single-strung
        static constexpr Sample kUnisonDoubleMaxHz = 89.9;    // A#1..F2 bichord; above, trichord

        // README ## 3 pitch -> fundamental-T60 curve (empirical log-log fit).
        static constexpr Sample kDecayRefHz = 261.6;  // C4
        static constexpr Sample kDecayRefT60 = 6.9;   // s, fitted value at C4
        static constexpr Sample kDecaySlope = 0.906;  // least-squares exponent
        static constexpr Sample kDecayMinSeconds = 0.25;
        static constexpr Sample kDecayMaxSeconds = 60.0;
        static constexpr Sample kUnaCordaStiffness = 0.85;
        static constexpr Sample kUnaCordaGain = 0.6;
        static constexpr Sample kVoiceLifetimeMs = 8000.0;
        static constexpr Sample kThumpAmpBase = 0.03;
        static constexpr Sample kThumpTauSeconds = 0.003;
        static constexpr Sample kDamperNoiseAmpFixed = 0.015;
        static constexpr Sample kDamperNoiseTauSeconds = 0.015;
    };
}
