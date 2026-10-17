#include "Transcribe.h"
#include "Stft.h"
#include "../meter/Onsets.h"
#include "../meter/Yin.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    std::vector<TranscribedNote> transcribe(const float *mono, long long frames, double rate, const TranscribeParams &p)
    {
        std::vector<TranscribedNote> out;
        const int W = p.window > 0 ? p.window : fftSizeFor(rate, 0.0427), H = W / 2, hop = std::max(1, W / 8);
        if (frames < W || rate <= 0.0) return out;
        // (T1)(T2) each frame: its level over the half YIN compares, its period, its note
        Yin yin(W);
        std::vector<double> x((size_t)W);
        const long long T = (frames - W) / hop + 1;
        std::vector<int> note((size_t)T, -1);
        std::vector<double> level((size_t)T, -144.0);
        for (long long t = 0; t < T; ++t)
        {
            const long long s = t * hop;
            double e = 0.0;
            for (int j = 0; j < W; ++j)
            {
                x[(size_t)j] = mono[s + j];
                if (j < H) e += x[(size_t)j] * x[(size_t)j];
            }
            level[(size_t)t] = 10.0 * std::log10(e / H + 1e-30);
            if (level[(size_t)t] < p.gateDb) continue;
            const double period = yin.period(x.data(), rate, p.minHz, p.maxHz, p.threshold);
            if (period <= 0.0) continue;
            note[(size_t)t] = (int)std::lround(69.0 + 12.0 * std::log2(rate / period / 440.0));
        }
        // (T3) a median over the frames around each: a stray octave or a lone unvoiced frame does not break a note
        const int M = std::max(1, p.medianFrames | 1);
        std::vector<int> smooth((size_t)T), win;
        for (long long t = 0; t < T; ++t)
        {
            win.clear();
            for (long long u = t - M / 2; u <= t + M / 2; ++u)
                if (u >= 0 && u < T) win.push_back(note[(size_t)u]);
            std::nth_element(win.begin(), win.begin() + (long)(win.size() / 2), win.end());
            smooth[(size_t)t] = win[win.size() / 2];
        }
        // (T5) the attacks, for a held pitch struck again and for where a note begins
        const std::vector<long long> onsets = detectOnsets(mono, frames, rate, OnsetParams{p.onsetSensitivity, p.minNoteMs, p.gateDb});
        const double centre = H * 0.5; // a frame's time: the middle of the half YIN compares
        auto timeOf = [&](long long t) { return (t * hop + centre) / rate; };
        // (T4) runs of one pitch — split at an attack inside them — at least minNoteMs long
        long long a = 0;
        while (a < T)
        {
            if (smooth[(size_t)a] < 0) { ++a; continue; }
            long long b = a;
            while (b < T && smooth[(size_t)b] == smooth[(size_t)a]) ++b;
            // the run's attacks: one inside it (away from its ends by a note's least length) splits it there
            std::vector<double> cuts{timeOf(a)};
            for (long long o : onsets)
            {
                const double ot = (double)o / rate;
                if (std::fabs(ot - timeOf(a)) <= (double)W / 2.0 / rate) cuts[0] = ot; // its start moved to its attack
                else if (ot > timeOf(a) + p.minNoteMs / 1000.0 && ot < timeOf(b - 1) - p.minNoteMs / 1000.0) cuts.push_back(ot);
            }
            cuts.push_back(timeOf(b - 1) + (double)hop / rate);
            for (size_t c = 0; c + 1 < cuts.size(); ++c)
            {
                TranscribedNote n;
                n.start = cuts[c];
                n.end = cuts[c + 1];
                n.pitch = smooth[(size_t)a];
                if ((n.end - n.start) * 1000.0 < p.minNoteMs) continue;
                // (T6) its velocity: its loudest frame, −60 … 0 dBFS → 1 … 127
                double loud = -144.0;
                for (long long t = a; t < b; ++t)
                    if (timeOf(t) >= n.start - (double)hop / rate && timeOf(t) < n.end) loud = std::max(loud, level[(size_t)t]);
                n.velocity = (int)std::clamp(std::lround(1.0 + 126.0 * (loud + 60.0) / 60.0), 1L, 127L);
                out.push_back(n);
            }
            a = b;
        }
        // a note ends where the next begins (a start moved back to its attack may reach under the last's end)
        for (size_t i = 0; i + 1 < out.size(); ++i) out[i].end = std::min(out[i].end, out[i + 1].start);
        out.erase(std::remove_if(out.begin(), out.end(), [&](const TranscribedNote &n) { return (n.end - n.start) * 1000.0 < p.minNoteMs * 0.5; }), out.end());
        return out;
    }
}
