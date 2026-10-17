/*
 *  Arstro DSP Library — Tuner: the pitch of what passes through, untouched (REQ-tuner-1). YIN (de Cheveigné &
 *  Kawahara, 2002) over a kWindow-sample window every kHop: the difference function (by the library FFT: a
 *  correlation and running energies), normalised by its running mean, the first dip under kThreshold, refined by a
 *  parabola — a fundamental, not its loudest harmonic. Its readings (REQ-device-11): frequency (Hz, 0 = no pitch),
 *  note (the nearest, a MIDI number, against `reference` = A4), cents (−50 … +50 from it), level (dBFS of the window).
 *  Written into atomics, read from any thread. src/meter/README.md, Math — Tuner (Y1)–(Y6). process never allocates.
 */
#pragma once
#include "../base/Sample.h"
#include "../spectral/FFT.h"
#include <atomic>
#include <complex>
#include <vector>

namespace arstro
{
    class Tuner
    {
    public:
        static constexpr int kWindow = 2048, kHop = 512;
        static constexpr double kThreshold = 0.15, kMinHz = 50.0, kMaxHz = 2000.0, kQuietDb = -60.0;
        enum Reading { Frequency = 0, Note, Cents, Level, kReadings };
        Tuner();
        void setReferenceHz(Sample hz) { mRef = hz < 400 ? 400 : hz > 480 ? 480 : hz; }
        /** Listens (the channels' mean); the audio is not changed. */
        void process(Sample *const *io, int channels, int frames);
        void reset();
        double reading(int index) const { return index >= 0 && index < kReadings ? mOut[index].load(std::memory_order_relaxed) : 0.0; }

    private:
        void estimate();
        double mFs = 48000, mRef = 440.0;
        std::vector<double> mRing; // the last kWindow samples, oldest at mAt
        int mAt = 0, mSince = 0;
        long long mHeard = 0;
        FFT mFft;
        std::vector<std::complex<double>> mA, mB; // (Y1) work
        std::vector<double> mX, mRaw, mD, mPrefix; // the window in order; d(τ); d′(τ); running energies
        std::atomic<double> mOut[kReadings];
    };
}
