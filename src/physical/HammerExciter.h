/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  HammerExciter: single-degree-of-freedom nonlinear hysteretic hammer-felt
 *  contact model. Runs mono (REQ-piano-13) — driven by PianoVoice on channel 0
 *  only. See src/physical/README.md ## 6.
 */
#pragma once
#include "../base/SignalProcessor.h"

namespace arstro
{
    class HammerExciter : public SignalProcessor
    {
    public:
        enum PropertyIndex
        {
            massID,             // m_h, normalized hammer inertia
            stiffnessID,        // K, felt stiffness
            nonlinearExponentID,// p
            hysteresisLossID,   // epsilon, 0..1 (unloading stiffness fraction lost)
            propertyCount
        };
        HammerExciter();

        void setMass(Sample m);
        void setStiffness(Sample k);
        void setNonlinearExponent(Sample p);
        void setHysteresisLoss(Sample eps);

        // Begin contact: velocity in 0..1 (matches SignalGenerator::noteOn convention).
        void strike(Sample velocity01);
        bool isInContact() const { return mInContact; }

    protected:
        // `in` unused (this is an excitation source, like SignalGenerator::process
        // ignoring its input); advances the contact ODE by one sample on channel 0.
        Sample process(Sample in, int channel) override;

    private:
        Sample mPos = 0.0; // hammer felt compression (>=0 while in contact)
        Sample mVel = 0.0;
        bool mInContact = false;
        long mContactSamples = 0;

        static constexpr Sample kMaxContactMs = 15.0;
        static constexpr Sample kMaxImpactSpeed = 4.0; // v0 at velocity01 = 1.0
    };
}
