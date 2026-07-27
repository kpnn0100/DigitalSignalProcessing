#include "PianoVoice.h"
#include "../base/AudioConfig.h"
#include <cmath>

namespace arstro
{
    PianoVoice::PianoVoice() : SignalGenerator(propertyCount)
    {
        // Transparent envelope (README rule-1/2 note): attack/decay ~0, sustain=1,
        // a generous fixed release backstop. Dynamics come from the hammer physics
        // (noteOn always arms the base envelope at velocity=1.0), not from ADSR.
        setAttackMs(0.0);
        setDecayMs(0.0);
        setSustain(1.0);
        setReleaseMs(kVoiceLifetimeMs);

        mThumpFilter.setCutoffFrequency(4000.0);
        mDamperNoiseFilter.setCutoffFrequency(1500.0);
        recomputeNoiseCoeffs();

        applyRegisterScaling(); // sets hammer mass/stiffness, beta, modal mass, U
        applyUnisonFrequencies();
    }

    void PianoVoice::setFrequency(Sample hz)
    {
        SignalGenerator::setFrequency(hz);
        // README ## 11: the register scaling is a function of f0, so it is re-derived
        // here — before applyUnisonFrequencies(), because ## 11.5 can change how many
        // unison banks that call configures.
        applyRegisterScaling();
        applyUnisonFrequencies();
    }


    Sample PianoVoice::computeDefaultInharmonicity(Sample f0Hz) const
    {
        if (f0Hz < 1.0) f0Hz = 1.0;
        Sample b = 0.00056 * std::pow(100.0 / f0Hz, 1.25);
        if (b < 0.00005) b = 0.00005;
        if (b > 0.02) b = 0.02;
        return b;
    }

    void PianoVoice::applyUnisonFrequencies()
    {
        Sample defaultB = mInharmonicityOverridden ? 0.0 : computeDefaultInharmonicity(frequency());
        // Piano decay spans two orders of magnitude across the keyboard (README ## 3),
        // so the fundamental's T60 is derived from pitch unless explicitly overridden.
        Sample defaultT60 = mBaseDecayOverridden ? 0.0 : defaultBaseDecaySeconds(frequency());
        for (int k = 0; k < mUnisonCount; ++k)
        {
            Sample offsetNorm = (mUnisonCount <= 1) ? 0.0 : (2.0 * (Sample)k / (Sample)(mUnisonCount - 1) - 1.0);
            Sample detuneCents = mUnisonDetuneCents * offsetNorm;
            Sample f0k = frequency() * std::pow(2.0, detuneCents / 1200.0);
            mStrings[k].setFundamentalHz(f0k);
            if (!mInharmonicityOverridden)
                mStrings[k].setInharmonicity(defaultB);
            if (!mBaseDecayOverridden)
                mStrings[k].setBaseDecaySeconds(defaultT60);
        }
    }

    // ───────────────── README ## 11: per-register scaling laws ─────────────────
    // All five are pure functions of f0 with no state, so tests can assert the laws
    // directly. Guard f0 once, here, rather than in each law.

    Sample PianoVoice::defaultModalMass(Sample f0Hz)
    {
        // ## 11.1: m(f0) = (f_ref/f0)^1.62, normalised to m(C4) = 1. The clamps bound
        // the drive gain 1/m for nonsense pitches; they sit far outside A0..C8
        // (38.4 at A0, 0.0112 at C8), so no real note ever reaches them.
        if (f0Hz < 1.0) f0Hz = 1.0;
        Sample m = std::pow(kRegisterRefHz / f0Hz, kModalMassExponent);
        if (m < kModalMassMin) m = kModalMassMin;
        if (m > kModalMassMax) m = kModalMassMax;
        return m;
    }

    Sample PianoVoice::defaultHammerMass(Sample f0Hz)
    {
        // ## 11.2: m_h barely halves across a compass over which m falls 3400x. The
        // resulting ratio m_h/m spans 0.15 (A0) to 184 (C8) — two opposite contact
        // regimes, which is exactly the point.
        if (f0Hz < 1.0) f0Hz = 1.0;
        return kHammerMassAtRef * std::pow(kRegisterRefHz / f0Hz, kHammerMassExponent);
    }

