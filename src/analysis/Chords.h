#pragma once
/*
 *  Chords — the chords a sound plays, beat by beat (REQ-analysis-4): a chromagram (each spectral bin's power added to its
 *  pitch class) averaged over each beat the caller gives, scored against the 24 major and minor triads by cosine, "N"
 *  (no chord) under a floor or in silence, smoothed by a Viterbi pass that pays a little for every change so a passing
 *  note does not flip the chord; equal neighbours merged into spans. The math is in src/analysis/README.md (C1)…(C6).
 *
 *  An analysis of a whole sound, done once, off any audio thread (it allocates). Deterministic.
 */
#include <string>
#include <vector>

namespace arstro
{
    struct ChordParams
    {
        int fftSize = 0;              // N; 0: the power of two nearest 170 ms at the rate (8192 at 48 kHz)
        double minHz = 110.0, maxHz = 4000.0; // (C1) the bins heard
        double minScore = 0.6;        // (C3) a beat scoring under it against every triad is "N"
        double silenceDb = -60.0;     // (C3) a beat quieter than this is "N"
        double stay = 0.1;            // (C4) what a change of chord costs, in score
    };
    struct ChordSpan
    {
        double start = 0.0, end = 0.0; // seconds
        std::string name;              // "C", "Am", "F#", "Bbm" — "N": none
    };
    /** A chord's name: `root` 0 … 11 from C, minor or major. */
    std::string chordName(int root, bool minor);
    /** The chords of `mono` (`frames` long, at `rate` Hz) over the beats `beats` — their boundaries in seconds, rising;
     *  fewer than two: half-second beats over the whole sound. */
    std::vector<ChordSpan> detectChords(const float *mono, long long frames, double rate, const std::vector<double> &beats,
                                        const ChordParams &p = ChordParams());
}
