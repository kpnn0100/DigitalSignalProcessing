#pragma once
/*
 *  Transcribe — a melody's notes from its sound (REQ-analysis-3): monophonic pitch tracking by the library's YIN
 *  (REQ-tuner-1's estimator, shared), framed every eighth of its window; a frame voiced when YIN finds a period under its
 *  threshold and the frame is louder than a gate; its note the nearest semitone; a median over five frames; a note where
 *  one pitch lasts, split where the library's onsets (REQ-onset-1) find a new attack on the same pitch, its start moved
 *  to that attack; its velocity from its loudest frame. The math is in src/analysis/README.md (T1)…(T6).
 *
 *  An analysis of a whole sound, done once, off any audio thread (it allocates). Deterministic.
 */
#include <vector>

namespace arstro
{
    struct TranscribedNote
    {
        double start = 0.0, end = 0.0; // seconds
        int pitch = 0;                 // MIDI note number
        int velocity = 0;              // 1 … 127
    };
    struct TranscribeParams
    {
        int window = 0;             // W; 0: the power of two nearest 43 ms at the rate (2048 at 48 kHz)
        double threshold = 0.15;    // (T1) YIN's: d′ under it is a period
        double gateDb = -50.0;      // (T1) a frame quieter than this is unvoiced
        double minHz = 50.0, maxHz = 2000.0;
        int medianFrames = 5;       // (T3) odd
        double minNoteMs = 60.0;    // (T4) a shorter run is no note
        double onsetSensitivity = 0.5; // (T5) the onsets that split a held pitch (REQ-onset-1's)
    };
    /** The notes of `mono` (`frames` long, at `rate` Hz), by start. */
    std::vector<TranscribedNote> transcribe(const float *mono, long long frames, double rate, const TranscribeParams &p = TranscribeParams());
}
