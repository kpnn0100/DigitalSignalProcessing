#include "Onsets.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    std::vector<long long> detectOnsets(const float *x, long long frames, double rate, const OnsetParams &p)
    {
        std::vector<long long> out;
        if (!x || frames <= 0 || rate <= 0) return out;
        // (O1) frames of N samples every H: their energy in dB
        const long long N = 1024, H = 256;
        const long long M = frames > N ? (frames - N) / H + 1 : 1;
        std::vector<double> L((size_t)M), d((size_t)M, 0.0);
        for (long long m = 0; m < M; ++m)
        {
            double e = 0.0;
            for (long long i = m * H; i < std::min(frames, m * H + N); ++i) e += (double)x[i] * x[i];
            L[(size_t)m] = 10.0 * std::log10(e / (double)N + 1e-12);
        }
        // (O2) the flux: how much louder each frame is than the one before
        for (long long m = 1; m < M; ++m) d[(size_t)m] = std::max(0.0, L[(size_t)m] - L[(size_t)m - 1]);
        const double delta = 3.0 + 12.0 * (1.0 - std::clamp(p.sensitivity, 0.0, 1.0));
        const long long gap = (long long)std::llround(p.minGapMs * 1e-3 * rate);
        std::vector<double> win;
        for (long long m = 1; m < M; ++m)
        {
            // (O3) above the local median by δ, and (O4) a peak of the flux, out of silence, apart from the last
            win.clear();
            for (long long j = std::max(1LL, m - 8); j <= std::min(M - 1, m + 8); ++j) win.push_back(d[(size_t)j]);
            std::nth_element(win.begin(), win.begin() + (long)(win.size() / 2), win.end());
            const double thr = win[win.size() / 2] + delta;
            const double v = d[(size_t)m];
            if (v <= thr || L[(size_t)m] < p.floorDb) continue;
            if ((m > 1 && d[(size_t)m - 1] > v) || (m + 1 < M && d[(size_t)m + 1] >= v)) continue;
            // (O5) to the sample: the first in the window a quarter of the way from what sounded before it to its peak
            const long long a = std::max(0LL, (m - 1) * H), b = std::min(frames, m * H + N);
            float peak = 0.0f, before = 0.0f;
            for (long long i = a; i < b; ++i) peak = std::max(peak, std::fabs(x[i]));
            for (long long i = std::max(0LL, a - H); i < a; ++i) before = std::max(before, std::fabs(x[i]));
            const float level = before + 0.25f * std::max(0.0f, peak - before);
            long long at = m * H;
            for (long long i = a; i < b; ++i)
                if (std::fabs(x[i]) > level) { at = i; break; }
            if (!out.empty() && at - out.back() < gap) continue;
            out.push_back(at);
        }
        return out;
    }

    double estimateTempo(const float *x, long long frames, double rate, const TempoParams &p)
    {
        if (!x || frames <= 0 || rate <= 0) return 0.0;
        // (B1) how much each frame rises over the one before — its energy log-compressed, so a hit out of silence and a
        // hit over a tail count alike — smoothed over ±2 frames, so a beat whose period falls between frames keeps its
        // height; at a frame rate of rate/H
        const long long N = 512, H = 256;
        const long long M = frames > N ? (frames - N) / H + 1 : 1;
        if (M < 16) return 0.0;
        std::vector<double> S((size_t)M), raw((size_t)M, 0.0), d((size_t)M, 0.0);
        for (long long m = 0; m < M; ++m)
        {
            double e = 0.0;
            for (long long i = m * H; i < std::min(frames, m * H + N); ++i) e += (double)x[i] * x[i];
            S[(size_t)m] = std::log1p(1000.0 * e / (double)N);
        }
        double rise = 0.0;
        for (long long m = 1; m < M; ++m)
        {
            raw[(size_t)m] = std::max(0.0, S[(size_t)m] - S[(size_t)m - 1]);
            rise = std::max(rise, raw[(size_t)m]);
        }
        if (rise < 0.1) return 0.0; // nothing rises from one frame to the next: no beat (a held tone's ripple)
        static const double kSmooth[5] = {1.0 / 9, 2.0 / 9, 3.0 / 9, 2.0 / 9, 1.0 / 9};
        for (long long m = 0; m < M; ++m)
            for (int j = -2; j <= 2; ++j)
                if (m + j >= 0 && m + j < M) d[(size_t)m] += kSmooth[j + 2] * raw[(size_t)(m + j)];
        double mean = 0.0;
        for (double v : d) mean += v;
        mean /= (double)M;
        double var = 0.0;
        for (double &v : d) { v -= mean; var += v * v; }
        // (B2) its autocorrelation, normalised by the overlap; (B3) weighted toward 120 bpm, 1.4 octaves either way
        const double fr = rate / (double)H;
        const long long lo = std::max(1LL, (long long)std::floor(fr * 60.0 / p.maxBpm)), hi = std::min(M - 2, (long long)std::ceil(fr * 60.0 / p.minBpm));
        if (hi <= lo + 1) return 0.0;
        auto r = [&](long long tau) {
            double s = 0.0;
            for (long long m = 0; m + tau < M; ++m) s += d[(size_t)m] * d[(size_t)(m + tau)];
            return s / (double)(M - tau);
        };
        std::vector<double> ac((size_t)(hi + 2), 0.0);
        for (long long tau = std::max(1LL, lo - 1); tau <= hi + 1 && tau < M; ++tau) ac[(size_t)tau] = r(tau);
        const double tau0 = 0.5 * fr; // 120 bpm
        long long best = -1;
        double bestScore = 0.0;
        for (long long tau = lo; tau <= hi; ++tau)
        {
            if (ac[(size_t)tau] <= 0.0 || ac[(size_t)tau] < ac[(size_t)tau - 1] || ac[(size_t)tau] < ac[(size_t)tau + 1]) continue; // a peak
            const double oct = std::log2((double)tau / tau0) / 1.4;
            const double score = ac[(size_t)tau] * std::exp(-0.5 * oct * oct);
            if (score > bestScore) { bestScore = score; best = tau; }
        }
        if (best < 0) return 0.0;
        // (B4) the peak between frames: a parabola through it and its neighbours
        const double a = ac[(size_t)best - 1], b = ac[(size_t)best], c = ac[(size_t)best + 1];
        const double den = a - 2.0 * b + c, off = den < 0.0 ? std::clamp(0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
        double bpm = 60.0 * fr / ((double)best + off);
        // (B5) a loop cut to whole beats: the tempo that makes it so, when that is this near
        const double seconds = (double)frames / rate, beats = std::round(seconds * bpm / 60.0);
        if (p.snap > 0.0 && beats >= 2.0)
        {
            const double whole = beats * 60.0 / seconds;
            if (std::fabs(whole / bpm - 1.0) <= p.snap) bpm = whole;
        }
        return bpm;
    }
}
