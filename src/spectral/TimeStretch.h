#pragma once
/*
 *  TimeStretch — a sound made longer or shorter with its pitch kept, or its pitch moved with its length kept
 *  (REQ-stretch-1…3): a phase vocoder with identity phase locking whose time map copies the span around each place a
 *  sound begins (the library's own onsets, REQ-onset-1) — phases reset — so an attack stays an attack, where it was; the pitch by stretching 2^(s/12) further and
 *  reading that back band-limited (REQ-resample-1). The math is in src/spectral/README.md (P1)…(P8), (R1)…(R3).
 *
 *  An analysis of a whole sound, done once, off any audio thread (it allocates). Deterministic: no clock, no
 *  threads, no randomness — the same sound and parameters give the same samples (Solaris R-RENDER-1).
 */
#include <vector>

namespace arstro
{
    struct StretchParams
    {
        double ratio = 1.0;       // the output's length over the input's, 0.25 … 4
        double semitones = 0.0;   // the pitch moved, −24 … +24
        int fftSize = 2048;       // N, a power of two, 256 … 16384 (2048 at 48 kHz: 43 ms)
        int overlap = 4;          // O, frames per window: the synthesis hop is N/O (4 or 8)
        double transients = 0.5;  // (P6) the onset sensitivity of the attacks kept whole; < 0: none (a plain vocoder)
        /** (P9) A time map in place of `ratio`: (output frame, input frame) anchors from (0, 0), both rising — where each
         *  input frame is heard (a tempo that changes). The output is the last anchor's frames long; the input past
         *  its last anchor is not read. Empty: the ratio's straight line. */
        std::vector<std::pair<double, double>> map;
    };

    /** `channels` (equal lengths) at `rate` Hz → round(ratio · length) frames each, the pitch moved `semitones`.
     *  Every channel is framed alike and reset at the same onsets (heard in their mean). Out-of-range parameters
     *  are clamped to the ranges above. */
    std::vector<std::vector<float>> timeStretch(const std::vector<std::vector<float>> &channels, double rate, const StretchParams &p = StretchParams());

    /** (R1)…(R3) `in` read every `step` samples from 0 — `frames` of them — through a Kaiser-windowed sinc whose cutoff
     *  falls to 1/step when step > 1 (no alias). Outside `in` is silence. */
    std::vector<float> resample(const std::vector<float> &in, double step, long long frames);
}
