#include "Separate.h"
#include "Stft.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        /** The median of v[0 … n) (n odd): nth_element on a copy the caller owns. */
        double median(std::vector<double> &v, size_t n)
        {
            std::nth_element(v.begin(), v.begin() + (long)(n / 2), v.begin() + (long)n);
            return v[n / 2];
        }
    }

    Separation separateHarmonicPercussive(const std::vector<std::vector<float>> &channels, double rate, const SeparateParams &p)
    {
        Separation out;
        if (channels.empty() || channels[0].empty()) return out;
        const long long frames = (long long)channels[0].size();
        const int N = p.fftSize > 0 ? p.fftSize : fftSizeFor(rate, 0.0427);
        // (H1) the masks come from the channels' sum: one magnitude spectrogram for all
        std::vector<float> sum((size_t)frames, 0.0f);
        for (const auto &c : channels)
            for (long long i = 0; i < frames && i < (long long)c.size(); ++i) sum[(size_t)i] += c[(size_t)i];
        const Spectrogram S = stft(sum.data(), frames, N);
        const size_t T = S.bins.size(), K = (size_t)(N / 2 + 1);
        std::vector<std::vector<double>> mag(T, std::vector<double>(K));
        for (size_t t = 0; t < T; ++t)
            for (size_t k = 0; k < K; ++k) mag[t][k] = std::abs(S.bins[t][k]);
        // (H2) H̃ = the median along time (frames t ± L/2), P̃ = along frequency (bins k ± M/2) — a window shrinks at the edges
        const int L = std::max(1, p.timeMedian | 1), M = std::max(1, p.freqMedian | 1);
        std::vector<std::vector<double>> maskH(T, std::vector<double>(K));
        std::vector<double> win((size_t)std::max(L, M));
        for (size_t t = 0; t < T; ++t)
            for (size_t k = 0; k < K; ++k)
            {
                size_t n = 0;
                for (long long u = (long long)t - L / 2; u <= (long long)t + L / 2; ++u)
                    if (u >= 0 && u < (long long)T) win[n++] = mag[(size_t)u][k];
                const double h = median(win, n);
                n = 0;
                for (long long b = (long long)k - M / 2; b <= (long long)k + M / 2; ++b)
                    if (b >= 0 && b < (long long)K) win[n++] = mag[t][(size_t)b];
                const double q = median(win, n);
                // (H3) the soft mask: M_H = H̃^p / (H̃^p + P̃^p), ½ where both are silent; M_P = 1 − M_H
                const double hp = std::pow(h, p.power), qp = std::pow(q, p.power);
                maskH[t][k] = hp + qp > 1e-30 ? hp / (hp + qp) : 0.5;
            }
        // (H4) each channel's spectrum shaped by the masks and resynthesised: harmonic + percussive = the channel
        for (const auto &c : channels)
        {
            std::vector<float> x(c);
            x.resize((size_t)frames, 0.0f);
            Spectrogram X = stft(x.data(), frames, N), Y = X;
            for (size_t t = 0; t < T; ++t)
                for (size_t k = 0; k < K; ++k)
                {
                    X.bins[t][k] *= maskH[t][k];
                    Y.bins[t][k] *= 1.0 - maskH[t][k];
                }
            out.harmonic.push_back(istft(X));
            out.percussive.push_back(istft(Y));
        }
        return out;
    }
}
