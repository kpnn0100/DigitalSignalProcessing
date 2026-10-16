#pragma once
/*
 *  FFT — the discrete Fourier transform of 2^m points (REQ-fft-1): iterative radix-2 Cooley–Tukey, decimation in
 *  time, twiddles computed once each from cos/sin (never by repeated multiplication, which drifts). The math is in
 *  src/spectral/README.md (F1)…(F3).
 *
 *  Built once for a size (it allocates then); forward / inverse transform in place and allocate nothing.
 */
#include <complex>
#include <vector>

namespace arstro
{
    class FFT
    {
    public:
        /** `n` a power of two ≥ 2 — else std::invalid_argument. */
        explicit FFT(int n);
        int size() const { return mN; }
        /** (F1) X[k] = Σ x[n] e^(−2πi kn/N), in place. */
        void forward(std::complex<double> *data) const;
        /** (F2) x[n] = (1/N) Σ X[k] e^(+2πi kn/N), in place. */
        void inverse(std::complex<double> *data) const;

    private:
        void transform(std::complex<double> *data, bool inverse) const;
        int mN = 0;
        std::vector<int> mReverse;                 // (F3) the bit-reversed index of each
        std::vector<std::complex<double>> mTwiddle; // e^(−2πi j/N), j < N/2
    };
}
