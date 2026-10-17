#pragma once
/*
 *  Stft — a sound's short-time Fourier transform and its exact inverse (REQ-analysis-1): periodic Hann frames, a hop of
 *  a quarter frame, centred (the sound padded half a frame each side, so its first sample is a frame's middle); the
 *  inverse windows again and divides by the sum of the windows' squares at each sample, so an untouched spectrum gives
 *  the sound back to rounding — the base of the separation (REQ-analysis-2) and the chords (REQ-analysis-4). The math is
 *  in src/analysis/README.md (S1)…(S4).
 *
 *  An analysis of a whole sound, done once, off any audio thread (it allocates). Deterministic.
 */
#include <complex>
#include <vector>

namespace arstro
{
    /** The frames of a sound: frame t holds bins 0 … N/2 of the window centred on sample t·hop. */
    struct Spectrogram
    {
        int fftSize = 0, hop = 0;
        long long frames = 0;                              // the sound's length, in samples
        std::vector<std::vector<std::complex<double>>> bins; // [frame][bin]
    };

    /** The FFT size the analysis uses at a rate: the power of two nearest `seconds` of it (≥ 256). */
    int fftSizeFor(double rate, double seconds);
    /** (S1)(S2): `x` (`frames` long) as frames of `fftSize` (a power of two ≥ 16) every fftSize/4. */
    Spectrogram stft(const float *x, long long frames, int fftSize);
    /** (S3)(S4): the sound back — `s.frames` samples. */
    std::vector<float> istft(const Spectrogram &s);
}
