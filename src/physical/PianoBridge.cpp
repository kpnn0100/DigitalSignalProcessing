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
        initProperty(couplingGainID, 0.15);
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
