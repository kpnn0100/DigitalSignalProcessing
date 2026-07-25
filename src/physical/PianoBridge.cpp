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
        // Stability-bounded, and RESCALED AT M3. The string drive path now carries the
        // physical 1/(kModalMass*f_s) (README ## 6), i.e. it got ~48000x smaller, so the
        // old 5e-4 left sympathetic resonance ~125 dB down — inaudible, and below 16-bit
        // resolution, which made the integration check pass on an all-zero signal.
        // Re-measured against the worst case (8 sustained bass voices, highest resonance
        // gain): diverges at 50, grows at 20, stable at 10. 5.0 keeps a 4x margin and puts
        // the sympathetic response ~45 dB below the struck note — audible, as on a real
        // piano. The selectivity ratio (~85x) is coupling-independent, so this trades no
        // REQ-piano-6 fidelity. M5's admittance bridge is the principled fix.
        initProperty(couplingGainID, 5.0);
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
