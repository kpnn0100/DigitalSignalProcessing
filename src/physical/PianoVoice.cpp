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
        updateRegisterGain();
    }

    void PianoVoice::setFrequency(Sample hz)
    {
        SignalGenerator::setFrequency(hz);
        applyUnisonFrequencies();
        updateRegisterGain();
    }

    void PianoVoice::updateRegisterGain()
    {
        Sample ratio = frequency() / kRegisterGainRefHz;
        if (ratio < 1e-6) ratio = 1e-6;
        mRegisterGain = std::pow(ratio, kRegisterGainExponent);
        if (mRegisterGain < 0.05) mRegisterGain = 0.05;
        if (mRegisterGain > 3.0) mRegisterGain = 3.0;
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
        for (int k = 0; k < mUnisonCount; ++k)
        {
            Sample offsetNorm = (mUnisonCount <= 1) ? 0.0 : (2.0 * (Sample)k / (Sample)(mUnisonCount - 1) - 1.0);
            Sample detuneCents = mUnisonDetuneCents * offsetNorm;
            Sample f0k = frequency() * std::pow(2.0, detuneCents / 1200.0);
            mStrings[k].setFundamentalHz(f0k);
            if (!mInharmonicityOverridden)
                mStrings[k].setInharmonicity(defaultB);
        }
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

    void PianoVoice::setBaseDecaySeconds(Sample t60)
    {
        for (int k = 0; k < kMaxUnison; ++k)
            mStrings[k].setBaseDecaySeconds(t60);
    }

    void PianoVoice::setDampingExponent(Sample pLoss)
    {
        for (int k = 0; k < kMaxUnison; ++k)
            mStrings[k].setDampingExponent(pLoss);
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

        Sample force = mHammer.out(0.0, 0) * kHammerToStringGain * mRegisterGain;
        if (mUnaCorda)
            force *= kUnaCordaGain;

        Sample feedback = mBridge ? mBridge->feedback() : 0.0;
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

        mLastSample = arstroFlush(stringSum + noise);
        return mLastSample;
    }
}
