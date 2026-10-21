/*
 *  Arstro DSP Library — Vowel: a sound shaped like a voice saying a vowel (REQ-fx-vowel-1). Three band-passes at a
 *  vowel's first three formants — the resonances of a vocal tract — summed, morphing through A, E, I, O, U as `vowel`
 *  moves from 0 to 4, moved up or down by `shift`. A bright synth through it reads as a vocal chop, with no recording.
 *  Composed: each band is the library's RBJ band-pass (equalizer/Biquad::design), recomputed as the vowel glides.
 *  effects/README.md, Math — Vowel (VW1)–(VW5). Whole frames; no allocation (up to kMaxChannels).
 */
#pragma once
#include "../base/Sample.h"
#include "../equalizer/Biquad.h"
#include <array>
#include <cstddef>

namespace arstro
{
    class Vowel
    {
    public:
        static constexpr int kMaxChannels = 8, kFormants = 3, kVowels = 5;
        /** (VW1) the three formants of A E I O U, Hz — a man's speaking voice. */
        static const double kTable[kVowels][kFormants];
        Vowel();
        /** 0 … 4: A, E, I, O, U, and every vowel between. */
        void setVowel(Sample v) { mVowelTarget = v < 0 ? 0 : v > 4 ? 4 : v; }
        /** The formants moved by this many semitones (a smaller or larger tract), −12 … 12. */
        void setShift(Sample st) { mShift = st; mDirty = true; }
        /** How narrow the bands are, 0.5 … 4 (1: a voice's own bandwidths). */
        void setResonance(Sample r) { mRes = r < 0.25 ? 0.25 : r; mDirty = true; }
        void setMix(Sample m) { mMix = m < 0 ? 0 : m > 1 ? 1 : m; }
        void setOutputDb(Sample db);
        /** The vowel's glide toward a new setting, ms (0: at once). */
        void setGlideMs(Sample ms);
        void process(Sample *const *io, int channels, int frames);
        void reset();
        /** (VW2) the formant `k` (0 … 2) at vowel position v, shifted by `st` semitones, Hz. */
        static double formantHz(double v, int k, double st);
        /** The vowel position the bands were last designed at (for tests). */
        double vowelNow() const { return mVowel; }

    private:
        void design();
        double mVowel = 0.0, mVowelTarget = 0.0, mDesigned = -1.0, mShift = 0.0, mRes = 1.0, mMix = 1.0, mOut = 1.0;
        double mAlpha = 1.0, mFs = 48000.0;
        bool mDirty = true;
        int mCount = 0;
        std::array<Biquad::Coeffs, kFormants> mC{};
        std::array<std::array<std::array<double, 2>, kFormants>, kMaxChannels> mZ{};
    };
}
