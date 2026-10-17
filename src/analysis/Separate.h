#pragma once
/*
 *  Separate — a sound split into what sustains and what strikes (REQ-analysis-2): harmonic / percussive separation by
 *  median filtering (Fitzgerald). In the magnitude spectrogram a held tone is a horizontal line and a hit a vertical
 *  one: a median along time keeps the lines, along frequency the columns. Soft (Wiener) masks from the two, taken from
 *  the channels' sum so every channel splits alike (the stereo image kept), shape each channel's spectrum; the masks sum
 *  to one, so the two halves sum back to the sound. The math is in src/analysis/README.md (H1)…(H4).
 *
 *  An analysis of a whole sound, done once, off any audio thread (it allocates). Deterministic.
 */
#include <vector>

namespace arstro
{
    struct SeparateParams
    {
        int fftSize = 0;        // N; 0: the power of two nearest 43 ms at the rate (2048 at 48 kHz)
        int timeMedian = 17;    // (H2) frames in the median along time — odd
        int freqMedian = 17;    // (H2) bins in the median along frequency — odd
        double power = 2.0;     // (H3) the masks' exponent: 2 is Wiener's
    };
    struct Separation
    {
        std::vector<std::vector<float>> harmonic, percussive; // each: the channels, as long as the sound
    };
    /** `channels` (each the same length) at `rate` split; empty in, empty out. */
    Separation separateHarmonicPercussive(const std::vector<std::vector<float>> &channels, double rate, const SeparateParams &p = SeparateParams());
}
