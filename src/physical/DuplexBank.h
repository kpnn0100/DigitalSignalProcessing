/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  DuplexBank: the short, un-struck string segments beyond the bridge (front/rear
 *  duplex, the "aliquot scale"). Tuned on the treble to coincide with an upper
 *  partial of the speaking string, they are never struck — the speaking string
 *  drives them SYMPATHETICALLY through the shared bridge, and they ring back a
 *  high, sustained shimmer. A treble effect; switched off in the bass.
 *
 *  Runs mono (REQ-piano-13) — driven by PianoVoice on channel 0 only. Feed-forward
 *  only: read-out of the string, never fed back into it (no loop). See
 *  src/physical/README.md ## 8.1.
 */
#pragma once
#include "../base/SignalProcessor.h"
#include "StringResonator.h"
#include <array>

namespace arstro
{
    class DuplexBank : public SignalProcessor
    {
    public:
        // README ## 8.1: a handful of aliquot segments per voice. Tuned to upper
        // partials n*f0 in the brilliance band; this bank exists per VOICE, so the
        // count is a realism knob, not a cost driver.
        static constexpr int kMaxStrings = 3;

        enum PropertyIndex
        {
            fundamentalID,   // Hz, the speaking-string f0 — the aliquot tuning derives from it
            driveGainID,     // kappa_dup: string motion -> duplex drive scale; 0 disables (## 8.1)
            propertyCount
        };
        DuplexBank();

        void setFundamentalHz(Sample hz);
        void setDriveGain(Sample kappa);

        /** README ## 8.1: treble weighting — ~0 in the bass, 1 by C5. Public and
         *  static so tests assert the law rather than infer it. */
        static Sample registerGainFor(Sample f0Hz);
        /** README ## 8.1: the aliquot frequency of segment `index` = n_index * f0. */
        static Sample aliquotHz(Sample f0Hz, int index);

        /** True when this note is in the treble and the bank runs at all (## 8.1) —
         *  below the crossover no resonators run, so the bass pays nothing. */
        bool isActive() const { return mActive; }
        int activeStringCount() const { return mActive ? mStringCount : 0; }

        // Zeroes every segment's history.
        void reset();

        void update() override;

    protected:
        /** `in` is the speaking string's transverse motion (PianoVoice's stringSum,
         *  README ## 5) — the drive the string puts across the bridge. Returns the
         *  radiated duplex shimmer; NOT fed back into the string. */
        Sample process(Sample stringMotion, int channel) override;

    private:
        std::array<StringResonator, kMaxStrings> mSegments;
        std::array<Sample, kMaxStrings> mSegWeight{};
        int mStringCount = 0;
        bool mActive = false;
        Sample mRegisterGain = 0.0;

        // ## 8.1: aliquot harmonics — upper partials that land in the ~2–8 kHz
        // brilliance band. Higher ones drop out near Nyquist for the top notes.
        static constexpr int kAliquotHarmonic[kMaxStrings] = {4, 6, 8};

        // ## 8.1 treble weighting. g_dup = clamp((f0/f_ref)^1.5, 0, 1); below the gain
        // floor the bank is switched off entirely (no resonators run).
        static constexpr Sample kDuplexRefHz = 523.25; // C5 — full strength here and up
        static constexpr Sample kRegisterExponent = 1.5;
        static constexpr Sample kMinAudibleGain = 0.28; // ~off below A3

        // ## 8.1: the segment is undamped, so it rings well past the bridge-damped
        // speaking partial — that sustain is what reads as shimmer rather than a
        // louder attack.
        static constexpr Sample kSegmentT60 = 1.2;

        // Highest segment kept, as a fraction of the sample rate (mirrors ## 2).
        static constexpr Sample kNyquistFraction = 0.45;
    };
}
