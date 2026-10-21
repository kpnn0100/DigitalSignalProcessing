/*
 *  Arstro DSP Library — Shaper: a volume shaped by the song's beat (REQ-fx-shaper-1). A tempo-synced LFO — a note
 *  value long — turns the sound down and up in a shape: a sine, a triangle, a saw either way, a square, or a pump (the
 *  sidechain's duck, without a key). The beat is the host's (Device::Transport, REQ-device-12): the phase at every
 *  sample is the song's, so a wobble lands on the grid wherever playing starts. effects/README.md, Math — Shaper
 *  (SH1)–(SH5). Whole frames; no allocation (up to kMaxChannels; beyond them a channel passes untouched).
 */
#pragma once
#include "../base/Sample.h"
#include <array>
#include <cstddef>

namespace arstro
{
    class Shaper
    {
    public:
        static constexpr int kMaxChannels = 8;
        enum class Shape { Sine = 0, Triangle, SawDown, SawUp, Square, Pump };
        /** The note values a cycle lasts, as `setRate` numbers them: 1/1 1/2 1/4 1/8 1/16 1/32 1/4T 1/8T 1/16T 1/4D 1/8D. */
        static constexpr int kRates = 11;
        static double beatsOfRate(int rate);
        Shaper();
        void setRate(int rate) { mBeats = beatsOfRate(rate); }
        void setShape(int shape) { mShape = (Shape)(shape < 0 ? 0 : shape > 5 ? 5 : shape); }
        /** How far down the shape's floor turns the sound, 0 … 1 (1: to silence). */
        void setDepth(Sample d) { mDepth = d < 0 ? 0 : d > 1 ? 1 : d; }
        /** Where in its cycle the shape starts on the beat, in cycles 0 … 1. */
        void setPhase(Sample p) { mPhase = p; }
        /** The odd channels' cycle ahead of the even ones', 0 … 0.5 cycles (½: one up while the other is down). */
        void setStereo(Sample s) { mStereo = s < 0 ? 0 : s > 0.5 ? 0.5 : s; }
        /** The gain's glide toward its shape, ms (0: none) — a square's edges without a click. */
        void setSmoothMs(Sample ms);
        /** REQ-device-12: the song's place (see Device::Transport). Without it, the shaper counts its own samples at
         *  120 bpm from where it was made. */
        void setTransport(long long sample, double samplesPerBeat, double beatOffset);
        void process(Sample *const *io, int channels, int frames);
        void reset();
        /** (SH2) the shape's open amount at phase φ in [0, 1): 0 the floor, 1 open. */
        static double shapeAt(Shape s, double phi);
        /** The gain channel `c` used for its last sample (for tests). */
        Sample lastGain(int c) const { return (Sample)mGain[(std::size_t)(c < 0 ? 0 : c >= kMaxChannels ? kMaxChannels - 1 : c)]; }

    private:
        Shape mShape = Shape::Sine;
        double mBeats = 0.5, mDepth = 1.0, mPhase = 0.0, mStereo = 0.0, mAlpha = 1.0, mFs = 48000.0;
        long long mSample = 0;
        double mSpb = 24000.0, mOffset = 0.0;
        bool mPlaced = false;
        std::array<double, kMaxChannels> mGain{};
    };
}
