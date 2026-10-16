#include "TimeStretch.h"
#include "FFT.h"
#include "../meter/Onsets.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace arstro
{
    namespace
    {
        double princarg(double a) { return a - 2.0 * M_PI * std::floor((a + M_PI) / (2.0 * M_PI)); } // into [−π, π)

        double besselI0(double x)
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

        /** (P1)…(P6) one channel, framed at `ta` (analysis centres) → `ts` = k·Hs; `reset[k]`: the phases restart. */
        void vocode(const std::vector<float> &x, const std::vector<long long> &ta, long long first, const std::vector<char> &reset, int N, int Hs,
                    const FFT &fft, const std::vector<double> &w, double norm, std::vector<double> &y)
        {
            const int bins = N / 2 + 1;
            std::vector<std::complex<double>> buf((size_t)N);
            std::vector<double> mag((size_t)bins), pa((size_t)bins), paPrev((size_t)bins, 0.0), ps((size_t)bins, 0.0), psOut((size_t)bins);
            std::vector<int> peaks;
            peaks.reserve((size_t)bins);
            const long long L = (long long)x.size();
            for (size_t k = 0; k < ta.size(); ++k)
            {
                // (P2) the windowed frame centred on ta_k — outside the sound, silence
                for (int n = 0; n < N; ++n)
                {
                    const long long i = ta[k] - N / 2 + n;
                    buf[(size_t)n] = {i >= 0 && i < L ? w[(size_t)n] * x[(size_t)i] : 0.0, 0.0};
                }
                fft.forward(buf.data());
                for (int b = 0; b < bins; ++b)
                {
                    mag[(size_t)b] = std::abs(buf[(size_t)b]);
                    pa[(size_t)b] = std::arg(buf[(size_t)b]);
                }
                if (k == 0 || reset[k])
                    ps = pa; // (P5)/(P6) the first frame, and a frame where a sound begins: its own phases
                else
                {
                    // (P3) each peak's frequency from how far its phase moved over the actual hop; (P4) the bins around a
                    // peak keep their phase relation to it (identity phase locking)
                    const double Ha = (double)(ta[k] - ta[k - 1]);
                    peaks.clear();
                    for (int b = 0; b < bins; ++b)
                    {
                        const double m = mag[(size_t)b];
                        bool peak = true;
                        for (int d = -2; d <= 2 && peak; ++d)
                            if (d != 0 && b + d >= 0 && b + d < bins && mag[(size_t)(b + d)] > m) peak = false;
                        if (peak && (peaks.empty() || b > peaks.back() + 1)) peaks.push_back(b);
                    }
                    for (int b : peaks)
                    {
                        const double omega = 2.0 * M_PI * b / N;
                        const double dev = Ha > 0 ? princarg(pa[(size_t)b] - paPrev[(size_t)b] - omega * Ha) : 0.0;
                        const double inst = omega + (Ha > 0 ? dev / Ha : 0.0);
                        psOut[(size_t)b] = ps[(size_t)b] + inst * Hs;
                    }
                    for (size_t j = 0; j < peaks.size(); ++j)
                    {
                        // a peak's region: half way to the peaks either side
                        const int b = peaks[j];
                        const int lo = j == 0 ? 0 : (peaks[j - 1] + b) / 2 + 1, hi = j + 1 == peaks.size() ? bins - 1 : (b + peaks[j + 1]) / 2;
                        for (int q = lo; q <= hi; ++q)
                            if (q != b) psOut[(size_t)q] = psOut[(size_t)b] + (pa[(size_t)q] - pa[(size_t)b]);
                    }
                    if (peaks.empty()) psOut = pa;
                    ps = psOut;
                }
                paPrev = pa;
                // (P7) the frame back with the synthesis phases, windowed again, added at ts_k = k·Hs
                for (int b = 0; b < bins; ++b) buf[(size_t)b] = std::polar(mag[(size_t)b], ps[(size_t)b]);
                for (int b = 1; b < N / 2; ++b) buf[(size_t)(N - b)] = std::conj(buf[(size_t)b]);
                fft.inverse(buf.data());
                const long long at = (first + (long long)k) * Hs - N / 2;
                for (int n = 0; n < N; ++n)
                {
                    const long long o = at + n;
                    if (o >= 0 && o < (long long)y.size()) y[(size_t)o] += norm * w[(size_t)n] * buf[(size_t)n].real();
                }
            }
        }
    }

    std::vector<float> resample(const std::vector<float> &in, double step, long long frames)
    {
        // (R1) cutoff c = min(1, 1/step) of the input's Nyquist; (R2) Z = 16 zero crossings each side, Kaiser β = 8.6
        constexpr int kZero = 16, kRes = 512;
        constexpr double kBeta = 8.6;
        const double c = std::min(1.0, 1.0 / std::max(1e-9, step));
        const double R = kZero / c; // the kernel's half width, input samples
        const int tableN = (int)std::ceil(R * kRes) + 2;
        std::vector<double> table((size_t)tableN);
        const double i0b = besselI0(kBeta);
        for (int i = 0; i < tableN; ++i)
        {
            const double t = (double)i / kRes, u = t / R;
            const double s = t == 0.0 ? 1.0 : std::sin(M_PI * c * t) / (M_PI * c * t);
            table[(size_t)i] = u >= 1.0 ? 0.0 : c * s * besselI0(kBeta * std::sqrt(1.0 - u * u)) / i0b;
        }
        auto h = [&](double t) {
            const double a = std::fabs(t) * kRes;
            const int i = (int)a;
            if (i + 1 >= tableN) return 0.0;
            const double f = a - i;
            return table[(size_t)i] + f * (table[(size_t)i + 1] - table[(size_t)i]); // (R3) the kernel, tabulated
        };
        std::vector<float> out((size_t)std::max(0LL, frames));
        const long long L = (long long)in.size(), r = (long long)std::ceil(R);
        for (long long j = 0; j < frames; ++j)
        {
            const double xpos = j * step;
            const long long m0 = (long long)std::floor(xpos);
            double acc = 0.0;
            for (long long m = std::max(0LL, m0 - r + 1); m <= std::min(L - 1, m0 + r); ++m) acc += in[(size_t)m] * h(xpos - (double)m);
            out[(size_t)j] = (float)acc;
        }
        return out;
    }

    std::vector<std::vector<float>> timeStretch(const std::vector<std::vector<float>> &channels, double rate, const StretchParams &p)
    {
        if (channels.empty()) return {};
        const long long L = (long long)channels[0].size();
        // (P9) a map given: its anchors, checked — from (0, 0), both rising — its end the output's length
        std::vector<std::pair<double, double>> given;
        if (p.map.size() >= 2 && p.map.front().first == 0.0 && p.map.front().second == 0.0)
        {
            bool rising = true;
            for (size_t k = 1; k < p.map.size(); ++k)
                rising = rising && p.map[k].first > p.map[k - 1].first && p.map[k].second > p.map[k - 1].second;
            if (rising) given = p.map;
        }
        const double ratio = given.empty() ? std::clamp(p.ratio, 0.25, 4.0) : given.back().first / std::max(1.0, given.back().second);
        const double semis = std::clamp(p.semitones, -24.0, 24.0);
        int N = 256;
        while (N < std::clamp(p.fftSize, 256, 16384)) N <<= 1;
        const int O = p.overlap >= 8 ? 8 : 4, Hs = N / O;
        const double pitch = std::pow(2.0, semis / 12.0);
        const double beta = ratio * pitch;                  // (P8) stretched this much, then read back `pitch` times faster
        const long long outFrames = given.empty() ? (long long)std::llround(ratio * (double)L) : (long long)std::llround(given.back().first);
        const long long midFrames = given.empty() ? (long long)std::llround(beta * (double)L) : (long long)std::llround(given.back().first * pitch);
        // (P9) the base map, stretched time (before the pitch's read-back) → input: the given anchors, or βt's line
        std::vector<std::pair<double, double>> base;
        if (given.empty()) base = {{0.0, 0.0}, {beta * (double)L, (double)L}};
        else
            for (const auto &a : given) base.push_back({a.first * pitch, std::min(a.second, (double)L)});
        const double Lmap = base.back().second; // the input the map reaches
        auto baseAt = [&](double t) { // input at stretched time t
            if (t <= base.front().first) return (t - base.front().first) * (base[1].second - base[0].second) / (base[1].first - base[0].first);
            size_t j = 1;
            while (j + 1 < base.size() && base[j].first < t) ++j;
            const auto &a = base[j - 1], &b = base[j];
            return a.second + (t - a.first) * (b.second - a.second) / (b.first - a.first);
        };
        auto timeOf = [&](double in) { // stretched time an input frame is heard at (the inverse)
            size_t j = 1;
            while (j + 1 < base.size() && base[j].second < in) ++j;
            const auto &a = base[j - 1], &b = base[j];
            return a.first + (in - a.second) * (b.first - a.first) / (b.second - a.second);
        };
        // (P6) where a sound begins — in the channels' mean: around each onset the time map runs at slope 1 (its attack
        // copied, not stretched — and in no frame before it), the stretch taken up between them
        std::vector<long long> onsets;
        if (p.transients >= 0.0 && L > 0)
        {
            std::vector<float> mono((size_t)L, 0.0f);
            for (const auto &ch : channels)
                for (long long i = 0; i < L; ++i) mono[(size_t)i] += ch[(size_t)i] / (float)channels.size();
            OnsetParams op;
            op.sensitivity = std::clamp(p.transients, 0.0, 1.0);
            onsets = detectOnsets(mono.data(), L, rate, op);
        }
        std::vector<std::pair<double, double>> map{{0.0, 0.0}}; // (output, input) anchors, both rising: zone ends
        std::vector<double> zoneStarts;                        // where each rigid zone begins, output samples
        const double shrink = std::min(1.0, beta);
        for (size_t i = 0; i < onsets.size(); ++i)
        {
            const double o = (double)onsets[i];
            if (o >= Lmap) break;
            const double c = given.empty() ? beta * o : timeOf(o);
            const double prevIn = i == 0 ? 0.0 : (double)onsets[i - 1], nextIn = i + 1 < onsets.size() ? std::min(Lmap, (double)onsets[i + 1]) : Lmap;
            double A, B;
            if (given.empty())
            {
                A = std::min(N / 2.0, 0.25 * shrink * (o - prevIn));
                B = std::min(N / 2.0, 0.25 * shrink * (nextIn - o));
            }
            else // a quarter of what lies between, in input and in output alike
            {
                A = std::min(N / 2.0, 0.25 * std::min(o - prevIn, c - timeOf(prevIn)));
                B = std::min(N / 2.0, 0.25 * std::min(nextIn - o, timeOf(nextIn) - c));
            }
            if (A + B < 1.0) continue;
            map.push_back({c - A, o - A});
            map.push_back({c + B, o + B});
            zoneStarts.push_back(c - A);
        }
        map.push_back({base.back().first, Lmap});
        const double slope0 = (base[1].second - base[0].second) / (base[1].first - base[0].first);
        const double slope1 = (base.back().second - base[base.size() - 2].second) / (base.back().first - base[base.size() - 2].first);
        auto inputAt = [&](double ts) {
            if (ts <= 0.0) return ts * slope0;
            if (ts >= map.back().first) return map.back().second + (ts - map.back().first) * slope1;
            size_t j = 1;
            while (j + 1 < map.size() && map[j].first < ts) ++j;
            const auto &a = map[j - 1], &b = map[j];
            if (b.first <= a.first) return b.second;
            if (given.empty() || j % 2 == 0) return a.second + (ts - a.first) * (b.second - a.second) / (b.first - a.first); // a zone, or the line
            // (P9) a gap between zones: the map's own shape there, scaled to meet them
            const double m0 = baseAt(a.first), m1 = baseAt(b.first);
            return m1 > m0 ? a.second + (baseAt(ts) - m0) * (b.second - a.second) / (m1 - m0) : a.second + (ts - a.first) * (b.second - a.second) / (b.first - a.first);
        };
        // (P1) the frames: synthesis centres k·Hs over the stretched span (and half a window either side), analysis
        // centres where the map sends them
        const long long first = -(long long)(N / 2 / Hs), last = (midFrames + N / 2) / Hs + 1;
        std::vector<long long> ta;
        for (long long k = first; k <= last; ++k) ta.push_back((long long)std::llround(inputAt((double)(k * Hs))));
        // (P6) the first frame centred in a zone takes its own phases: the zone after it is copied coherently
        std::vector<char> reset(ta.size(), 0);
        for (double z : zoneStarts)
            for (size_t k = 1; k < ta.size(); ++k)
                if ((double)((first + (long long)k - 1) * Hs) < z && (double)((first + (long long)k) * Hs) >= z) { reset[k] = 1; break; }
        FFT fft(N);
        std::vector<double> w((size_t)N);
        for (int n = 0; n < N; ++n) w[(size_t)n] = 0.5 - 0.5 * std::cos(2.0 * M_PI * n / N); // periodic Hann
        const double norm = 8.0 / (3.0 * O);  // (P7) Σ w² over the hops is 3O/8
        std::vector<std::vector<float>> out;
        for (const auto &x : channels)
        {
            std::vector<double> y((size_t)midFrames, 0.0);
            vocode(x, ta, first, reset, N, Hs, fft, w, norm, y);
            std::vector<float> f(y.begin(), y.end());
            if (std::fabs(pitch - 1.0) < 1e-12) { f.resize((size_t)outFrames, 0.0f); out.push_back(std::move(f)); }
            else out.push_back(resample(f, pitch, outFrames)); // (P8) the pitch: read back `pitch` samples a sample
        }
        return out;
    }
}
