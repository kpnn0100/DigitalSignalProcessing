#include "FillController.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    FillController::FillController(const FillParams &p) : mP(p)
    {
        // (F4) ωn = 2π/Tn;  Ki = ωn²/fs,  Kp = 2ζωn/fs
        const double fs = std::max(1.0, mP.rate), wn = 2.0 * M_PI / std::max(1e-3, mP.naturalSeconds);
        mKi = wn * wn / fs;
        mKp = 2.0 * mP.damping * wn / fs;
        mMax = std::max(0.0, mP.maxPpm) * 1e-6;
        mGoal = mP.target;
        reset(mP.target);
    }

    void FillController::reset(double fill, double driftPpm)
    {
        mFill = fill;
        mI = std::min(mMax, std::max(-mMax, -driftPpm * 1e-6)); // (F5) the other way round
        mDelta = mI;
    }

    double FillController::update(double fill, int frames)
    {
        const double fs = std::max(1.0, mP.rate), dt = std::max(0, frames) / fs;
        // (F6) the target on its way to the goal, rampPpm of the frames that pass
        const double ramp = std::max(0.0, mP.rampPpm) * 1e-6 * std::max(0, frames);
        mP.target += std::min(ramp, std::max(-ramp, mGoal - mP.target));
        // (F1) F̄ ← F̄ + α(F − F̄), α = 1 − exp(−B/(τ·fs))
        const double alpha = 1.0 - std::exp(-dt / std::max(1e-6, mP.smoothSeconds));
        mFill += alpha * (fill - mFill);
        // (F2) e = F̄ − T;  (F3) I ← clamp(I + Ki·e·B/fs);  δ = clamp(Kp·e + I)
        const double e = mFill - mP.target;
        mI = std::min(mMax, std::max(-mMax, mI + mKi * e * dt));
        mDelta = std::min(mMax, std::max(-mMax, mKp * e + mI));
        return mDelta;
    }
}
