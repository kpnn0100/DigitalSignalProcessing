#include "PianoBridge.h"

namespace arstro
{
    namespace
    {
        // Documented generic placeholder soundboard body-mode set (README ## 8) —
        // representative low-order panel resonances, NOT measured from a real
        // instrument (REQ-piano-14: no measured IR / room acoustics in scope).
        constexpr Sample kModeFreqs[PianoBridge::kBodyModeCount] = {80, 130, 190, 250, 340, 420, 550, 700};
        constexpr Sample kModeT60[PianoBridge::kBodyModeCount] = {0.45, 0.42, 0.40, 0.38, 0.35, 0.33, 0.30, 0.30};
    }

    PianoBridge::PianoBridge() : SignalProcessor(propertyCount)
    {
        setSmoothEnable(false);
        // Stability-bounded (M1). Impulse-normalised string partials (README ## 1)
        // have a very high SUSTAINED resonance gain — G/(1-r^2) ~ 800 for a 6.9 s
        // C4 partial, more for the bass — so the string->bridge->string loop needs a
        // correspondingly small coupling gain. Measured worst case (8 sustained
        // bass voices, longest T60 hence highest gain): diverges at 5e-3, stable at
        // 1e-3; 5e-4 keeps a 10x margin while leaving sympathetic selectivity at
        // ~35x (the ratio is coupling-independent, so lowering this costs nothing
        // in REQ-piano-6 terms). Proper treatment — where the bridge admittance
        // sets string damping AND radiation, making the loop self-limiting — is M5.
        initProperty(couplingGainID, 5e-4);
        initProperty(radiationGainID, 0.5);
        for (int i = 0; i < kBodyModeCount; ++i)
        {
            mModes[i].setFrequencyHz(kModeFreqs[i]);
            mModes[i].setDecaySeconds(kModeT60[i]);
        }
        update();
    }

    void PianoBridge::setCouplingGain(Sample g) { setProperty(couplingGainID, g); }
    void PianoBridge::setRadiationGain(Sample g) { setProperty(radiationGainID, g); }

    void PianoBridge::update()
    {
        mCouplingGain = getProperty(couplingGainID);
        mRadiationGain = getProperty(radiationGainID);
    }

    void PianoBridge::accumulate(Sample contribution) { mBus += contribution; }

    void PianoBridge::tick()
    {
        Sample resp = 0.0;
        for (int i = 0; i < kBodyModeCount; ++i)
            resp += mModes[i].out(mBus, 0);
        mLastResponse = arstroFlush(resp);
        mBus = 0.0;
    }

    Sample PianoBridge::process(Sample in, int /*channel*/)
    {
        accumulate(in);
        tick();
        return radiatedOutput();
    }
}
