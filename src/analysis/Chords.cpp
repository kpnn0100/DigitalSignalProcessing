#include "Chords.h"
#include "Stft.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace arstro
{
    std::string chordName(int root, bool minor)
    {
        static const char *kRoots[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
        return std::string(kRoots[((root % 12) + 12) % 12]) + (minor ? "m" : "");
    }

    std::vector<ChordSpan> detectChords(const float *mono, long long frames, double rate, const std::vector<double> &beatsIn, const ChordParams &p)
    {
        std::vector<ChordSpan> out;
        if (frames <= 0 || rate <= 0.0) return out;
        std::vector<double> beats = beatsIn;
        if (beats.size() < 2)
        {
            beats.clear();
            for (double s = 0.0; s < (double)frames / rate; s += 0.5) beats.push_back(s);
            beats.push_back((double)frames / rate);
        }
        const int N = p.fftSize > 0 ? p.fftSize : fftSizeFor(rate, 0.17);
        const Spectrogram S = stft(mono, frames, N);
        // (C1) each frame's chroma: a bin at f adds its power to the pitch class of its nearest note, by a triangle a
        // semitone wide (full at the note, nothing half a semitone off)
        std::vector<std::array<double, 12>> chroma(S.bins.size());
        for (size_t t = 0; t < S.bins.size(); ++t)
        {
            chroma[t].fill(0.0);
            for (int k = 1; k <= N / 2; ++k)
            {
                const double f = k * rate / N;
                if (f < p.minHz || f > p.maxHz) continue;
                const double m = 69.0 + 12.0 * std::log2(f / 440.0), r = std::round(m), w = std::max(0.0, 1.0 - 2.0 * std::fabs(m - r));
                if (w > 0.0) chroma[t][(size_t)(((long long)r % 12 + 12) % 12)] += w * std::norm(S.bins[t][(size_t)k]);
            }
        }
        // (C2) each beat: its frames' chroma averaged (a frame by its centre), unit length; its level from its samples
        const size_t B = beats.size() - 1;
        std::vector<std::array<double, 12>> v(B);
        std::vector<bool> silent(B, true);
        for (size_t b = 0; b < B; ++b)
        {
            v[b].fill(0.0);
            int n = 0;
            for (size_t t = 0; t < S.bins.size(); ++t)
            {
                const double at = (double)t * S.hop / rate;
                if (at < beats[b] || at >= beats[b + 1]) continue;
                for (int c = 0; c < 12; ++c) v[b][(size_t)c] += chroma[t][(size_t)c];
                ++n;
            }
            if (n == 0) // a beat shorter than a hop: the frame nearest its middle
            {
                const size_t t = std::min(S.bins.size() - 1, (size_t)std::lround((beats[b] + beats[b + 1]) * 0.5 * rate / S.hop));
                v[b] = chroma[t];
            }
            double norm = 0.0;
            for (double c : v[b]) norm += c * c;
            norm = std::sqrt(norm);
            for (double &c : v[b]) c = norm > 0.0 ? c / norm : 0.0;
            const long long s0 = std::clamp((long long)std::llround(beats[b] * rate), 0LL, frames), s1 = std::clamp((long long)std::llround(beats[b + 1] * rate), 0LL, frames);
            double e = 0.0;
            for (long long i = s0; i < s1; ++i) e += (double)mono[i] * mono[i];
            silent[b] = s1 <= s0 || 10.0 * std::log10(e / (double)(s1 - s0) + 1e-30) < p.silenceDb;
        }
        // (C3) a beat's score for each of the 24 triads — cosine with the triad's three classes — and for "N"
        constexpr int kStates = 25, kNone = 24;
        auto score = [&](size_t b, int s) {
            if (silent[b]) return s == kNone ? 1.0 : 0.0;
            if (s == kNone) return p.minScore;
            const int root = s % 12, third = s < 12 ? 4 : 3;
            return (v[b][(size_t)root] + v[b][(size_t)((root + third) % 12)] + v[b][(size_t)((root + 7) % 12)]) / std::sqrt(3.0);
        };
        // (C4) Viterbi: the path of states with the most score, a change costing `stay`
        std::vector<std::array<double, kStates>> best(B);
        std::vector<std::array<int, kStates>> from(B);
        for (int s = 0; s < kStates; ++s) best[0][(size_t)s] = score(0, s);
        for (size_t b = 1; b < B; ++b)
            for (int s = 0; s < kStates; ++s)
            {
                int arg = s;
                double top = best[b - 1][(size_t)s];
                for (int q = 0; q < kStates; ++q)
                    if (best[b - 1][(size_t)q] - p.stay > top) { top = best[b - 1][(size_t)q] - p.stay; arg = q; }
                best[b][(size_t)s] = top + score(b, s);
                from[b][(size_t)s] = arg;
            }
        std::vector<int> path(B);
        path[B - 1] = (int)(std::max_element(best[B - 1].begin(), best[B - 1].end()) - best[B - 1].begin());
        for (size_t b = B - 1; b > 0; --b) path[b - 1] = from[b][(size_t)path[b]];
        // (C5)(C6) equal beats merged into spans, each named
        for (size_t b = 0; b < B; ++b)
        {
            const std::string name = path[b] == kNone ? std::string("N") : chordName(path[b] % 12, path[b] >= 12);
            if (!out.empty() && out.back().name == name) out.back().end = beats[b + 1];
            else out.push_back(ChordSpan{beats[b], beats[b + 1], name});
        }
        return out;
    }
}