    Sample PianoVoice::defaultHammerStiffness(Sample f0Hz)
    {
        // ## 11.3: harder felt toward the treble...
        if (f0Hz < 1.0) f0Hz = 1.0;
        const Sample physical = kHammerRefStiffness * std::pow(f0Hz / kRegisterRefHz, kStiffnessExponent);

        // ...capped by what the explicit contact integration can actually resolve.
        // Contact time ~ (m_red/K)^(1/(p+1)), so bounding K/m_red bounds it from
        // below. m_red is the hammer/string reduced mass — the inertia the felt
        // spring actually works against.
        const Sample m = defaultModalMass(f0Hz);
        const Sample mh = defaultHammerMass(f0Hz);
        const Sample mReduced = (mh * m) / (mh + m);
        const Sample maxStable = kStiffnessStabilityRatio * mReduced;
        return (physical < maxStable) ? physical : maxStable;
    }

    Sample PianoVoice::defaultStrikePosition(Sample f0Hz)
    {
        // ## 11.4: ~1/8 through bass and tenor (held there by the upper clamp),
        // falling to 1/15 at the top of the compass.
        if (f0Hz < 1.0) f0Hz = 1.0;
        Sample beta = kStrikePosAtRef * std::pow(kRegisterRefHz / f0Hz, kStrikePosExponent);
        if (beta < kStrikePosMin) beta = kStrikePosMin;
        if (beta > kStrikePosMax) beta = kStrikePosMax;
        return beta;
    }

    Sample PianoVoice::defaultTensionModulation(Sample f0Hz)
    {
        // ## 12.5: κ_t = κ_ref·(f_ref/f0)^3, with f0 floored at C2 so it holds flat
        // through the bottom octaves rather than exploding (a bare cube would give A0
        // an 861× multiplier). Steep enough that the mid falls to a fraction of a cent;
        // above C5 it is switched off entirely (glide < 0.05 cents, inaudible), which
        // also keeps the treble off the per-sample tension path.
        if (f0Hz > kTensionModMaxHz) return 0.0;
        if (f0Hz < kTensionModCapHz) f0Hz = kTensionModCapHz;
        return kTensionModRefValue * std::pow(kTensionModRefHz / f0Hz, kTensionModExponent);
    }

    Sample PianoVoice::defaultFeltHysteresis(Sample f0Hz)
    {
        // ## 6 (M9.3): ε = ε_ref·(f_cross/f0)^1.5, clamped to ε_ref below C4. Full in the
        // bass/mid, ~0 by the treble (hard felt + preserve the M6 centroid rise).
        if (f0Hz < 1.0) f0Hz = 1.0;
        Sample g = std::pow(kFeltHysteresisCrossHz / f0Hz, kFeltHysteresisExp);
        if (g > 1.0) g = 1.0;
        return kFeltHysteresisRef * g;
    }

    bool PianoVoice::defaultHasDamper(Sample f0Hz)
    {
        // ## 7.1: no dampers on the top ~1.5–2 octaves. Below the cutoff the note is
        // damped as normal; at or above it the string always rings.
        return f0Hz < kNoDamperAboveHz;
    }

    int PianoVoice::defaultUnisonCount(Sample f0Hz)
    {
        // ## 11.5: standard stringing-scale breaks. Single-strung bass is a feature —
        // no unison beating there, its multi-stage decay comes from ## 5b instead.
        if (f0Hz < kUnisonSingleMaxHz) return 1;
        if (f0Hz < kUnisonDoubleMaxHz) return 2;
        return 3;
    }

