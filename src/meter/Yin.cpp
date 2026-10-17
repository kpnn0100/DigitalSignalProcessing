#include "Yin.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    Yin::Yin(int window) : mW(window), mFft(2 * window)
    {
        mA.assign((size_t)(2 * window), 0.0);
        mB.assign((size_t)(2 * window), 0.0);
        mD.assign((size_t)(window / 2 + 1), 0.0);
        mRaw.assign((size_t)(window / 2 + 1), 0.0);
        mPrefix.assign((size_t)(window + 1), 0.0);
    }

    double Yin::period(const double *x, double rate, double minHz, double maxHz, double threshold, double *dip)
    {
        const int W = mW, H = mW / 2, N = 2 * mW;
        // (Y1) d(τ) = Σ_{j<H} (x_j − x_{j+τ})² = e_0 + e(τ) − 2·r(τ):  r by the FFT (a against b, no wrap), e by prefixes
        for (int j = 0; j < N; ++j)
        {
            mA[(size_t)j] = j < H ? x[j] : 0.0;
            mB[(size_t)j] = j < W ? x[j] : 0.0;
        }
        mFft.forward(mA.data());
        mFft.forward(mB.data());
        for (int k = 0; k < N; ++k) mA[(size_t)k] = std::conj(mA[(size_t)k]) * mB[(size_t)k];
        mFft.inverse(mA.data());
        mPrefix[0] = 0.0;
        for (int j = 0; j < W; ++j) mPrefix[(size_t)j + 1] = mPrefix[(size_t)j] + x[j] * x[j];
        const double e0 = mPrefix[(size_t)H];
        for (int t = 0; t <= H; ++t) mRaw[(size_t)t] = mD[(size_t)t] = std::max(0.0, e0 + (mPrefix[(size_t)(t + H)] - mPrefix[(size_t)t]) - 2.0 * mA[(size_t)t].real());
        // (Y2) normalised by the running mean: d'(0) = 1, d'(τ) = d(τ)·τ / Σ_{1..τ} d
        double run = 0.0;
        mD[0] = 1.0;
        for (int t = 1; t <= H; ++t)
        {
            run += mD[(size_t)t];
            mD[(size_t)t] = run > 0.0 ? mD[(size_t)t] * t / run : 1.0;
        }
        // (Y3) the first dip under the threshold, followed to its bottom
        const int lo = std::max(2, (int)std::floor(rate / maxHz)), hi = std::min(H - 1, (int)std::ceil(rate / minHz));
        int tau = -1;
        for (int t = lo; t <= hi; ++t)
            if (mD[(size_t)t] < threshold)
            {
                while (t + 1 <= hi && mD[(size_t)(t + 1)] < mD[(size_t)t]) ++t;
                tau = t;
                break;
            }
        if (tau < 0) return 0.0;
        if (dip) *dip = mD[(size_t)tau];
        // (Y4) a parabola through the dip and its neighbours — of d itself: d′'s running mean leans the fit
        const double a = mRaw[(size_t)(tau - 1)], b = mRaw[(size_t)tau], c = mRaw[(size_t)(tau + 1)], den = a - 2.0 * b + c;
        return tau + (den != 0.0 ? 0.5 * (a - c) / den : 0.0);
    }
}
