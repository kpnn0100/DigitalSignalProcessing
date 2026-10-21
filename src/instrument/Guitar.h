/*
 *  Arstro DSP Library — Guitar: a plucked string (REQ-guitar-1, -2).
 *
 *  The piano's modal string (physical/StringPartialBank: inharmonic partials, the highs dying first) PLUCKED instead
 *  of struck — released from rest at a pluck position, each partial ringing from its peak, the pick's release rounding
 *  the highs (README ## 14 of physical/) — eight strings at once, under one shared body of three modes
 *  (physical/StringResonator). A palm mute holds the string's damper while the note is held; a release is a finger
 *  lifting off the fret. A composition: see src/instrument/README.md ## Guitar (GT1)–(GT8).
 */
#pragma once
#include "Instrument.h"
#include "../physical/StringPartialBank.h"
#include "../physical/StringResonator.h"
#include <array>

namespace arstro
{
    class Guitar : public Instrument
    {
    public:
        static constexpr int kVoices = 8;
        static constexpr int kEndWindow = 64;    // (GT7) a voice is judged silent over this many of its own samples
        static constexpr double kSilent = 1e-5;  // −100 dB

        struct Params
        {
            double position = 0.18;  // β: where along the string it is plucked (0.5 the middle)
            double pick = 4000.0;    // f_p, Hz: the pick's release corner — brighter higher
            double sustain = 5.0;    // s: T60 of the fundamental at E2, (GT2)
            double brightness = 0.5; // s: T60 at 5 kHz
            double stiffness = 0.5;  // B in units of 1e−4
            double body = 0.3;       // 0 an electric's bare string … 1 an acoustic's body
            double mute = 0.0;       // the palm on the strings while held, 0 … 1
            double release = 80.0;   // ms: a finger lifting off the fret
            double volumeDb = -6.0;
            double velocity = 0.6;
            double bendRange = 2.0;
        };

        Guitar();
        void setParams(const Params &p);
        const Params &params() const { return mP; }

        void noteOn(int note, int velocity) override;
        void noteOff(int note) override;
        void allNotesOff() override;
        void reset() override;
        void render(Sample *const *out, int channels, int frames) override;
        int activeVoices() const override;
        void pitchBend(Sample amount) override;

        /** (GT2) the fundamental's T60 at a pitch, for tests. */
        double sustainAt(double hz) const;

    private:
        struct Voice
        {
            StringPartialBank string;
            int note = -1;
            bool on = false;      // sounding
            bool held = false;    // the key down
            long long age = 0;    // its pluck order, for stealing
            long long t = 0;      // its own samples since the pluck — (GT7)'s clock
            double peak = 0.0;    // over the current end window
        };
        void tune(Voice &v);
        void endVoice(Voice &v);

        Params mP;
        std::array<Voice, kVoices> mVoices;
        std::array<StringResonator, 3> mBody;
        long long mPlucks = 0;
        double mBend = 0.0, mGain = 0.5;
        bool mBodyOn = false;     // the body still ringing
        double mBodyPeak = 0.0;
        long long mBodyT = 0;
    };
}
