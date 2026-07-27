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
#include "LongitudinalBank.h"
#include "DuplexBank.h"
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
        static Sample defaultTensionModulation(Sample f0Hz);// ## 12.5
        static bool   defaultHasDamper(Sample f0Hz);        // ## 7.1 (M9.1)
        static Sample defaultFeltHysteresis(Sample f0Hz);   // ## 6 (M9.3): eps tapers to ~0 in the treble
        static Sample defaultAftersound(Sample f0Hz);        // ## 5b (M11): eps_pol, full bass/mid → 0.05 treble

        void setStrikePosition(Sample beta);
        void setModalMass(Sample m);
        // README ## 5b (M11): the aftersound share (horizontal-polarisation energy) on
        // every unison bank — how much the note sings after the prompt decay.
        void setAftersound(Sample eps);
        // README ## 12.2: displacement^2 -> tension modulation. 0 disables the
        // longitudinal stage entirely (useful for isolating it in tests).
        void setTensionCoupling(Sample kappa);
        // README ## 12.5: the attack pitch glide (κ_t → every unison bank). Latches
        // an override of the §12.5 register default, like the other §11/§12 laws.
        void setTensionModulation(Sample kappaT);
        const LongitudinalBank &longitudinal() const { return mLongitudinal; }
        // README ## 8.1: duplex/aliquot shimmer drive scale (0 disables the stage,
        // useful for isolating it by differencing two renders).
        void setDuplexDriveGain(Sample kappa);
        const DuplexBank &duplex() const { return mDuplex; }
        // Introspection for tests: the first unison bank's current glide state.
        Sample pitchModulation() const { return mStrings[0].pitchModulation(); }
        void setDamperEngageMs(Sample ms);

        void setHammerMass(Sample m);
        void setHammerStiffness(Sample k);
        void setHammerNonlinearExponent(Sample p);
        void setHammerHysteresisLoss(Sample eps);
        // README ## 6 (M9.3): the Stulov relaxation depth. Overrides the register taper
        // until the next setFrequency(); 0 disables the term (isolates it in tests).
        void setHammerRelaxationDepth(Sample eps);

        /** True when this voice is BOTH fully damped and inaudible, so freezing it
         *  changes nothing. Engines may skip such a voice entirely
         *  (PianoEngine does) — one note otherwise costs almost as much as eight,
         *  because every voice in the pool is rendered whether it sounds or not.
         *
         *  The damper condition is not a nicety: an UNdamped string can still be
         *  re-excited through the shared bridge (REQ-piano-6), so freezing it would
         *  break sympathetic resonance. A fully damped string is already gated off
         *  that path by the (1 - damperValue) factor in generate(), so it cannot be
         *  re-excited and cannot become audible again until it is struck — and
         *  noteOn() calls reset() before that. See src/physical/README.md ## 13. */
        bool isSilent() const;

        void setUnaCorda(bool on) { mUnaCorda = on; }
        // README ## 9 (M9.4): how many of the U unison strings the hammer strikes —
        // U normally, U−1 (≥1) under una corda. Public so tests assert the law.
        int struckUnisonCount() const { return (mUnaCorda && mUnisonCount > 1) ? mUnisonCount - 1 : mUnisonCount; }
        // Sustain/sostenuto gate: while true, noteOff() does not engage the damper.
        void setDamperHeld(bool held) { mDamperHeld = held; }
        // Shared soundboard/bridge for sympathetic resonance (REQ-piano-6). Optional —
        // null means the voice still works standalone (no cross-string coupling).
        void setBridge(PianoBridge *bridge) { mBridge = bridge; }
        // README ## 8.2 (M10): body-radiation mix. 0 = string heard raw (M0–M9); 1 =
        // string radiated only through the shared soundboard (series). The engine also
        // scales the board's radiativity by this, so the string level is conserved.
        void setBodyMix(Sample mix);
        // The velocity→line-level transduction constant, exposed so the engine can match
        // the board's radiativity level to the direct string level (README ## 8.2, ## Units).
        static constexpr Sample velocityToSignal() { return kVelocityToSignal; }

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
        // README ## 12.4: one bank per VOICE, not per string — the U unison strings
        // are within a cent of each other and the hammer already drives them from
        // their mean displacement (## 6).
        LongitudinalBank mLongitudinal;
        // README ## 8.1: one duplex bank per voice — the aliquot segments belong to
        // this note's strings. Treble-weighted, off in the bass.
        DuplexBank mDuplex;
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
        bool mTensionModOverridden = false;
        bool mAftersoundOverridden = false;
        bool mUnaCorda = false;
        bool mDamperHeld = false;
        Sample mBodyMix = 0.0; // README ## 8.2 (M10): 0 = raw string, 1 = fully through the board

        Sample mHammerBaseStiffness = 0.0; // set per-note by ## 11.3 from the ctor on
        Sample mLastSample = 0.0;
        // Decaying peak follower over the voice's own output, for isSilent()
        // (README ## 13). One max and one multiply per sample.
        Sample mOutputEnvelope = 0.0;

        // Secondary mechanical noise (README ## 10): amplitude decays exponentially
        // each sample from a trigger event; coefficients derived from AudioConfig
        // sample rate in recomputeNoiseCoeffs().
        Sample mThumpAmp = 0.0;
        Sample mDamperNoiseAmp = 0.0;
        Sample mSilenceRelease = 0.0;
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
        // README ## 12.5 — tension-modulation (pitch-glide) register law. The raw
        // slope-energy E(t) the glide is driven by actually GROWS toward the treble
        // (a top-octave note's modal-velocity sum is larger in the normalised unit
        // system), so κ_t must fall steeply with pitch to make the glide the bass
        // phenomenon it is on a real instrument. κ_t(f0) = κ_ref·(f_ref/f0)^3, but
        // with f0 FLOORED at C2: a bare (f_ref/f0)^3 would give A0 a 861× multiplier
        // and glide it ~20 cents, so κ_t is held flat through the bottom octaves and
        // only falls above the floor. Calibrated (like §12.2's κ) against the level a
        // hard blow shows — κ_ref sets a C2 fortissimo to ~2.5 cents sharp in the
        // first 50 ms (≥2 required) and a pianissimo to ~0.1 cents (<0.5 required),
        // the ~23× (≈v²) hard/soft split doing the separating (plan §M8).
        static constexpr Sample kTensionModRefHz = 261.6;      // C4, the f_ref anchor
        static constexpr Sample kTensionModCapHz = 65.41;      // C2 — κ_t flat below here
        static constexpr Sample kTensionModRefValue = 1.1e-5;  // κ_ref, calibrated
        static constexpr Sample kTensionModExponent = 3.0;
        // Above C5 the glide is < 0.05 cents — inaudible — so κ_t is set to exactly 0
        // there, not merely small. That both makes "negligible in the treble" an exact
        // property and takes those voices off the per-sample tension path entirely
        // (the same off-above-a-crossover economy §12.4's longitudinal bank uses).
        static constexpr Sample kTensionModMaxHz = 523.25;     // C5
        // README ## 6 (M9.3): the Stulov relaxation depth ε is graded by register.
        // Real treble felt is hard and dense — it barely relaxes — and the treble's
        // contact is already stability-capped (§11.3), so a full ε there would dull an
        // attack that must stay bright, breaking M6's centroid rise. So ε is full
        // through the bass/mid (where felt viscoelasticity is prominent, and where the
        // rate-dependence perceptually lives) and tapers to ~0 by the top octaves.
        static constexpr Sample kFeltHysteresisRef = 0.25;    // ε in the bass/mid
        static constexpr Sample kFeltHysteresisCrossHz = 261.6; // C4 — full at/below, tapering above
        static constexpr Sample kFeltHysteresisExp = 3.0;
        // README ## 7.1 (M9.1): the top ~1.5–2 octaves have no dampers. Cutoff is the
        // geometric mean of MIDI 88 (1318.5 Hz) and 89 (1396.9 Hz) so the boundary
        // sits between notes, not on one — MIDI >= 89 (top 20 keys) rings undamped.
        static constexpr Sample kNoDamperAboveHz = 1357.11;
        static constexpr Sample kUnaCordaStiffness = 0.85; // softer, less-compacted felt (## 9)
        static constexpr Sample kVoiceLifetimeMs = 8000.0;
        // README ## 13. Threshold is ~-100 dBFS: far below the 16-bit noise floor
        // (-96 dB) and ~5 orders below a struck note's peak, so nothing audible is
        // ever frozen. The follower's release is slow enough that a note passing
        // briefly through zero cannot trip it.
        static constexpr Sample kSilenceThreshold = 1.0e-5;
        static constexpr Sample kSilenceReleaseMs = 50.0;
        static constexpr Sample kDamperClosedThreshold = 0.999;
        static constexpr Sample kThumpAmpBase = 0.03;
        static constexpr Sample kThumpTauSeconds = 0.003;
        static constexpr Sample kDamperNoiseAmpFixed = 0.015;
        static constexpr Sample kDamperNoiseTauSeconds = 0.015;
    };
}
