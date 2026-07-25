#include "HammerExciter.h"
#include "../base/AudioConfig.h"
#include <cmath>

namespace arstro
{
    HammerExciter::HammerExciter() : SignalProcessor(propertyCount)
    {
        setSmoothEnable(false); // strike() is an instantaneous event, not a tone knob
        initProperty(massID, 1.0);
        initProperty(stiffnessID, 1.0e10);
        initProperty(nonlinearExponentID, 2.5);
        initProperty(hysteresisLossID, 0.2);
    }

    void HammerExciter::setMass(Sample m) { setProperty(massID, m); }
    void HammerExciter::setStiffness(Sample k) { setProperty(stiffnessID, k); }
    void HammerExciter::setNonlinearExponent(Sample p) { setProperty(nonlinearExponentID, p); }
    void HammerExciter::setHysteresisLoss(Sample eps) { setProperty(hysteresisLossID, eps); }

    void HammerExciter::strike(Sample velocity01)
    {
        if (velocity01 < 0.0) velocity01 = 0.0;
        if (velocity01 > 1.0) velocity01 = 1.0;
        mPos = 0.0;
        mVel = kMaxImpactSpeed * velocity01;
        mInContact = true;
        mContactSamples = 0;
    }

    Sample HammerExciter::process(Sample /*in*/, int channel)
    {
        // Mono physics (REQ-piano-13): only channel 0 advances the contact ODE.
        if (channel != 0 || !mInContact)
            return 0.0;

        Sample sr = AudioConfig::instance().sampleRate();
        Sample dt = 1.0 / sr;
        Sample mass = getProperty(massID);
        Sample stiffness = getProperty(stiffnessID);
        Sample p = getProperty(nonlinearExponentID);
        Sample eps = getProperty(hysteresisLossID);

        Sample compression = (mPos > 0.0) ? mPos : 0.0;
        Sample force = 0.0;
        if (compression > 0.0)
        {
            Sample keff = (mVel >= 0.0) ? stiffness : stiffness * (1.0 - eps);
            force = keff * std::pow(compression, p);
        }

        // Semi-implicit (symplectic) Euler: stable for a stiff nonlinear spring.
        mVel += -(force / mass) * dt;
        mPos += mVel * dt;
        ++mContactSamples;

        Sample maxContactSamples = kMaxContactMs * 0.001 * sr;
        if ((mPos <= 0.0 && mVel < 0.0) || (Sample)mContactSamples > maxContactSamples)
            mInContact = false;

        return arstroFlush(force);
    }
}
