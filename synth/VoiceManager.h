/*
 *  Gyrus Space DSP Library — ESP32DigitalSynth
 *
 *  VoiceManager: 8-note polyphony. Allocates a free voice on note-on, else steals
 *  the oldest. Voice age is tracked in SAMPLE COUNTS (tick once per frame).
 */
#pragma once
#include "Voice.h"

namespace gyrus_space
{
    class VoiceManager
    {
    public:
        static constexpr int kVoiceCount = 8;

        void noteOn(int midiNote, Sample velocity);
        void noteOff(int midiNote);

        Sample render(int channel); // sum of active voices for one channel
        void renderBlock(Sample *buf, int frames, int channel); // block path (all voices)
        // Block path over a voice subset [v0,v1) — used for multi-core voice-split.
        void renderBlockRange(Sample *buf, int frames, int channel, int v0, int v1);
        void tick();                // advance all voices' sample-count age
        void advance(int frames);   // advance age by a whole block

        Voice &voice(int i) { return mVoices[i]; }
        int voiceCount() const { return kVoiceCount; }
        int activeVoices() const;

    private:
        int allocateVoice();
        Voice mVoices[kVoiceCount];
    };
}
