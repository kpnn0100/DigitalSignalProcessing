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
        initProperty(dampingExponentID, 0.9);
        initProperty(strikePositionID, 0.125);
        update();
    }

    void StringPartialBank::setFundamentalHz(Sample hz) { setProperty(fundamentalID, hz); }
    void StringPartialBank::setInharmonicity(Sample b) { setProperty(inharmonicityID, b); }
    void StringPartialBank::setBaseDecaySeconds(Sample t60) { setProperty(baseDecayID, t60); }
    void StringPartialBank::setDampingExponent(Sample pLoss) { setProperty(dampingExponentID, pLoss); }
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
    }

    void StringPartialBank::update()
    {
        Sample f0 = getProperty(fundamentalID);
        Sample b = getProperty(inharmonicityID);
        Sample t60_1 = getProperty(baseDecayID);
        Sample pLoss = getProperty(dampingExponentID);
        Sample beta = getProperty(strikePositionID);

        for (int i = 0; i < kPartialCount; ++i)
        {
            int n = i + 1;
            Sample fn = (Sample)n * f0 * std::sqrt(1.0 + b * (Sample)n * (Sample)n);
            Sample t60n = t60_1 / std::pow((Sample)n, pLoss);
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
