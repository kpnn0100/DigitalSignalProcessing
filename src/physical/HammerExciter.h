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
            hysteresisLossID,   // epsilon_branch, 0..1: base load/unload asymmetry (README ## 6)
            relaxationDepthID,  // epsilon_relax, 0..1: Stulov fading-memory term depth (M9.3)
            relaxationTimeID,   // tau, seconds: the felt's fading-memory time constant
            propertyCount
        };
        HammerExciter();

        void setMass(Sample m);
        void setStiffness(Sample k);
        void setNonlinearExponent(Sample p);
        void setHysteresisLoss(Sample eps);          // base load/unload asymmetry
        void setRelaxationDepth(Sample eps);         // README ## 6, Stulov rate-dependent term (M9.3)
        void setRelaxationTime(Sample tauSeconds);   // README ## 6, the felt's memory time constant

        // Begin contact: velocity in 0..1 (matches SignalGenerator::noteOn convention).
        void strike(Sample velocity01);
        bool isInContact() const { return mInContact; }

    protected:
        // `in` is the STRING DISPLACEMENT at the strike point (README ## 6): the
        // felt compresses against a yielding string, not a rigid wall, so contact
        // force depends on the relative displacement x_hammer - y_string. Advances
        // the contact ODE by one sample; mono, channel 0 only (REQ-piano-13).
        Sample process(Sample in, int channel) override;

    private:
        Sample mPos = 0.0; // hammer felt compression (>=0 while in contact)
        Sample mVel = 0.0;
        Sample mHyst = 0.0; // README ## 6: h, the relaxed memory of u^p (Stulov)
        bool mInContact = false;
        long mContactSamples = 0;

        static constexpr Sample kMaxContactMs = 15.0;
        static constexpr Sample kMaxImpactSpeed = 4.0; // v0 at velocity01 = 1.0
        static constexpr Sample kMinRelaxationTime = 1e-6; // guard: tau -> alpha = dt/tau <= 1
    };
}
