#include "PianoBridge.h"
#include <cmath>

namespace arstro
{
    namespace
    {
        // Documented generic placeholder soundboard body-mode set (README ## 8) —
        // representative low-order panel resonances, NOT measured from a real
        // instrument (REQ-piano-14: no measured IR / room acoustics in scope).
        // README ## 8 — plate mode set. Spaced uniformly in FREQUENCY (a plate's modal
        // density is constant in Hz; a harmonic series would be the wrong model), with a
        // deterministic low-discrepancy jitter so the uniform grid cannot ring as a comb.
        constexpr Sample kBodyLowHz = 50.0;
        constexpr Sample kBodyHighHz = 5000.0;
        constexpr Sample kBodyJitter = 0.35;      // fraction of the spacing
        constexpr Sample kBodyT60RefHz = 100.0;   // T60_body reference point
        constexpr Sample kBodyT60Ref = 0.35;      // s at kBodyT60RefHz
        constexpr Sample kBodyT60Slope = 0.45;    // falls with f -> modal overlap grows toward ~1
        constexpr Sample kBodyT60Min = 0.015;
        constexpr Sample kBodyT60Max = 0.5;
        constexpr Sample kRadiationCornerHz = 2500.0; // gentle HF radiation rolloff

        Sample bodyModeFrequency(int m)
        {
            const Sample spacing = (kBodyHighHz - kBodyLowHz) / (Sample)PianoBridge::kBodyModeCount;
            // Golden-ratio low-discrepancy sequence: deterministic, evenly scattered.
            const Sample frac = std::fmod((Sample)m * 0.6180339887498949, 1.0);
            const Sample jitter = (frac - 0.5) * 2.0 * kBodyJitter;
            return kBodyLowHz + ((Sample)m + 0.5 + jitter) * spacing;
        }

        Sample bodyModeT60(Sample f)
        {
            Sample t60 = kBodyT60Ref * std::pow(kBodyT60RefHz / f, kBodyT60Slope);
            if (t60 < kBodyT60Min) t60 = kBodyT60Min;
            if (t60 > kBodyT60Max) t60 = kBodyT60Max;
            return t60;
        }

        // Modes at different frequencies sum incoherently, so a bare sum would scale as
        // sqrt(M) and changing kBodyModeCount would silently rescale the
        // string->bridge->string loop gain — exactly how the coupling gain came to be
        // re-tuned at M1 and again at M3. Normalising per mode makes the bridge's output
        // level, and therefore the loop gain, independent of M.
        Sample bodyModeWeight(Sample f)
        {
            const Sample radiation = 1.0 / std::sqrt(1.0 + (f / kRadiationCornerHz) * (f / kRadiationCornerHz));
            return radiation / std::sqrt((Sample)PianoBridge::kBodyModeCount);
        }
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
            const Sample f = bodyModeFrequency(i);
            mModes[i].setFrequencyHz(f);
            mModes[i].setDecaySeconds(bodyModeT60(f));
            mModeWeight[i] = bodyModeWeight(f);
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
            resp += mModeWeight[i] * mModes[i].out(mBus, 0);
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
