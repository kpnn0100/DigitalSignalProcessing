#include "StringPartialBank.h"
#include "../base/AudioConfig.h"
#include <cmath>

namespace arstro
{
    StringPartialBank::StringPartialBank() : SignalProcessor(propertyCount)
    {
        setSmoothEnable(false); // struck note: parameters jump at note-on, no tone-knob click risk
        initProperty(fundamentalID, 110.0);
        initProperty(inharmonicityID, 0.0002);
        initProperty(baseDecayID, 3.0);
        initProperty(brightnessDecayID, 0.08); // T60 at 5 kHz — real highs die in ~50-150 ms
        initProperty(strikePositionID, 0.125);
        // Struck, not driven: a partial's initial amplitude comes from the hammer
        // force and mode shape, not from its decay time (README ## 1).
        for (auto &p : mPartials)
            p.setImpulseNormalized(true);
        update();
    }

    void StringPartialBank::setFundamentalHz(Sample hz) { setProperty(fundamentalID, hz); }
    void StringPartialBank::setInharmonicity(Sample b) { setProperty(inharmonicityID, b); }
    void StringPartialBank::setBaseDecaySeconds(Sample t60) { setProperty(baseDecayID, t60); }
    void StringPartialBank::setBrightnessDecaySeconds(Sample t60AtRef) { setProperty(brightnessDecayID, t60AtRef); }

    Sample StringPartialBank::partialFrequencyHz(int index) const
    {
        return (index >= 0 && index < kPartialCount) ? mFreqN[index] : (Sample)0;
    }

    Sample StringPartialBank::partialDecaySeconds(int index) const
    {
        return (index >= 0 && index < kPartialCount) ? mBaseT60N[index] : (Sample)0;
    }
    void StringPartialBank::setStrikePosition(Sample beta) { setProperty(strikePositionID, beta); }

    void StringPartialBank::setDamperEngageMs(Sample ms)
    {
        mDamperEngageMs = (ms < 1.0) ? 1.0 : ms;
    }

    void StringPartialBank::setDamperEngagement(Sample target01)
    {
        if (target01 < 0.0) target01 = 0.0;
        if (target01 > 1.0) target01 = 1.0;
        mDamperTarget = target01;
        Sample sr = AudioConfig::instance().sampleRate();
        Sample rampSamples = mDamperEngageMs * 0.001 * sr;
        if (rampSamples < 1.0) rampSamples = 1.0;
        Sample dir = (mDamperTarget >= mDamperValue) ? 1.0 : -1.0;
        mDamperStep = dir / rampSamples;
    }

    void StringPartialBank::reset()
    {
        for (auto &p : mPartials)
            p.reset();
        mDamperValue = 0.0;
        mDamperTarget = 0.0;
        mDamperStep = 0.0;
        // Re-derive each partial's decay coefficient from the now-lifted damper.
        // A reused (voice-stolen) bank can still carry heavily-damped coefficients
        // (short T60 -> large StringResonator input gain G, README ## 1) computed
        // for the PREVIOUS note's fully-engaged damper — PianoEngine calls
        // setFrequency() (which recomputes coefficients from the *current*
        // mDamperValue) before noteOn() (where this reset() runs), so a reused
        // voice's coefficients are briefly stale/wrong otherwise. Clearing history
        // above without this would leave the new note's hammer strike driving a
        // resonator whose gain was calibrated for a ~40x-shorter decay than the
        // fresh string actually has — a large, spurious amplitude spike.
        recomputeEffectivePartials();
    }

    void StringPartialBank::update()
    {
        Sample f0 = getProperty(fundamentalID);
        Sample b = getProperty(inharmonicityID);
        Sample t60_1 = getProperty(baseDecayID);
        Sample t60Ref = getProperty(brightnessDecayID);
        Sample beta = getProperty(strikePositionID);

        // Solve the loss law alpha(f) = c1 + c3*(2*pi*f)^2 from the two decay-time
        // anchors (README ## 3). Positive-time guards first so the reciprocals below
        // stay finite for any caller input.
        if (t60_1 < 1e-3) t60_1 = 1e-3;
        if (t60Ref < 1e-4) t60Ref = 1e-4;

        const Sample f1 = f0 * std::sqrt(1.0 + b); // actual first partial, not f0
        const Sample w1 = 2.0 * M_PI * f1;
        const Sample w1sq = w1 * w1;
        const Sample wRef = 2.0 * M_PI * kLossRefHz;
        const Sample wRefSq = wRef * wRef;
        const Sample alphaLo = kT60Constant / t60_1;
        const Sample alphaHi = kT60Constant / t60Ref;

        Sample c1, c3;
        const Sample denom = wRefSq - w1sq;
        if (denom <= 0.0)
        {
            // Fundamental at/above the reference — no two-point solve exists.
            c3 = 0.0;
            c1 = alphaLo;
        }
        else
        {
            c3 = (alphaHi - alphaLo) / denom;
            if (c3 < 0.0)
                c3 = 0.0; // highs asked to ring longer than the fundamental: unphysical
            c1 = alphaLo - c3 * w1sq;
            if (c1 < 0.0)
            {
                // Treble case: losses are entirely omega^2-dominated. Keep T60 of the
                // fundamental exact and let the 5 kHz anchor go (README ## 3 guard 2).
                c1 = 0.0;
                c3 = alphaLo / w1sq;
            }
        }

        for (int i = 0; i < kPartialCount; ++i)
        {
            int n = i + 1;
            Sample fn = (Sample)n * f0 * std::sqrt(1.0 + b * (Sample)n * (Sample)n);
            Sample wn = 2.0 * M_PI * fn;
            Sample alphaN = c1 + c3 * wn * wn;
            if (alphaN < kMinAlpha)
                alphaN = kMinAlpha;
            Sample t60n = kT60Constant / alphaN;
            Sample gn = std::fabs(std::sin((Sample)n * M_PI * beta));
            mFreqN[i] = fn;
            mBaseT60N[i] = t60n;
            mGainN[i] = gn;
            mPartials[i].setFrequencyHz(fn);
        }
        recomputeEffectivePartials();
    }

    void StringPartialBank::recomputeEffectivePartials()
    {
        for (int i = 0; i < kPartialCount; ++i)
        {
            Sample t60eff = mBaseT60N[i] / (1.0 + mDamperValue * kDamperLossGain);
            mPartials[i].setDecaySeconds(t60eff);
        }
    }

    Sample StringPartialBank::process(Sample forceIn, int channel)
    {
        if (channel == 0 && mDamperValue != mDamperTarget)
        {
            mDamperValue += mDamperStep;
            if ((mDamperStep > 0.0 && mDamperValue >= mDamperTarget) ||
                (mDamperStep < 0.0 && mDamperValue <= mDamperTarget))
                mDamperValue = mDamperTarget;
            recomputeEffectivePartials();
        }

        Sample sum = 0.0;
        for (int i = 0; i < kPartialCount; ++i)
            sum += mPartials[i].out(forceIn * mGainN[i] * kExciteNorm, channel);
        return sum;
    }
}
