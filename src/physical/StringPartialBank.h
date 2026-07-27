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

        // README ## 5b: how many low partials get a second (horizontal) polarisation.
        // Double decay is perceptually a low-partial phenomenon, so this is capped to
        // bound the cost against REQ-piano-17. A documented approximation.
        static constexpr int kPolarizedPartials = 16;

        // README ## 5b — two transverse polarisations. The T60 split is GEOMETRIC about
        // T60_n (vertical /sqrt(R), horizontal *sqrt(R)) so §3's calibrated pitch->decay
        // curve keeps its meaning; giving the horizontal plane R*T60_n outright would
        // multiply every note's overall ring time by up to 8x. Public so tests can
        // reproduce the split (and its predicted crossover time) exactly.
        static constexpr Sample kPolarizationDecayRatio = 8.0; // R_pol
        static constexpr Sample kPolarizationSplit = 0.05;     // eps_pol -> horizontal
        // d_pol (bridge anisotropy). Deliberately SMALL: measured, 3e-4 puts the
        // polarisation beat period BELOW the aftersound decay time above C5, so the
        // beat masquerades as a decay slope and corrupts the very double-decay
        // envelope M4 exists to produce. At 3e-5 the beat is 5-20x longer than the
        // aftersound everywhere, so the two planes stay slightly incoherent (their
        // real role here) without beating. Audible beating is unison detuning's
        // job (README ## 5), not polarisation's.
        static constexpr Sample kPolarizationDetune = 3e-5;
        static constexpr Sample kPolarizationT60Cap = 60.0;    // s; bass must not ring for minutes

        // README ## 6 / ## 11.1: modal mass (rho*L/2), normalised so that m == 1 at
        // C4. Sets how far the string yields under the hammer, so it is calibrated
        // jointly with the hammer's mass/stiffness — the RATIO m_h/m governs the
        // contact, not either alone. Replaces the deleted kHammerToStringGain.
        // NOT a constant any more (M6): real modal mass spans ~3400x across the
        // keyboard, and holding it at 1.0 was what made every note a transposition
        // of every other. PianoVoice drives it from f0; this is the C4 anchor.
        static constexpr Sample kModalMassAtRef = 1.0;

        enum PropertyIndex
        {
            fundamentalID,     // Hz, f0
            inharmonicityID,   // B
            baseDecayID,       // seconds, T60 of the fundamental
            brightnessDecayID, // seconds, T60 at kLossRefHz -> solves c3/c1 (README ## 3)
            strikePositionID,  // 0..0.5, beta
            modalMassID,       // normalised modal mass m (README ## 11.1)
            propertyCount
        };
        StringPartialBank();

        void setFundamentalHz(Sample hz);
        void setInharmonicity(Sample b);
        void setBaseDecaySeconds(Sample t60);
        void setBrightnessDecaySeconds(Sample t60AtRef);
        void setStrikePosition(Sample beta);
        void setModalMass(Sample m); // README ## 11.1; clamped positive

        // README ## 12.5: tension modulation (attack pitch glide). kappaT scales the
        // low-passed slope-energy envelope into a fractional pitch shift applied
        // uniformly to every partial. 0 (the default) disables the stage entirely,
        // so a bank that opts out pays nothing. Not a smoothed property — like the
        // damper, it drives its own state.
        void setTensionModulation(Sample kappaT);

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

        /** Transverse displacement at the strike point as of the most recent
         *  process() call — sum(g_n/omega_n * y_n), README ## 6. This is what the
         *  hammer compresses against; without it the felt meets a rigid wall. */
        Sample displacementAtStrike() const { return mLastDisplacement; }

        // README ## 12.5 introspection, so tests assert the glide directly rather
        // than inferring it from rendered audio: the current fractional pitch shift
        // delta, and the tension-rise envelope E(t) = LPF(slope^2) that drives it.
        Sample pitchModulation() const { return mPitchDelta; }
        Sample tensionEnergy() const { return mEnergyLP; }

        void update() override;
        void onChannelCountChanged() override;

    protected:
        Sample process(Sample forceIn, int channel) override;

    private:
        void recomputeEffectivePartials(); // damper changed -> only the pole radii move
        void ensureChannels();

        // The natural (single-polarisation) partial series — introspection only.
        // Only [0, mActiveCount) is live.
        std::array<Sample, kMaxPartials> mFreqN{};    // f_n
        std::array<Sample, kMaxPartials> mBaseT60N{}; // natural T60_n (pre-split, pre-damper)

        // Per RESONATOR ENTRY. [0, mActiveCount) are the vertical partials;
        // [mActiveCount, mTotalCount) are the horizontal twins of the first
        // mPolarizedCount of them (README ## 5b). One flat loop covers both.
        static constexpr int kMaxEntries = kMaxPartials + kPolarizedPartials;
        std::array<Sample, kMaxEntries> mEntryT60{};    // pre-damper T60 for this entry
        std::array<Sample, kMaxEntries> mCosTheta{};
        std::array<Sample, kMaxEntries> mDrive{};       // sin(theta)*g_n*split/(m*fs)
        std::array<Sample, kMaxEntries> mDispWeight{};  // g_n/omega_n; ZERO for horizontal
        std::array<Sample, kMaxEntries> mA1{};          // 2*r*cos(theta*(1+delta))
        std::array<Sample, kMaxEntries> mA2{};          // r^2
        // README ## 12.5: first-order pitch-bend sensitivity b_n = -theta_n*sin(theta_n),
        // so the bent cosine is cos(theta_n) + b_n*delta with no per-partial trig.
        std::array<Sample, kMaxEntries> mCosBend{};
        int mActiveCount = 1;    // vertical partials (== the physical partial count)
        int mTotalCount = 1;     // vertical + horizontal entries actually run
        Sample mLastDisplacement = 0.0;

        // Per-channel recurrence history, flat: [channel*kMaxEntries + entry].
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

        // ───────── README ## 12.5: tension modulation (attack pitch glide) ─────────
        Sample mTensionCoupling = 0.0; // kappa_t; 0 disables the whole stage
        Sample mEnergyLP = 0.0;        // E(t) = LPF(slope^2), the tension-rise envelope
        Sample mEnergyCoeff = 0.0;     // one-pole coefficient, derived from tau in update()
        Sample mPitchDelta = 0.0;      // current fractional pitch shift delta = kappa_t*E
        int mBendCounter = 0;          // decimation counter for the coefficient re-tune
        // The envelope corner (## 12.5): deliberately BELOW ## 12.2's 60 Hz DC-blocker
        // so a bottom-octave note's 2*f0 ~ 55 Hz ripple cannot wobble the pitch.
        static constexpr Sample kEnergyTauSeconds = 0.013; // ~12 Hz corner
        // The re-tune runs once per this many samples, not per sample: E(t) evolves
        // over tens of ms, so a ~1 ms grid is inaudible, and the coefficient pass
        // must stay off the hot path (the ledger's standing caution).
        static constexpr int kBendRetuneInterval = 64;
        // Stability guard: |delta| capped so a pathological input energy cannot push
        // a pole coefficient out of range. Far above any real glide (2 cents ~ 1.2e-3).
        static constexpr Sample kMaxPitchDelta = 0.05;

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
