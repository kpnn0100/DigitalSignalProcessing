#pragma once
/*
 *  Yin — a period search by YIN (REQ-tuner-1's (Y1)…(Y4), lifted out of the Tuner so the tuner and the transcriber
 *  (REQ-analysis-3) share one copy). The math is in src/meter/README.md (Y1)…(Y4).
 *
 *  Built once for a window (it allocates then); `period` allocates nothing.
 */
#include "../spectral/FFT.h"
#include <complex>
#include <vector>

namespace arstro
{
    class Yin
    {
    public:
        /** `window` W, a power of two ≥ 4 — the lags searched are up to W/2. */
        explicit Yin(int window);
        int window() const { return mW; }
        /** The period of x[0 … W) in samples, fractional — (Y1) the difference function by the FFT, (Y2) normalised by its
         *  running mean, (Y3) the first dip under `threshold` among the lags of `maxHz` … `minHz` followed to its bottom,
         *  (Y4) a parabola through it — or 0 when nothing dips under. `dip`: d′ at the lag chosen (0 … 1: how periodic). */
        double period(const double *x, double rate, double minHz, double maxHz, double threshold, double *dip = nullptr);

    private:
        int mW;
        FFT mFft;
        std::vector<std::complex<double>> mA, mB; // (Y1) work
        std::vector<double> mRaw, mD, mPrefix;     // d(τ); d′(τ); running energies
    };
}
