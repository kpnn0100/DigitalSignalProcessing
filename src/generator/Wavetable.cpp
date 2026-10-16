#include "Wavetable.h"
#include "../spectral/FFT.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace arstro
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr int kN = Wavetable::kFrameSize;

        int pointsOf(int lvl) { return std::max(256, 16 * (Wavetable::kMaxHarmonics >> lvl)); } // (W1) N_l

        std::vector<float> cycle(double (*f)(double x, double u), double u)
        {
            std::vector<float> c((size_t)kN);
            for (int n = 0; n < kN; ++n) c[(size_t)n] = (float)f((double)n / kN, u);
            return c;
        }
        std::vector<std::vector<float>> frames32(double (*f)(double x, double u))
        {
            std::vector<std::vector<float>> v;
            for (int j = 0; j < 32; ++j) v.push_back(cycle(f, j / 31.0));
            return v;
        }
        double frac(double x) { return x - std::floor(x); }

        // (W5) the built-ins, as formulas of the cycle's place x (0…1) and the frame's u (0…1)
        double basic(double x, double u)
        {
            // sine → triangle → saw → square: each shape starts at its zero, rising (a smooth morph)
            const double s[4] = {std::sin(2 * kPi * x), (2 / kPi) * std::asin(std::sin(2 * kPi * x)), 2 * frac(x + 0.5) - 1, x < 0.5 ? 1.0 : -1.0};
            const double t = u * 3.0;
            const int seg = std::min(2, (int)t);
            const double w = t - seg;
            return (1 - w) * s[seg] + w * s[seg + 1];
        }
        double pulse(double x, double u) { return x < 0.5 - 0.46 * u ? 1.0 : -1.0; }
        double harmonics(double x, double u)
        {
            const int top = 1 + (int)std::lround(u * 31);
            double y = 0;
            for (int h = 1; h <= top; ++h) y += std::sin(2 * kPi * h * x) / h;
            return y;
        }
        double formant(double x, double u)
        {
            // a → e → i → o → u: (F1, F2) in Hz, interpolated across four spans; harmonics of 130 Hz under them
            static const double kF[5][2] = {{730, 1090}, {530, 1840}, {270, 2290}, {570, 840}, {300, 870}};
            const double t = u * 4.0;
            const int seg = std::min(3, (int)t);
            const double w = t - seg, f1 = (1 - w) * kF[seg][0] + w * kF[seg + 1][0], f2 = (1 - w) * kF[seg][1] + w * kF[seg + 1][1];
            double y = 0;
            for (int h = 1; h <= Wavetable::kMaxHarmonics; ++h)
            {
                const double f = 130.0 * h, g1 = (f - f1) / 90.0, g2 = (f - f2) / 110.0;
                const double a = (std::exp(-0.5 * g1 * g1) + 0.7 * std::exp(-0.5 * g2 * g2) + 0.02) / std::sqrt((double)h);
                y += a * std::sin(2 * kPi * h * x);
            }
            return y;
        }
        double sync(double x, double u) { return 2 * frac((1 + 7 * u) * x) - 1; }
        double fold(double x, double u) { return std::sin((1 + 11 * u) * std::sin(2 * kPi * x)); }
    }

    Wavetable::Wavetable(const std::vector<std::vector<float>> &cycles)
    {
        mFrames = std::clamp((int)cycles.size(), 1, kMaxFrames);
        for (int l = 0; l < kLevels; ++l)
        {
            mPoints[l] = pointsOf(l);
            mData[l].assign((size_t)mFrames * (size_t)mPoints[l], 0.0f);
        }
        const FFT big(kN);
        std::vector<std::unique_ptr<FFT>> small((size_t)kLevels);
        for (int l = 0; l < kLevels; ++l) small[(size_t)l] = std::make_unique<FFT>(mPoints[l]);
        std::vector<std::complex<double>> X((size_t)kN), Y;
        for (int f = 0; f < mFrames && f < (int)cycles.size(); ++f)
        {
            const auto &c = cycles[(size_t)f];
            for (int n = 0; n < kN; ++n) X[(size_t)n] = n < (int)c.size() ? (double)c[(size_t)n] : 0.0;
            big.forward(X.data());
            double peak = 0.0;
            for (int l = 0; l < kLevels; ++l)
            {
                // (W1) harmonics 1 … H_l kept, the DC dropped, rebuilt at N_l points
                const int N = mPoints[l], H = std::min(kMaxHarmonics >> l, N / 2 - 1);
                Y.assign((size_t)N, 0.0);
                const double k = (double)N / kN;
                for (int h = 1; h <= H; ++h)
                {
                    Y[(size_t)h] = X[(size_t)h] * k;
                    Y[(size_t)(N - h)] = X[(size_t)(kN - h)] * k;
                }
                small[(size_t)l]->inverse(Y.data());
                float *d = &mData[l][(size_t)f * (size_t)N];
                for (int n = 0; n < N; ++n)
                {
                    d[n] = (float)Y[(size_t)n].real();
                    if (l == 0) peak = std::max(peak, std::fabs((double)d[n]));
                }
            }
            // a frame's levels share one gain: its level 0 peaks at 1
            const float g = peak > 1e-9 ? (float)(1.0 / peak) : 0.0f;
            for (int l = 0; l < kLevels; ++l)
            {
                float *d = &mData[l][(size_t)f * (size_t)mPoints[l]];
                for (int n = 0; n < mPoints[l]; ++n) d[n] *= g;
            }
        }
    }

    const float *Wavetable::level(int frame, int lvl, int &points) const
    {
        lvl = std::clamp(lvl, 0, kLevels - 1);
        frame = std::clamp(frame, 0, mFrames - 1);
        points = mPoints[lvl];
        return &mData[lvl][(size_t)frame * (size_t)points];
    }

    Sample Wavetable::readLevel(int lvl, int frame, Sample phase) const
    {
        // (W3) Catmull-Rom on the circular table
        const int N = mPoints[lvl], m = N - 1;
        const float *d = &mData[lvl][(size_t)frame * (size_t)N];
        const Sample x = phase * N;
        const int i = (int)x;
        const Sample t = x - i;
        const Sample y0 = d[(i - 1) & m], y1 = d[i & m], y2 = d[(i + 1) & m], y3 = d[(i + 2) & m];
        return y1 + 0.5 * t * (y2 - y0) + t * t * (y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3) + 0.5 * t * t * t * (3.0 * (y1 - y2) + y3 - y0);
    }

    Sample Wavetable::read(Sample phase, Sample position, Sample dt) const
    {
        phase = phase - std::floor(phase);
        // (W4) the two frames either side of the position
        const Sample q = std::clamp(position, (Sample)0.0, (Sample)1.0) * (mFrames - 1);
        const int f0 = (int)q, f1 = std::min(f0 + 1, mFrames - 1);
        const Sample w = q - f0;
        auto atLevel = [&](int l) {
            const Sample a = readLevel(l, f0, phase);
            return w > 0.0 ? a + w * (readLevel(l, f1, phase) - a) : a;
        };
        // (W2) two levels, both below Nyquist for this step
        const Sample lambda = dt > 0.0 ? std::log2(2.0 * kMaxHarmonics * dt) : -1.0;
        if (lambda <= -1.0) return atLevel(0);
        if (lambda >= kLevels - 1) return atLevel(kLevels - 1);
        const int k = (int)std::ceil(lambda);
        const Sample wk = (Sample)k - lambda;
        const int k1 = std::min(k + 1, kLevels - 1);
        const Sample lo = wk > 0.0 ? atLevel(k) : 0.0;
        return wk * lo + (1.0 - wk) * atLevel(k1);
    }

    const std::vector<std::string> &Wavetable::builtInNames()
    {
        static const std::vector<std::string> n = {"Basic", "Pulse", "Harmonics", "Formant", "Sync", "Fold"};
        return n;
    }

    const Wavetable &Wavetable::builtIn(int index)
    {
        // made together on first use (a thread-safe static), immutable after
        static const std::vector<std::unique_ptr<Wavetable>> all = [] {
            std::vector<std::unique_ptr<Wavetable>> v;
            for (auto f : {basic, pulse, harmonics, formant, sync, fold}) v.push_back(std::make_unique<Wavetable>(frames32(f)));
            return v;
        }();
        return *all[(size_t)std::clamp(index, 0, (int)all.size() - 1)];
    }

    std::unique_ptr<Wavetable> Wavetable::fromSample(const float *mono, long long frames)
    {
        if (!mono || frames <= 0) return nullptr;
        std::vector<std::vector<float>> cycles;
        if (frames < kN)
        {
            // (W6) shorter than a cycle: stretched to one
            std::vector<float> c((size_t)kN);
            for (int n = 0; n < kN; ++n)
            {
                const double x = (double)n * (double)(frames - 1) / (kN - 1);
                const long long i = (long long)x;
                const double t = x - (double)i;
                c[(size_t)n] = (float)(mono[i] + t * ((i + 1 < frames ? mono[i + 1] : mono[i]) - mono[i]));
            }
            cycles.push_back(std::move(c));
        }
        else
            for (long long f = 0; f + kN <= frames && (int)cycles.size() < kMaxFrames; f += kN) cycles.emplace_back(mono + f, mono + f + kN);
        return std::make_unique<Wavetable>(cycles);
    }
}
