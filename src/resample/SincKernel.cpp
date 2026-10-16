#include "SincKernel.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        double besselI0(double x) // I0 by its series Σ ((x/2)^k / k!)²
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 64; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < 1e-17 * sum) break;
            }
            return sum;
        }
    }

    SincKernel::SincKernel(double cutoff, int zeroCrossings, double beta, int perSample)
        : mR(zeroCrossings / cutoff), mRes(perSample)
    {
        // (R2) h(t) = c · sinc(c·t) · I0(β√(1 − (t/R)²)) / I0(β), |t| < R; (R3) at `perSample` points a frame
        const double c = cutoff;
        const int tableN = (int)std::ceil(mR * mRes) + 2;
        mTable.resize((size_t)tableN);
        const double i0b = besselI0(beta);
        for (int i = 0; i < tableN; ++i)
        {
            const double t = (double)i / mRes, u = t / mR;
            const double s = t == 0.0 ? 1.0 : std::sin(M_PI * c * t) / (M_PI * c * t);
            mTable[(size_t)i] = u >= 1.0 ? 0.0 : c * s * besselI0(beta * std::sqrt(1.0 - u * u)) / i0b;
        }
    }
}
