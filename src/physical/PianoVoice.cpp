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

        mHammer.setStiffness(mHammerBaseStiffness);
        applyUnisonFrequencies();
        updateVoicingGain();
    }

    void PianoVoice::setFrequency(Sample hz)
    {
        SignalGenerator::setFrequency(hz);
        applyUnisonFrequencies();
        updateVoicingGain();
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

    void PianoVoice::updateVoicingGain()
    {
        Sample ratio = frequency() / kVoicingRefHz;
        if (ratio < 1e-6) ratio = 1e-6;
        mVoicingGain = std::pow(ratio, kVoicingExponent);
        if (mVoicingGain < 0.1) mVoicingGain = 0.1;
        if (mVoicingGain > 10.0) mVoicingGain = 10.0;
    }

    void PianoVoice::setUnisonCount(int count)
    {
        if (count < 1) count = 1;
        if (count > kMaxUnison) count = kMaxUnison;
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
        for (int k = 0; k < kMaxUnison; ++k)
            mStrings[k].setStrikePosition(beta);
    }

    void PianoVoice::setDamperEngageMs(Sample ms)
    {
        for (int k = 0; k < kMaxUnison; ++k)
            mStrings[k].setDamperEngageMs(ms);
    }

    void PianoVoice::setHammerMass(Sample m) { mHammer.setMass(m); }
    void PianoVoice::setHammerNonlinearExponent(Sample p) { mHammer.setNonlinearExponent(p); }
    void PianoVoice::setHammerHysteresisLoss(Sample eps) { mHammer.setHysteresisLoss(eps); }
    void PianoVoice::setHammerStiffness(Sample k) { mHammerBaseStiffness = k; }

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
    }

    void PianoVoice::noteOff()
    {
        SignalGenerator::noteOff(); // long backstop release (kVoiceLifetimeMs) — see README ## isFinished note
        if (!mDamperHeld)
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
        // displacement, not a rigid wall. The hammer contacts all U unison strings at
        // once, so it feels their MEAN displacement and its reaction force is shared
        // among them — a lumped approximation of U parallel contacts.
        Sample stringDisp = 0.0;
        for (int k = 0; k < mUnisonCount; ++k)
            stringDisp += mStrings[k].displacementAtStrike();
        stringDisp /= (Sample)mUnisonCount;

        Sample force = mHammer.out(stringDisp, 0) * mVoicingGain / (Sample)mUnisonCount;
        if (mUnaCorda)
            force *= kUnaCordaGain;

        // Gate sympathetic feedback by how engaged the damper is (README ## 8):
        // a real damper mutes the string's response to ANY driving, not only its
        // own free decay. Without this gate a damped string keeps accepting bridge
        // energy at full strength while supposedly being silenced, which is both
        // unphysical and a needless feedback path.
        Sample damping = mStrings[0].damperValue();
        Sample feedback = mBridge ? mBridge->feedback() * (1.0 - damping) : 0.0;
        Sample totalDrive = force + feedback;

        Sample stringSum = 0.0;
        for (int k = 0; k < mUnisonCount; ++k)
            stringSum += mStrings[k].out(totalDrive, 0);

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
            mBridge->accumulate(stringSum);

        mLastSample = arstroFlush(stringSum * kVelocityToSignal + noise);
        return mLastSample;
    }
}
