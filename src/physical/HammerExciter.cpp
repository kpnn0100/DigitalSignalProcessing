#include "HammerExciter.h"
#include "../base/AudioConfig.h"
#include <cmath>

namespace arstro
{
    HammerExciter::HammerExciter() : SignalProcessor(propertyCount)
    {
        setSmoothEnable(false); // strike() is an instantaneous event, not a tone knob
        initProperty(massID, 1.0);
        initProperty(stiffnessID, 3.0e11);
        initProperty(nonlinearExponentID, 2.5);
        // README ## 6: hysteresisLossID is the base load/unload asymmetry (the M0 model,
        // which shapes the bright attack M6 depends on); relaxationDepthID + tau add the
        // Stulov fading-memory term (M9.3) that makes the loss RATE-dependent. Defaults
        // calibrated against the coupled loop to hold the M6 energy guard and M3 contact
        // durations (see the ledger's decisions log). PianoVoice tapers relaxationDepth
        // to ~0 in the treble, where the pure-memory form over-rounds a capped contact.
        initProperty(hysteresisLossID, 0.2);
        initProperty(relaxationDepthID, 0.25); // == PianoVoice's bass value (## 6 taper ref)
        initProperty(relaxationTimeID, 0.0005); // 0.5 ms — comparable to a contact
    }

    void HammerExciter::setMass(Sample m) { setProperty(massID, m); }
    void HammerExciter::setStiffness(Sample k) { setProperty(stiffnessID, k); }
    void HammerExciter::setNonlinearExponent(Sample p) { setProperty(nonlinearExponentID, p); }
    void HammerExciter::setHysteresisLoss(Sample eps) { setProperty(hysteresisLossID, eps); }
    void HammerExciter::setRelaxationDepth(Sample eps) { setProperty(relaxationDepthID, eps); }
    void HammerExciter::setRelaxationTime(Sample tauSeconds)
    {
        if (tauSeconds < kMinRelaxationTime) tauSeconds = kMinRelaxationTime;
        setProperty(relaxationTimeID, tauSeconds);
    }

    void HammerExciter::strike(Sample velocity01)
    {
        if (velocity01 < 0.0) velocity01 = 0.0;
        if (velocity01 > 1.0) velocity01 = 1.0;
        mPos = 0.0;
        mVel = kMaxImpactSpeed * velocity01;
        mHyst = 0.0; // README ## 6: felt memory starts empty at each strike
        mInContact = true;
        mContactSamples = 0;
    }

    Sample HammerExciter::process(Sample stringDisplacement, int channel)
    {
        // Mono physics (REQ-piano-13): only channel 0 advances the contact ODE.
        if (channel != 0 || !mInContact)
            return 0.0;

        Sample sr = AudioConfig::instance().sampleRate();
        Sample dt = 1.0 / sr;
        Sample mass = getProperty(massID);
        Sample stiffness = getProperty(stiffnessID);
        Sample p = getProperty(nonlinearExponentID);
        Sample epsBranch = getProperty(hysteresisLossID);
        Sample epsRelax = getProperty(relaxationDepthID);
        Sample tau = getProperty(relaxationTimeID);
        if (tau < kMinRelaxationTime) tau = kMinRelaxationTime;

        // README ## 6: compression is RELATIVE — the string yields under the felt.
        // Before M3 this was just mPos, i.e. an infinitely rigid string.
        const Sample c = mPos - stringDisplacement;
        Sample compression = (c > 0.0) ? c : 0.0;
        const Sample up = (compression > 0.0) ? std::pow(compression, p) : 0.0;

        // README ## 6: BASE load/unload asymmetry — the M0 hysteresis, stiffer on the
        // way in than out. It shapes the sharp, bright attack the treble (already
        // stability-capped, ## 11.3) needs, so it is kept everywhere.
        const Sample keff = (mVel >= 0.0) ? stiffness : stiffness * (1.0 - epsBranch);

        // README ## 6 (Stulov, M9.3): a fading MEMORY of u^p (one-pole, dh/dt=(u^p−h)/tau)
        // subtracted from the force makes the dissipation RATE-dependent — the memory fills
        // on the timescale tau, so how much it takes off depends on how long contact lasts,
        // which the fixed branch alone cannot do. h advances every in-contact sample (incl.
        // compression == 0, so it relaxes back toward 0 near release). PianoVoice tapers
        // epsRelax to ~0 in the treble, where this term's low-pass would over-round the
        // capped contact and dull an attack that must stay bright (see ## 6 note).
        Sample alpha = dt / tau;
        if (alpha > 1.0) alpha = 1.0; // stability: tau below one sample -> h just tracks u^p
        mHyst += alpha * (up - mHyst);

        // Felt cannot pull the string, so the force is clamped non-negative; near release
        // (u^p -> 0 while h > 0) this is where the contact lets go.
        Sample force = keff * up - stiffness * epsRelax * mHyst;
        if (force < 0.0) force = 0.0;

        // Semi-implicit (symplectic) Euler: stable for a stiff nonlinear spring.
        mVel += -(force / mass) * dt;
        mPos += mVel * dt;
        ++mContactSamples;

        Sample maxContactSamples = kMaxContactMs * 0.001 * sr;
        if ((c <= 0.0 && mVel < 0.0) || (Sample)mContactSamples > maxContactSamples)
            mInContact = false; // hammer rebounded off the (moving) string

        return arstroFlush(force);
    }
}
