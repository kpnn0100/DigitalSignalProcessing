/*
 *  Gyrus Space DSP Library — ESP32DigitalSynth
 *
 *  Voice: one playing note = 3 Oscillators (each owns its ADSR envelope).
 *  No separate envelope stage — noteOn/noteOff just trigger the oscillators.
 */
#pragma once
#include "../generator/Oscillator.h"

namespace gyrus_space
{
    class Voice
    {
    public:
        static constexpr int kOscCount = 2; // oscillators per voice (was 3)

        Voice();

        void noteOn(int midiNote, Sample velocity);
        void noteOff();
        bool isActive() const;
        int note() const { return mNote; }
        long age() const { return mAge; }
        void tick() { ++mAge; } // sample-count age for voice stealing
        void advanceAge(long n) { mAge += n; }

        // Sum of the 3 (already-enveloped) oscillators for one channel.
        Sample render(int channel);
        // Block path: add this voice's output to buf for one channel.
        void renderBlock(Sample *buf, int frames, int channel);

        Oscillator &osc(int i) { return mOsc[i]; }
        void setOscLevel(int i, Sample level) { mOscLevel[i] = level; }
        void setOscTuneSemitones(int i, Sample semis) { mOscTune[i] = semis; }

    private:
        Sample midiToHz(int note) const;
        Oscillator mOsc[kOscCount];
        Sample mOscLevel[kOscCount];
        Sample mOscTune[kOscCount]; // semitone offset per oscillator
        int mNote = -1;
        long mAge = 0;
    };
}
