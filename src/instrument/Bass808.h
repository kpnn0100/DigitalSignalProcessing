/*
 *  Arstro DSP Library — Bass808: a tuned bass drum (REQ-808-1, -2). Its pitch the note, punched down to it at each
 *  attack, gliding between held notes; a long damped decay; a click; drive; a low-pass tone. One voice (a bassline).
 *  A composition — Phasor, DecayEnvelope, Noise, Biquad::design — instrument/README.md ## Bass808 (B1)–(B6).
 *
 *  Every value moves per sample from the sample count since its attack — a render is the same however a host chops
 *  time. Deterministic (the click's noise from a fixed seed); no allocation after construction.
 */
#pragma once
#include "Instrument.h"
#include "../envelope/DecayEnvelope.h"
#include "../generator/Noise.h"
#include "../generator/Phasor.h"

namespace arstro
{
    class Bass808 : public Instrument
    {
    public:
        struct Params
        {
            double punch = 12.0;      // P: semitones above the note at the attack
            double punchTime = 40.0;  // T_p: ms to 1 % of it
            double glide = 60.0;      // T_g: ms
            double attack = 1.0, decay = 900.0, release = 60.0; // ms (decay to −60 dB; release: a note-off's choke)
            double click = 0.2;
            double drive = 0.25;
            double tone = 4000.0;     // Hz
            double volumeDb = -6.0;
            double velocity = 0.4;
            double bendRange = 2.0;
        };
        Bass808();
        void setParams(const Params &p);
        const Params &params() const { return mP; }
        /** The pitch it plays now, semitones (−1 silent) — for tests. */
        double pitch() const { return mActive ? mPitch : -1.0; }

        void noteOn(int note, int velocity) override;
        void noteOff(int note) override;
        void allNotesOff() override;
        void reset() override;
        void render(Sample *const *out, int channels, int frames) override;
        int activeVoices() const override { return mActive ? 1 : 0; }
        void pitchBend(Sample amount) override;

    private:
        void attack(int note, int velocity);
        void glideTo(int note);
        void design();
        Params mP;
        Phasor mPhase;
        DecayEnvelope mEnv;
        Noise mNoise{0x808u};
        bool mActive = false;
        long long mT = 0;                 // samples since the attack
        double mFrom = 0.0, mTo = 0.0;    // (B1) the glide, semitones
        long long mGlideLeft = 0, mGlideLen = 0;
        double mPitch = 0.0, mVel = 1.0, mBend = 0.0;
        int mStack[16] = {0};
        int mStackSize = 0;
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; // (B5) the tone's section
    };
}