    void PianoVoice::applyRegisterScaling()
    {
        const Sample f0 = frequency();
        if (!mUnisonOverridden)
            mUnisonCount = defaultUnisonCount(f0);
        mLongitudinal.setFundamentalHz(f0); // README ## 12: mode set + register gain
        mDuplex.setFundamentalHz(f0);        // README ## 8.1: aliquot tuning + treble gain
        if (!mHammerMassOverridden)
            mHammer.setMass(defaultHammerMass(f0));
        if (!mHammerStiffnessOverridden)
            mHammerBaseStiffness = defaultHammerStiffness(f0);
        // Stiffness reaches the hammer via noteOn() (which applies the una-corda
        // factor), but a voice that is never struck should still report the right
        // value, and unit tests read it back.
        mHammer.setStiffness(mHammerBaseStiffness);
        // README ## 6 (M9.3): taper the Stulov relaxation depth to ~0 in the treble,
        // full in the bass/mid. The base load/unload asymmetry (setHysteresisLoss) is
        // left at the hammer default, unchanged from the M0 model.
        mHammer.setRelaxationDepth(defaultFeltHysteresis(f0));
        if (!mStrikePositionOverridden || !mModalMassOverridden || !mTensionModOverridden)
        {
            const Sample beta = defaultStrikePosition(f0);
            const Sample m = defaultModalMass(f0);
            const Sample kappaT = defaultTensionModulation(f0); // README ## 12.5
            for (int k = 0; k < kMaxUnison; ++k)
            {
                if (!mStrikePositionOverridden) mStrings[k].setStrikePosition(beta);
                if (!mModalMassOverridden) mStrings[k].setModalMass(m);
                if (!mTensionModOverridden) mStrings[k].setTensionModulation(kappaT);
            }
        }
    }

    void PianoVoice::setUnisonCount(int count)
    {
        if (count < 1) count = 1;
        if (count > kMaxUnison) count = kMaxUnison;
        mUnisonOverridden = true; // stop setFrequency() reapplying README ## 11.5
        mUnisonCount = count;
        applyUnisonFrequencies();
    }

    void PianoVoice::setUnisonDetuneCents(Sample cents)
    {
        mUnisonDetuneCents = cents;
        applyUnisonFrequencies();
    }

    void PianoVoice::setInharmonicity(Sample b)
    {
        mInharmonicityOverridden = true;
        for (int k = 0; k < kMaxUnison; ++k)
            mStrings[k].setInharmonicity(b);
    }

    Sample PianoVoice::defaultBaseDecaySeconds(Sample f0Hz)
    {
        if (f0Hz < 1.0) f0Hz = 1.0;
        Sample t60 = kDecayRefT60 * std::pow(kDecayRefHz / f0Hz, kDecaySlope);
        if (t60 < kDecayMinSeconds) t60 = kDecayMinSeconds;
        if (t60 > kDecayMaxSeconds) t60 = kDecayMaxSeconds;
        return t60;
    }

    void PianoVoice::setBaseDecaySeconds(Sample t60)
    {
        mBaseDecayOverridden = true; // stop setFrequency() reapplying the pitch default
        for (int k = 0; k < kMaxUnison; ++k)
            mStrings[k].setBaseDecaySeconds(t60);
    }

    void PianoVoice::setBrightnessDecaySeconds(Sample t60AtRef)
    {
        for (int k = 0; k < kMaxUnison; ++k)
            mStrings[k].setBrightnessDecaySeconds(t60AtRef);
    }

    void PianoVoice::setStrikePosition(Sample beta)
    {
        mStrikePositionOverridden = true; // stop setFrequency() reapplying README ## 11.4
        for (int k = 0; k < kMaxUnison; ++k)
            mStrings[k].setStrikePosition(beta);
    }

    void PianoVoice::setTensionCoupling(Sample kappa) { mLongitudinal.setTensionCoupling(kappa); }

    void PianoVoice::setDuplexDriveGain(Sample kappa) { mDuplex.setDriveGain(kappa); }

    void PianoVoice::setBodyMix(Sample mix)
    {
        if (mix < 0.0) mix = 0.0;
        if (mix > 1.0) mix = 1.0;
        mBodyMix = mix;
    }

    void PianoVoice::setTensionModulation(Sample kappaT)
    {
        mTensionModOverridden = true; // stop setFrequency() reapplying README ## 12.5
        for (int k = 0; k < kMaxUnison; ++k)
            mStrings[k].setTensionModulation(kappaT);
    }

    void PianoVoice::setModalMass(Sample m)
    {
        mModalMassOverridden = true; // stop setFrequency() reapplying README ## 11.1
        for (int k = 0; k < kMaxUnison; ++k)
            mStrings[k].setModalMass(m);
    }

