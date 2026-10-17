/*
 *  Arstro DSP Library — Vocoder: one sound speaking through another (REQ-fx-vocoder-1). A channel vocoder: N bands,
 *  log-spaced; the strip's own sound (the modulator — a voice) band-passed and followed, band by band; the KEY (the
 *  carrier — a synth on another track) band-passed alike, each band times the modulator's envelope there, summed.
 *  Composed of library Biquads (band-pass, two sections a band) and (C2)'s follower. No key: silence (the carrier is
 *  silent). effects/README.md, Math — Vocoder (VO1)–(VO5). Whole frames; no allocation (up to kMaxChannels).
 */
#pragma once
#include "../base/Sample.h"
#include "../equalizer/Biquad.h"
#include <array>

namespace arstro
{
    class Vocoder
    {
    public:
        static constexpr int kMaxBands = 32, kMaxChannels = 2;
        Vocoder();
        void setBands(int n);
        void setLowHz(Sample hz);
        void setHighHz(Sample hz);
        void setAttackMs(Sample ms);
        void setReleaseMs(Sample ms);
        void setMix(Sample mix) { mMix = mix < 0 ? 0 : mix > 1 ? 1 : mix; }
        void setOutputDb(Sample db);
        /** The carrier for the NEXT process call: `channels` buffers of that block's frames; nullptr = silence. */
        void setKey(const Sample *const *key, int channels) { mKey = key; mKeyChannels = key ? channels : 0; }
        void process(Sample *const *io, int channels, int frames);
        void reset();
        int bands() const { return mBands; }
        /** (VO1) band b's centre, Hz. */
        double centerHz(int b) const;

    private:
        void design();
        int mBands = 16;
        double mLo = 100, mHi = 8000, mAttackMs = 5, mReleaseMs = 50, mMix = 1, mOut = 1, mFs = 48000, mA = 0, mR = 0;
        std::array<std::array<Biquad, 2>, kMaxBands> mMod, mCar; // (VO2) two band-pass sections a band, each path
        std::array<std::array<double, kMaxChannels>, kMaxBands> mEnv{};
        const Sample *const *mKey = nullptr;
        int mKeyChannels = 0;
    };
}
