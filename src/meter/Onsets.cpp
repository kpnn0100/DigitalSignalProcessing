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
}