    void PianoVoice::setDamperEngageMs(Sample ms)
    {
        for (int k = 0; k < kMaxUnison; ++k)
            mStrings[k].setDamperEngageMs(ms);
    }

    void PianoVoice::setHammerMass(Sample m)
    {
        mHammerMassOverridden = true; // stop setFrequency() reapplying README ## 11.2
        mHammer.setMass(m);
    }
    void PianoVoice::setHammerNonlinearExponent(Sample p) { mHammer.setNonlinearExponent(p); }
    void PianoVoice::setHammerHysteresisLoss(Sample eps) { mHammer.setHysteresisLoss(eps); }
    void PianoVoice::setHammerRelaxationDepth(Sample eps) { mHammer.setRelaxationDepth(eps); }
    void PianoVoice::setHammerStiffness(Sample k)
    {
        mHammerStiffnessOverridden = true; // stop setFrequency() reapplying README ## 11.3
        mHammerBaseStiffness = k;
        mHammer.setStiffness(k);
    }

    void PianoVoice::noteOn(Sample velocity)
    {
        // Transparent envelope arm — see ctor comment. Real dynamics go to the hammer.
        SignalGenerator::noteOn(1.0);

        Sample v = velocity;
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;

        Sample effStiffness = mUnaCorda ? mHammerBaseStiffness * kUnaCordaStiffness : mHammerBaseStiffness;
        mHammer.setStiffness(effStiffness);
        mHammer.strike(v);

        mThumpAmp = kThumpAmpBase * v;

        // Voice-stealing safety (REQ-piano-12): a reused voice may still hold the
        // previous note's resonator history. Reset before applying new
        // frequencies/decays, or stale history under new coefficients can produce
        // a large transient. A fresh string's damper also starts lifted.
        for (int k = 0; k < kMaxUnison; ++k)
            mStrings[k].reset();
        mLongitudinal.reset(); // same voice-steal reasoning (README ## 12)
        mDuplex.reset();       // README ## 8.1
        mOutputEnvelope = 0.0; // a struck voice is not silent (README ## 13)
    }

    void PianoVoice::noteOff()
    {
        SignalGenerator::noteOff(); // long backstop release (kVoiceLifetimeMs) — see README ## isFinished note
        // README ## 7.1 (M9.1): the top ~1.5–2 octaves have no damper, so releasing
        // the key leaves the string ringing — no ramp to seat, no damper-release noise.
        if (!mDamperHeld && defaultHasDamper(frequency()))
        {
            for (int k = 0; k < mUnisonCount; ++k)
                mStrings[k].setDamperEngagement(1.0);
            mDamperNoiseAmp = kDamperNoiseAmpFixed;
        }
    }

    void PianoVoice::onSampleRateChanged() { recomputeNoiseCoeffs(); }

    void PianoVoice::recomputeNoiseCoeffs()
    {
        Sample sr = AudioConfig::instance().sampleRate();
        mThumpDecayCoeff = std::exp(-1.0 / (kThumpTauSeconds * sr));
        mDamperNoiseDecayCoeff = std::exp(-1.0 / (kDamperNoiseTauSeconds * sr));
        mSilenceRelease = std::exp(-1.0 / (kSilenceReleaseMs * 0.001 * sr));
    }

    bool PianoVoice::isSilent() const
    {
        // Both conditions are required — see the header. Damped first: it is the
        // cheap check and the one that makes freezing physically safe.
        return mStrings[0].damperValue() >= kDamperClosedThreshold &&
               mOutputEnvelope < kSilenceThreshold;
    }

    Sample PianoVoice::nextWhiteNoise()
    {
        mNoiseState ^= mNoiseState << 13;
        mNoiseState ^= mNoiseState >> 17;
        mNoiseState ^= mNoiseState << 5;
        return ((Sample)(mNoiseState & 0xFFFFFFu) / (Sample)0x1000000u) * 2.0 - 1.0;
    }

