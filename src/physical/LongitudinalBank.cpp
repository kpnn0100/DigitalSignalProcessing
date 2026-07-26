#include "LongitudinalBank.h"
#include "../base/AudioConfig.h"
#include <cmath>

namespace arstro
{
    LongitudinalBank::LongitudinalBank() : SignalProcessor(propertyCount)
    {
        setSmoothEnable(false); // struck note: parameters jump at note-on, no knob-click risk
        initProperty(fundamentalID, 110.0);
        // README ## 12.2: kappa converts slope^2 into tension modulation. Calibrated
        // (not derived) — the exact constant is E*A/2L in SI, but every term of that
        // is in the normalised unit system of ## 6/## 11.1, so it is measured
        // against the level a real bass note shows instead. At this value A0 at full
        // velocity puts the longitudinal signal 19.8 dB below the note's peak, and
        // the transverse behaviour is untouched (the fundamental's velocity exponent
        // is unchanged at 0.85, so the coupling is not distorting the string).
        initProperty(tensionCouplingID, 8.0e-3);
        mDcTracker.setCutoffFrequency(kDcBlockerHz);
        update();
    }

    void LongitudinalBank::setFundamentalHz(Sample hz) { setProperty(fundamentalID, hz); }
    void LongitudinalBank::setTensionCoupling(Sample kappa) { setProperty(tensionCouplingID, kappa); }

    Sample LongitudinalBank::registerGainFor(Sample f0Hz)
    {
        // README ## 12.4: full strength through the bottom two octaves, ~0.35 by C4,
        // ~0.04 by C6 — where a real instrument's longitudinal colour lives.
        if (f0Hz < 1.0) f0Hz = 1.0;
        Sample g = std::pow(kCrossoverHz / f0Hz, kRegisterExponent);
        if (g > 1.0) g = 1.0;
        return g;
    }

    Sample LongitudinalBank::firstModeHz(Sample f0Hz)
    {
        // README ## 12.1: f_long,1 = c_L/(2L), with L fitted from real string
        // scaling. Set by GEOMETRY, not by tension — which is exactly why the
        // longitudinal series is inharmonic against the note and reads as clang.
        if (f0Hz < 1.0) f0Hz = 1.0;
        return kFirstModeAtRefHz * std::pow(f0Hz / kRegisterRefHz, kLengthExponent);
    }

    void LongitudinalBank::reset()
    {
        for (int i = 0; i < kMaxModes; ++i)
            mModes[i].reset();
        // LowPassFilterBase::reset() is protected in that hierarchy, but its state
        // is one public member — clear it directly rather than widen an unrelated
        // class's API for one caller.
        mDcTracker.filteredValue = 0.0;
    }

    void LongitudinalBank::update()
    {
        const Sample f0 = getProperty(fundamentalID);
        const Sample sr = AudioConfig::instance().sampleRate();
        const Sample nyquistLimit = kNyquistFraction * sr;

        mRegisterGain = registerGainFor(f0);
        // README ## 12.4: above the crossover the bank is switched OFF, not merely
        // quiet — no resonators run at all, so the treble pays nothing for an
        // effect it cannot produce.
        mActive = (mRegisterGain >= kMinAudibleGain);
        if (!mActive)
        {
            mModeCount = 0;
            return;
        }

        const Sample f1 = firstModeHz(f0);
        mModeCount = 0;
        for (int m = 0; m < kMaxModes; ++m)
        {
            const Sample fm = (Sample)(m + 1) * f1;
            if (fm >= nyquistLimit)
                break;
            Sample t60 = kFirstModeT60 * std::pow(f1 / fm, kT60Exponent);
            if (t60 < kT60Min) t60 = kT60Min;
            if (t60 > kT60Max) t60 = kT60Max;

            mModes[m].setImpulseNormalized(true); // struck, like the string partials (## 1)
            mModes[m].setFrequencyHz(fm);
            mModes[m].setDecaySeconds(t60);
            // Higher longitudinal modes carry progressively less energy; 1/m is the
            // same falloff the transverse drive gets from the mode shape (## 4)
            // averaged over strike positions. Incoherent-sum normalisation as in
            // ## 8, so changing kMaxModes cannot rescale the output level.
            mModeWeight[m] = (1.0 / (Sample)(m + 1)) / std::sqrt((Sample)kMaxModes);
            mModeCount = m + 1;
        }
    }

    Sample LongitudinalBank::process(Sample transverseSlope, int channel)
    {
        // Mono physics (REQ-piano-13): only channel 0 advances the bank.
        if (channel != 0 || !mActive)
            return 0.0;

        // README ## 12.2: the whole mechanism. Squaring generates 2*f_i and
        // f_i +/- f_j — frequencies at which ## 2's series predicts no partial —
        // and makes the result scale as amplitude^2, so it grows far faster with
        // strike velocity than any real partial and vanishes at zero velocity.
        const Sample squared = transverseSlope * transverseSlope *
                               getProperty(tensionCouplingID);

        // HP(x) = x - LPF(x): remove the DC term of x^2, which is the STATIC
        // tension rise (a pitch shift, i.e. M8's subject) rather than a driving
        // force, and which a two-pole resonator would otherwise pass through as an
        // output offset (its DC gain is ~6 at 1.3 kHz, not 0).
        const Sample drive = squared - mDcTracker.out(squared, 0);

        // ## 12.3: quasi-static response PLUS resonances. The direct term is what
        // carries phantom partials at combination frequencies that do not happen
        // to land on a longitudinal mode — which is most of them.
        Sample sum = kDirectWeight * drive;
        for (int m = 0; m < mModeCount; ++m)
            sum += mModeWeight[m] * mModes[m].out(drive, 0);

        return arstroFlush(sum * mRegisterGain);
    }
}
