#include "Stft.h"
#include "../spectral/FFT.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        std::vector<double> hann(int n)
        {
            // (S1) periodic Hann: w[j] = ½ − ½·cos(2πj/N) — its squares, every N/4, sum to 3/2 (S3)
            std::vector<double> w((size_t)n);
            for (int j = 0; j < n; ++j) w[(size_t)j] = 0.5 - 0.5 * std::cos(2.0 * M_PI * j / n);
            return w;
        }
        long long frameCount(long long frames, int hop) { return frames <= 0 ? 0 : frames / hop + 1; } // centres 0, hop, … ≤ frames
    }

    int fftSizeFor(double rate, double seconds)
    {
        const double want = std::max(256.0, rate * seconds);
        int n = 256;
        while (n * 2 <= want * std::sqrt(2.0)) n *= 2; // the nearest power of two, by its log
        return n;
    }

    Spectrogram stft(const float *x, long long frames, int fftSize)
    {
        Spectrogram s;
        s.fftSize = fftSize;
        s.hop = fftSize / 4;
        s.frames = frames;
        const long long T = frameCount(frames, s.hop);
        const std::vector<double> w = hann(fftSize);
        FFT fft(fftSize);
        std::vector<std::complex<double>> buf((size_t)fftSize);
        s.bins.assign((size_t)T, std::vector<std::complex<double>>((size_t)(fftSize / 2 + 1)));
        for (long long t = 0; t < T; ++t)
        {
            // (S2) the frame centred on sample t·hop: X_t[k] = Σ_j w[j]·x[t·hop − N/2 + j]·e^(−2πi jk/N) (outside the sound: 0)
            const long long start = t * s.hop - fftSize / 2;
            for (int j = 0; j < fftSize; ++j)
            {
                const long long i = start + j;
                buf[(size_t)j] = i >= 0 && i < frames ? w[(size_t)j] * (double)x[i] : 0.0;
            }
            fft.forward(buf.data());
            std::copy(buf.begin(), buf.begin() + fftSize / 2 + 1, s.bins[(size_t)t].begin());
        }
        return s;
    }

    std::vector<float> istft(const Spectrogram &s)
    {
        const int N = s.fftSize;
        std::vector<double> y((size_t)std::max<long long>(0, s.frames), 0.0), norm(y.size(), 0.0);
        if (N <= 0 || s.frames <= 0) return std::vector<float>(y.size(), 0.0f);
        const std::vector<double> w = hann(N);
        FFT fft(N);
        std::vector<std::complex<double>> buf((size_t)N);
        for (size_t t = 0; t < s.bins.size(); ++t)
        {
            // (S3) the real frame back from its half spectrum (Hermitian), windowed again and added where it was
            const auto &X = s.bins[t];
            for (int k = 0; k <= N / 2; ++k) buf[(size_t)k] = X[(size_t)k];
            for (int k = N / 2 + 1; k < N; ++k) buf[(size_t)k] = std::conj(X[(size_t)(N - k)]);
            fft.inverse(buf.data());
            const long long start = (long long)t * s.hop - N / 2;
            for (int j = 0; j < N; ++j)
            {
                const long long i = start + j;
                if (i < 0 || i >= s.frames) continue;
                y[(size_t)i] += w[(size_t)j] * buf[(size_t)j].real();
                norm[(size_t)i] += w[(size_t)j] * w[(size_t)j];
            }
        }
        // (S4) divided by the windows' squares at each sample — 3/2 inside, what there is at the ends: exact everywhere
        std::vector<float> out(y.size());
        for (size_t i = 0; i < y.size(); ++i) out[i] = (float)(norm[i] > 1e-12 ? y[i] / norm[i] : 0.0);
        return out;
    }
}
