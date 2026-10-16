#pragma once
/*
 *  SincKernel — the band-limited interpolation kernel (REQ-resample-1): a Kaiser-windowed sinc, tabulated and read
 *  linearly. src/resample/README.md (R2)–(R3). Built once (it allocates); read from any thread.
 */
#include <cstddef>
#include <vector>

namespace arstro
{
    class SincKernel
    {
    public:
        /** `cutoff` c of the input's Nyquist (≤ 1), `zeroCrossings` Z each side, Kaiser `beta`, `perSample` table points. */
        explicit SincKernel(double cutoff = 1.0, int zeroCrossings = 16, double beta = 8.6, int perSample = 512);
        /** (R2) R = Z/c: input frames each side the kernel reaches. */
        double halfWidth() const { return mR; }
        /** (R3) h(t), t in input frames (0 beyond R). */
        double operator()(double t) const
        {
            const double a = (t < 0 ? -t : t) * mRes;
            const int i = (int)a;
            if (i + 1 >= (int)mTable.size()) return 0.0;
            const double f = a - i;
            return mTable[(size_t)i] + f * (mTable[(size_t)i + 1] - mTable[(size_t)i]);
        }

    private:
        std::vector<double> mTable;
        double mR;
        int mRes;
    };
}
