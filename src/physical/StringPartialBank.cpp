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
        initProperty(modalMassID, kModalMassAtRef);
        ensureChannels();
        update();
    }

    void StringPartialBank::ensureChannels()
    {
        const size_t n = (size_t)AudioConfig::instance().channelCount() * (size_t)kMaxEntries;
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

    void StringPartialBank::setModalMass(Sample m)
    {
        // A zero/negative modal mass is not a physical string; it would also divide
        // the drive gain by zero. Clamp rather than assert — same policy as the
        // other setters here.
        if (m < 1e-6) m = 1e-6;
        setProperty(modalMassID, m);
    }

    void StringPartialBank::setTensionModulation(Sample kappaT)
    {
        // Stored raw (may be negative to bend flat, e.g. for tests); process()
        // clamps the resulting delta symmetrically. 0 skips the whole stage.
        mTensionCoupling = kappaT;
    }

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
        // README ## 12.5: a fresh strike starts at nominal pitch. Zero the glide
        // state BEFORE recomputeEffectivePartials() below so it rebuilds a1 at
        // delta = 0; otherwise a voice-stolen bank would carry the previous note's
        // bend into the new one's coefficients.
        mEnergyLP = 0.0;
        mPitchDelta = 0.0;
        mBendCounter = 0;
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
        // README ## 11.1: graded by register, no longer a constant. Guard mirrors
        // setModalMass() so a direct setProperty() cannot divide by zero below.
        Sample modalMass = getProperty(modalMassID);
        if (modalMass < 1e-6) modalMass = 1e-6;

        const Sample sr = AudioConfig::instance().sampleRate();
        const Sample nyquistLimit = kNyquistFraction * sr;

        // README ## 12.5: one-pole coefficient for the tension-rise envelope
        // E += mEnergyCoeff*(slope^2 - E). Derived from tau here so a sample-rate
        // change (which re-runs update() via the property path) tracks it.
        mEnergyCoeff = 1.0 - std::exp(-1.0 / (kEnergyTauSeconds * sr));

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

        // README ## 5b: the vertical/horizontal T60 split is geometric about T60_n.
        const Sample splitRoot = std::sqrt(kPolarizationDecayRatio);
        const int polarized = (mActiveCount < kPolarizedPartials) ? mActiveCount : kPolarizedPartials;
        mTotalCount = mActiveCount + polarized;

        for (int i = 0; i < mActiveCount; ++i)
        {
            // Frequency guard mirrors StringResonator's: the first partial can be
            // above the limit when f0 itself is (see loop above).
            Sample fn = mFreqN[i];
            if (fn < 1.0) fn = 1.0;
            if (fn > 0.49 * sr) fn = 0.49 * sr;
            mFreqN[i] = fn;

            const Sample wn = 2.0 * M_PI * fn;
            const Sample internal = c3 * wn * wn; // viscoelastic: SAME in both planes
            Sample alphaN = c1 + internal;
            if (alphaN < kMinAlpha)
                alphaN = kMinAlpha;
            mBaseT60N[i] = kT60Constant / alphaN; // natural, pre-split (introspection)

            // README ## 5b: only the BRIDGE-loss term splits between the two planes.
            // The vertical plane loses more to the bridge, the horizontal less; the
            // internal c3*w^2 loss is a property of the wire and is identical in both.
            // This is why double decay is a low-partial phenomenon *by physics* — high
            // partials are internal-loss dominated, so their two planes barely differ.
            Sample alphaVert = c1 * splitRoot + internal;
            Sample alphaHoriz = c1 / splitRoot + internal;
            if (alphaVert < kMinAlpha) alphaVert = kMinAlpha;
            if (alphaHoriz < kMinAlpha) alphaHoriz = kMinAlpha;

            // Impulse normalisation (README ## 1): G = sin(theta), independent of
            // decay, folded together with the mode-shape gain so process() does one
            // multiply. Deliberately NOT scaled by 1/N_active — modal superposition
            // has no such factor, and now that N varies with pitch (README ## 2) it
            // would make a bass note's fundamental quieter purely because the note
            // carries more partials. See README ## 4.
            const Sample gn = std::fabs(std::sin((Sample)(i + 1) * M_PI * beta));
            // Only partials that actually get a horizontal twin give away eps of their
            // drive; the rest keep all of it (they have nowhere to give it to).
            const bool hasTwin = (i < polarized);
            const Sample vertShare = hasTwin ? (1.0 - kPolarizationSplit) : 1.0;

            // --- vertical: the hammer's plane, strongly bridge-coupled, fast decay ---
            const Sample theta = wn / sr;
            const Sample sinTheta = std::sin(theta);
            mCosTheta[i] = std::cos(theta);
            // README ## 12.5: first-order pitch-bend sensitivity b_n = -theta*sin(theta).
            mCosBend[i] = -theta * sinTheta;
            // Velocity gain (README ## 6): sin(theta)*g_n/(m*fs). The 1/omega_n that
            // would appear for displacement cancels here — velocity is what radiates.
            mDrive[i] = sinTheta * gn * vertShare / (modalMass * sr);
            mDispWeight[i] = gn / wn; // ...displacement recovered by weighting
            mEntryT60[i] = kT60Constant / alphaVert;

            // --- horizontal twin: weakly coupled, slow decay, does NOT push the felt ---
            if (hasTwin)
            {
                const int h = mActiveCount + i;
                Sample fh = fn * (1.0 + kPolarizationDetune);
                if (fh > 0.49 * sr) fh = 0.49 * sr;
                const Sample thetaH = 2.0 * M_PI * fh / sr;
                const Sample sinThetaH = std::sin(thetaH);
                mCosTheta[h] = std::cos(thetaH);
                mCosBend[h] = -thetaH * sinThetaH; // README ## 12.5, horizontal twin
                mDrive[h] = sinThetaH * gn * kPolarizationSplit / (modalMass * sr);
                // Horizontal motion is perpendicular to the hammer's compression axis,
                // so it does not change c = x_h - y_string (README ## 5b, ## 6).
                mDispWeight[h] = 0.0;
                Sample t60h = kT60Constant / alphaHoriz;
                if (t60h > kPolarizationT60Cap) t60h = kPolarizationT60Cap;
                mEntryT60[h] = t60h;
            }
        }
        recomputeEffectivePartials();
    }

    void StringPartialBank::recomputeEffectivePartials()
    {
        const Sample sr = AudioConfig::instance().sampleRate();
        const Sample damperScale = 1.0 + mDamperValue * kDamperLossGain;
        for (int i = 0; i < mTotalCount; ++i)
        {
            Sample t60eff = mEntryT60[i] / damperScale;
            if (t60eff < 0.001) t60eff = 0.001;
            // r = 10^(-3/(T60*fs)) = exp(-ln(1000)/(T60*fs)); only r moves with the
            // damper, so theta's cos/sin stay cached from update().
            const Sample r = std::exp(-kT60Constant / (t60eff * sr));
            // README ## 12.5: cos(theta*(1+delta)) ~ cos(theta) + b_n*delta, so the
            // tension glide re-tunes every partial with no per-partial trig. delta is
            // 0 unless the tension stage is active, leaving the pre-M8 path unchanged.
            const Sample bentCos = mCosTheta[i] + mCosBend[i] * mPitchDelta;
            mA1[i] = 2.0 * r * bentCos;
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
        Sample *y1 = mY1.data() + (size_t)channel * kMaxEntries;
        Sample *y2 = mY2.data() + (size_t)channel * kMaxEntries;
        Sample sum = 0.0;
        Sample disp = 0.0;
        for (int i = 0; i < mTotalCount; ++i)
        {
            const Sample y = arstroFlush(mA1[i] * y1[i] - mA2[i] * y2[i] + mDrive[i] * forceIn);
            y2[i] = y1[i];
            y1[i] = y;
            sum += y;
            disp += mDispWeight[i] * y; // README ## 6: q_n = y_n/omega_n, projected by g_n
        }
        mLastDisplacement = disp;

        // README ## 12.5: tension modulation. `sum` is the string's slope (§12.2), so
        // its low-passed square is the tension-rise envelope E(t). Advance it only on
        // the mono core (channel 0, like the damper ramp) and only when the stage is
        // enabled, so the default path costs nothing. The coefficient re-tune is
        // decimated — E(t) is slow, and this pass must stay off the per-sample budget.
        if (channel == 0 && mTensionCoupling != 0.0)
        {
            mEnergyLP += mEnergyCoeff * (sum * sum - mEnergyLP);
            if (++mBendCounter >= kBendRetuneInterval)
            {
                mBendCounter = 0;
                Sample delta = mTensionCoupling * mEnergyLP;
                if (delta > kMaxPitchDelta) delta = kMaxPitchDelta;
                else if (delta < -kMaxPitchDelta) delta = -kMaxPitchDelta;
                mPitchDelta = delta;
                recomputeEffectivePartials(); // rebuild a1 at the new bend
            }
        }
        return sum;
    }
}
