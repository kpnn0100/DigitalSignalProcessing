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
        void setInharmonicity(Sample b);       // overrides the register-dependent default
        void setBaseDecaySeconds(Sample t60);
        void setDampingExponent(Sample pLoss);
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
        void updateRegisterGain();
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
        bool mUnaCorda = false;
        bool mDamperHeld = false;

        Sample mHammerBaseStiffness = 1.0e10;
        Sample mLastSample = 0.0;
        // Register (voicing) compensation — see README ## Units / calibration note.
        // One HammerExciter model is reused for every register, but a bass note
        // packs many more of its 12 partials into the resonators' effective range
        // than a treble note does (whose high partials clamp near Nyquist), so raw
        // output would swing >>1 in the bass and be nearly inaudible in the treble.
        // Real pianos counter this with per-register hammer/string voicing; this is
        // the same idea as one documented empirical gain curve.
        Sample mRegisterGain = 1.0;

        // Secondary mechanical noise (README ## 10): amplitude decays exponentially
        // each sample from a trigger event; coefficients derived from AudioConfig
        // sample rate in recomputeNoiseCoeffs().
        Sample mThumpAmp = 0.0;
        Sample mDamperNoiseAmp = 0.0;
        Sample mThumpDecayCoeff = 0.0;
        Sample mDamperNoiseDecayCoeff = 0.0;
        uint32_t mNoiseState = 0x9E3779B9u;

        // Empirical calibration gain (README ## Units): converts HammerExciter's
        // normalized contact-force units into the resonator bank's signal-level
        // units, so a full-velocity strike lands near +-1 like every other
        // SignalGenerator in this codebase, not derived from a physical unit system.
        static constexpr Sample kHammerToStringGain = 0.010;
        static constexpr Sample kRegisterGainExponent = 0.6; // README ## Units voicing curve
        static constexpr Sample kRegisterGainRefHz = 220.0;
        static constexpr Sample kUnaCordaStiffness = 0.85;
        static constexpr Sample kUnaCordaGain = 0.6;
        static constexpr Sample kVoiceLifetimeMs = 8000.0;
        static constexpr Sample kThumpAmpBase = 0.03;
        static constexpr Sample kThumpTauSeconds = 0.003;
        static constexpr Sample kDamperNoiseAmpFixed = 0.015;
        static constexpr Sample kDamperNoiseTauSeconds = 0.015;
    };
}
