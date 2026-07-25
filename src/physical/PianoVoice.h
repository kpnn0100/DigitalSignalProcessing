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
        void setStrikePosition(Sample beta);
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
        void updateVoicingGain();
        void recomputeNoiseCoeffs();
        Sample nextWhiteNoise();

        std::array<StringPartialBank, kMaxUnison> mStrings;
        HammerExciter mHammer;
        LowPassFilter mThumpFilter;
        LowPassFilter mDamperNoiseFilter;
        PianoBridge *mBridge = nullptr;

        int mUnisonCount = 2;
        Sample mUnisonDetuneCents = 0.6;
        bool mInharmonicityOverridden = false;
        bool mBaseDecayOverridden = false;
        bool mUnaCorda = false;
        bool mDamperHeld = false;

        Sample mHammerBaseStiffness = 3.0e11;
        Sample mLastSample = 0.0;

        // Interim stand-in for M6 per-register hammer voicing (see
        // docs/piano-physics-plan.md §M6). With impulse-normalised partials
        // (README ## 1) a struck mode's amplitude falls as ~1/omega, so at equal
        // hammer velocity the bass comes out ~78x louder than the top octave —
        // that spread is REAL physics, not an artifact, and a real piano flattens
        // it with lighter, harder treble hammers and higher treble tension. Until
        // M6 models that properly, one documented curve stands in for it.
        // NOTE: this is NOT the old registerGain, which compensated a bug (the
        // 1/T60 gain artifact) and was deleted at M1 — this compensates physics
        // that the instrument itself also compensates.
        Sample mVoicingGain = 1.0;

        // Secondary mechanical noise (README ## 10): amplitude decays exponentially
        // each sample from a trigger event; coefficients derived from AudioConfig
        // sample rate in recomputeNoiseCoeffs().
        Sample mThumpAmp = 0.0;
        Sample mDamperNoiseAmp = 0.0;
        Sample mThumpDecayCoeff = 0.0;
        Sample mDamperNoiseDecayCoeff = 0.0;
        uint32_t mNoiseState = 0x9E3779B9u;

        // Velocity -> signal transduction scale. NOT a physics fudge: the bank now
        // outputs modal VELOCITY in the normalised unit system fixed by kModalMass
        // (README ## 6), and converting that to a line-level signal is a radiation /
        // transduction constant. M5's bridge is where this properly belongs; until
        // then it is one documented scalar setting the instrument's loudness.
        static constexpr Sample kVelocityToSignal = 0.055;

        // M6 stand-in curve (see mVoicingGain): measured peak ~ f^-0.9, so f^0.8
        // flattens it to ~2.5x across the keyboard, keeping a mild bass emphasis.
        static constexpr Sample kVoicingRefHz = 261.6;
        static constexpr Sample kVoicingExponent = 0.8;

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
