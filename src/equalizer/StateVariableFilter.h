/*
 *  Arstro DSP Library — StateVariableFilter: a resonant 2-pole filter that can be swept every
 *  sample (REQ-svf-1).
 *
 *  The topology-preserving-transform (trapezoidal) state-variable filter — Zavalishin's
 *  derivation, in Simper's form. It is the synth's filter and the Filter effect, and the drum
 *  voices' band-pass/high-pass, because it has three properties a Biquad lacks for that job:
 *  low-, band- and high-pass from ONE state; a cutoff that can change on every sample without
 *  the coefficient-update blow-ups of direct forms; and a stable, self-oscillation-free resonance
 *  all the way to the top of its range. The math is in equalizer/README.md ## Math (S1)…(S6).
 */
#pragma once
#include "../base/SignalProcessor.h"
#include <vector>

namespace arstro
{
    class StateVariableFilter : public SignalProcessor
    {
    public:
        enum Mode { LowPass = 0, BandPass, HighPass, Notch };
        enum PropertyIndex
        {
            cutoffID,    // Hz
            resonanceID, // 0..1 → Q 0.707..32
            propertyCount
        };

        StateVariableFilter();
        StateVariableFilter(Mode mode, Sample cutoffHz, Sample resonance);

        void setMode(Mode mode) { mMode = mode; }
        Mode mode() const { return mMode; }
        void setCutoff(Sample hz);
        void setResonance(Sample r);
        void reset();

        /** Q for a resonance in [0,1] — (S5). */
        static Sample qFor(Sample resonance);

        /** The modulation path: one sample of `channel` with an explicit cutoff, ignoring the
         *  stored one. Non-virtual, no smoothing — the caller's modulator IS the smoothing. */
        Sample tick(Sample in, int channel, Sample cutoffHz);

        void update() override;
        void onSampleRateChanged() override;
        void onChannelCountChanged() override;

    protected:
        Sample process(Sample in, int channel) override;

    private:
        void ensureChannels();
        Sample run(Sample in, int channel, Sample g);
        Mode mMode = LowPass;
        Sample mG = 0.0; // tan(π·fc/fs) for the stored cutoff
        Sample mK = 1.4142; // 1/Q
        std::vector<Sample> mIc1, mIc2; // per-channel integrator states
    };
}
