#include "StringPartialBank.h"
#include "../base/AudioConfig.h"
#include <cmath>
#include <algorithm>

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
        ensureChannels();
        update();
    }

    void StringPartialBank::ensureChannels()
    {
        const size_t n = (size_t)AudioConfig::instance().channelCount() * (size_t)kMaxPartials;
        if (mY1.size() != n)
        {
            mY1.assign(n, 0.0);
            mY2.assign(n, 0.0);
        }
    }

    void StringPartialBank::onChannelCountChanged() { ensureChannels(); }

    void StringPartialBank::setFundamentalHz(Sample hz) { setProperty(fundamentalID, hz); }
    void StringPartialBank::setInharmonicity(Sample b) { setProperty(inharmonicityID, b); }
    void StringPartialBank::setBaseDecaySeconds(Sample t60) { setProperty(baseDecayID, t60); }
    void StringPartialBank::setBrightnessDecaySeconds(Sample t60AtRef) { setProperty(brightnessDecayID, t60AtRef); }
    void StringPartialBank::setStrikePosition(Sample beta) { setProperty(strikePositionID, beta); }

    Sample StringPartialBank::partialFrequencyHz(int index) const
    {
        return (index >= 0 && index < mActiveCount) ? mFreqN[index] : (Sample)0;
    }

    Sample StringPartialBank::partialDecaySeconds(int index) const
    {
        return (index >= 0 && index < mActiveCount) ? mBaseT60N[index] : (Sample)0;
    }

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
        std::fill(mY1.begin(), mY1.end(), (Sample)0);
        std::fill(mY2.begin(), mY2.end(), (Sample)0);
        mDamperValue = 0.0;
        mDamperTarget = 0.0;
        mDamperStep = 0.0;
        // Re-derive the pole radii from the now-lifted damper. A reused
        // (voice-stolen) bank can still carry heavily-damped coefficients computed
        // for the PREVIOUS note's fully-engaged damper — PianoEngine calls
        // setFrequency() (which recomputes from the *current* mDamperValue) before
        // noteOn() (where this reset() runs), so a reused voice's coefficients are
        // briefly stale otherwise: the new note's hammer would strike a resonator
        // calibrated for a ~40x-shorter decay, a large spurious amplitude spike.
        recomputeEffectivePartials();
    }

    void StringPartialBank::update()
    {
        Sample f0 = getProperty(fundamentalID);
        Sample b = getProperty(inharmonicityID);
        Sample t60_1 = getProperty(baseDecayID);
        Sample t60Ref = getProperty(brightnessDecayID);
        Sample beta = getProperty(strikePositionID);

        const Sample sr = AudioConfig::instance().sampleRate();
        const Sample nyquistLimit = kNyquistFraction * sr;

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

        // How many partials fit below Nyquist (README ## 2). Evaluated on the
        // INHARMONIC f_n — stiffness stretches the series well beyond n*f0, so far
        // fewer partials fit than harmonic spacing suggests. At least one always
        // survives, so a (nonsensical) ultrasonic f0 still yields a usable bank.
        int active = 0;
        for (int i = 0; i < kMaxPartials; ++i)
        {
            const Sample n = (Sample)(i + 1);
            const Sample fn = n * f0 * std::sqrt(1.0 + b * n * n);
            if (i > 0 && fn >= nyquistLimit)
                break;
            mFreqN[i] = fn;
            active = i + 1;
        }
        mActiveCount = active;

        for (int i = 0; i < mActiveCount; ++i)
        {
            // Frequency guard mirrors StringResonator's: the first partial can be
            // above the limit when f0 itself is (see loop above).
            Sample fn = mFreqN[i];
            if (fn < 1.0) fn = 1.0;
            if (fn > 0.49 * sr) fn = 0.49 * sr;
            mFreqN[i] = fn;

            const Sample wn = 2.0 * M_PI * fn;
            Sample alphaN = c1 + c3 * wn * wn;
            if (alphaN < kMinAlpha)
                alphaN = kMinAlpha;
            mBaseT60N[i] = kT60Constant / alphaN;

            const Sample theta = wn / sr;
            mCosTheta[i] = std::cos(theta);
            // Impulse normalisation (README ## 1): G = sin(theta), independent of
            // decay, folded together with the mode-shape gain so process() does one
            // multiply. Deliberately NOT scaled by 1/N_active — modal superposition
            // has no such factor, and now that N varies with pitch (README ## 2) it
            // would make a bass note's fundamental quieter purely because the note
            // carries more partials. See README ## 4.
            const Sample gn = std::fabs(std::sin((Sample)(i + 1) * M_PI * beta));
            mDrive[i] = std::sin(theta) * gn;
        }
        recomputeEffectivePartials();
    }

    void StringPartialBank::recomputeEffectivePartials()
    {
        const Sample sr = AudioConfig::instance().sampleRate();
        const Sample damperScale = 1.0 + mDamperValue * kDamperLossGain;
        for (int i = 0; i < mActiveCount; ++i)
        {
            Sample t60eff = mBaseT60N[i] / damperScale;
            if (t60eff < 0.001) t60eff = 0.001;
            // r = 10^(-3/(T60*fs)) = exp(-ln(1000)/(T60*fs)); only r moves with the
            // damper, so theta's cos/sin stay cached from update().
            const Sample r = std::exp(-kT60Constant / (t60eff * sr));
            mA1[i] = 2.0 * r * mCosTheta[i];
            mA2[i] = r * r;
        }
    }

    Sample StringPartialBank::process(Sample forceIn, int channel)
    {
        // ensureChannels() keeps mY1/mY2 sized to channelCount*kMaxPartials, so this
        // bound is the only one needed.
        if (channel < 0 || channel >= AudioConfig::instance().channelCount())
            return 0.0;

        if (channel == 0 && mDamperValue != mDamperTarget)
        {
            mDamperValue += mDamperStep;
            if ((mDamperStep > 0.0 && mDamperValue >= mDamperTarget) ||
                (mDamperStep < 0.0 && mDamperValue <= mDamperTarget))
                mDamperValue = mDamperTarget;
            recomputeEffectivePartials();
        }

        // README ## 1's recurrence, inlined over flat arrays — see the header note.
        Sample *y1 = mY1.data() + (size_t)channel * kMaxPartials;
        Sample *y2 = mY2.data() + (size_t)channel * kMaxPartials;
        Sample sum = 0.0;
        for (int i = 0; i < mActiveCount; ++i)
        {
            const Sample y = arstroFlush(mA1[i] * y1[i] - mA2[i] * y2[i] + mDrive[i] * forceIn);
            y2[i] = y1[i];
            y1[i] = y;
            sum += y;
        }
        return sum;
    }
}
