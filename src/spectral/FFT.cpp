#include "FFT.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace arstro
{
    FFT::FFT(int n) : mN(n)
    {
        if (n < 2 || (n & (n - 1)) != 0) throw std::invalid_argument("FFT: the size must be a power of two ≥ 2");
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        mReverse.resize((size_t)n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            mReverse[(size_t)i] = r;
        }
        mTwiddle.resize((size_t)n / 2);
        for (int j = 0; j < n / 2; ++j)
        {
            const double a = -2.0 * M_PI * j / n;
            mTwiddle[(size_t)j] = {std::cos(a), std::sin(a)};
        }
    }

    void FFT::forward(std::complex<double> *data) const { transform(data, false); }

    void FFT::inverse(std::complex<double> *data) const
    {
        transform(data, true);
        const double s = 1.0 / mN;
        for (int i = 0; i < mN; ++i) data[i] *= s;
    }

    void FFT::transform(std::complex<double> *data, bool inverse) const
    {
        // (F3) inputs in bit-reversed order, then log2 N stages of butterflies
        for (int i = 0; i < mN; ++i)
            if (i < mReverse[(size_t)i]) std::swap(data[i], data[mReverse[(size_t)i]]);
        for (int len = 2; len <= mN; len <<= 1)
        {
            const int half = len / 2, step = mN / len;
            for (int s = 0; s < mN; s += len)
                for (int j = 0; j < half; ++j)
                {
                    const std::complex<double> w = inverse ? std::conj(mTwiddle[(size_t)(j * step)]) : mTwiddle[(size_t)(j * step)];
                    const std::complex<double> t = w * data[s + j + half];
                    data[s + j + half] = data[s + j] - t;
                    data[s + j] += t;
                }
        }
    }
}