    Sample PianoVoice::generate(int channel)
    {
        // Mono physics core (REQ-piano-13): only channel 0 advances the simulation;
        // other channels replay the cached value. Requires channel 0 to be rendered
        // first for a given sample — see class-doc comment in PianoVoice.h.
        if (channel != 0)
            return mLastSample;

        // M3 coupling (README ## 6): the felt compresses against the string's actual
        // displacement, not a rigid wall. M9.4 (README ## 9): under una corda the
        // hammer strikes only a SUBSET of the unison strings, so it feels the mean
        // displacement of the STRUCK ones and its reaction force is shared among them.
        const int struck = struckUnisonCount(); // == mUnisonCount unless una corda
        Sample stringDisp = 0.0;
        for (int k = 0; k < struck; ++k)
            stringDisp += mStrings[k].displacementAtStrike();
        stringDisp /= (Sample)struck;

        // No voicingGain here any more (M6): the bass/treble loudness spread it used
        // to flatten is now produced — and self-corrected — by the register scaling
        // of README ## 11. The hammer's energy is normalised over the NOMINAL unison
        // count but (## 9) delivered only to the struck strings, so una corda drives
        // S/U of the normal energy — no kUnaCordaGain fudge any more.
        Sample force = mHammer.out(stringDisp, 0) / (Sample)mUnisonCount;

        // Gate sympathetic feedback by how engaged the damper is (README ## 8):
        // a real damper mutes the string's response to ANY driving, not only its
        // own free decay. Without this gate a damped string keeps accepting bridge
        // energy at full strength while supposedly being silenced, which is both
        // unphysical and a needless feedback path.
        Sample damping = mStrings[0].damperValue();
        Sample feedback = mBridge ? mBridge->feedback() * (1.0 - damping) : 0.0;

        // README ## 9: struck strings get hammer force + bridge feedback; the
        // un-struck string(s) under una corda get ONLY the bridge feedback, so they
        // ring as a sympathetic halo rather than being driven by the hammer.
        Sample stringSum = 0.0;
        for (int k = 0; k < mUnisonCount; ++k)
        {
            const Sample drive = (k < struck) ? (force + feedback) : feedback;
            stringSum += mStrings[k].out(drive, 0);
        }

        // M7 (README ## 12): longitudinal modes. Driven by the SQUARE of the summed
        // modal velocity — which is the string's slope, since y_n = omega_n*q_n
        // weights mode n by n exactly as d(y)/dx does, so no new state is needed.
        // The nonlinearity puts energy at 2*f_i and f_i +/- f_j, where the
        // transverse series has no partial at all. The bank switches itself off
        // above the ## 12.4 crossover, so the treble pays nothing for it.
        const Sample transverse = stringSum; // the speaking string's motion, pre-longitudinal
        const Sample longitudinal = mLongitudinal.out(transverse, 0);
        stringSum += longitudinal;

        // M9.2 (README ## 8.1): the duplex/aliquot segments ring sympathetically off
        // the speaking string's transverse motion and add a treble shimmer. Driven
        // feed-forward and radiated ONLY — deliberately NOT summed into stringSum
        // before the bridge accumulate below, so it cannot form a bridge feedback loop.
        const Sample duplex = mDuplex.out(transverse, 0);

        Sample noise = 0.0;
        if (mThumpAmp > 1e-6)
        {
            noise += mThumpFilter.out(nextWhiteNoise() * mThumpAmp);
            mThumpAmp *= mThumpDecayCoeff;
        }
        if (mDamperNoiseAmp > 1e-6)
        {
            noise += mDamperNoiseFilter.out(nextWhiteNoise() * mDamperNoiseAmp);
            mDamperNoiseAmp *= mDamperNoiseDecayCoeff;
        }

        if (mBridge)
            mBridge->accumulate(stringSum); // README ## 8.1: bridge sees string+longitudinal, not duplex

        // README ## 8.2 (M10): body-radiation mix. (1−mBodyMix) of the string is heard raw;
        // the rest leaves through the board's radiativity, supplied by the shared bridge at
        // the matching level. duplex + mechanical noise stay direct.
        mLastSample = arstroFlush((stringSum * (1.0 - mBodyMix) + duplex) * kVelocityToSignal + noise);

        // Peak follower for isSilent() (README ## 13).
        const Sample mag = std::fabs(mLastSample);
        mOutputEnvelope = arstroFlush(mag > mOutputEnvelope ? mag
                                                            : mOutputEnvelope * mSilenceRelease);
        return mLastSample;
    }
}
