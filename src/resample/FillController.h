#pragma once
/*
 *  FillController — the step that holds a buffer's fill at its target (REQ-resample-3): a PI loop on the fill, its
 *  gains from a settling time; its integral the drift between the producer's clock and the consumer's.
 *  src/resample/README.md (F1)–(F5). No allocation; any thread (one at a time).
 */

namespace arstro
{
    struct FillParams
    {
        double target = 1024.0;        // T, frames
        double rate = 48000.0;         // fs, the consumer's rate, Hz
        double naturalSeconds = 60.0;  // Tn: (F4) ωn = 2π/Tn
        double damping = 1.0;          // ζ
        double smoothSeconds = 4.0;    // τ: (F1)
        double maxPpm = 2000.0;        // δmax · 10⁶
        double rampPpm = 200.0;        // (F6) how fast a new target is approached: its frames a second, over fs, · 10⁶
    };

    class FillController
    {
    public:
        explicit FillController(const FillParams &p = FillParams());
        /** (F1)–(F3) a block of `frames` taken while the buffer held `fill`: the step's deviation δ (step = s0·(1 + δ)). */
        double update(double fill, int frames);
        /** The deviation last returned. */
        double deviation() const { return mDelta; }
        /** (F5) the consumer's crystal against the producer's, ppm: + when the consumer runs fast. */
        double driftPpm() const { return -mI * 1e6; }
        double smoothedFill() const { return mFill; }
        /** The target the loop holds now — on its way (F6) to the one last set. */
        double target() const { return mP.target; }
        /** (F6) A new target: approached at `rampPpm`, so the step never jumps. */
        void setTarget(double t) { mGoal = t; }
        /** Start again from `fill`, the drift taken as `driftPpm` (a device's last known: locked from the start). */
        void reset(double fill, double driftPpm = 0.0);

    private:
        FillParams mP;
        double mKp, mKi, mMax;
        double mFill = 0.0, mI = 0.0, mDelta = 0.0, mGoal = 0.0;
    };
}
