/*
 *  Arstro DSP — piano_demo
 *
 *  PianoEngine: fixed-size polyphony pool of PianoVoice sharing one PianoBridge,
 *  with MIDI note on/off and the three pedals (REQ-piano-8, REQ-piano-10). Mirrors
 *  src/synth/VoiceManager's fixed-array + oldest-steals-first allocation pattern,
 *  but VoiceManager is hardcoded to Oscillator-owning Voice objects and has no
 *  concept of a shared cross-voice coupling bus (see src/physical/README.md "why
 *  new classes") — a piano-specific engine composes PianoVoice + PianoBridge
 *  directly rather than editing VoiceManager for a shape it isn't built for.
 */
#pragma once
#include "../../src/synth_dsp.h"
#include <array>
#include <cstdint>
#include <vector>

namespace arstro
{
    class PianoEngine
    {
    public:
        static constexpr int kVoiceCount = 8; // matches VoiceManager's existing convention

        PianoEngine();

        void noteOnMidi(int midiNote, Sample velocity);
        void noteOff(int midiNote);
        void panic(); // all notes off, pedals cleared

        void setSustainPedal(bool on);
        void setSostenutoPedal(bool on);
        void setUnaCorda(bool on);

        // Renders `frames` stereo samples as interleaved 16-bit PCM, appended to `out`.
        void renderBlockBytes(std::vector<uint8_t> &out, int frames);

        // Renders `frames` stereo samples straight into a caller-owned interleaved
        // float buffer. Preferred on a live audio thread: it allocates nothing, and
        // it skips the round trip through 16-bit PCM that renderBlockBytes() does
        // (a live float sink would only have to convert straight back, losing
        // resolution on the way through for no reason).
        void renderBlockFloat(float *interleaved, int frames);

    private:
        void renderFrame(Sample &outL, Sample &outR); // one sample, both paths
        int allocateVoice();
        void refreshDamperHeld(int i);
        static Sample midiToHz(int note);

        std::array<PianoVoice, kVoiceCount> mVoices;
        PianoBridge mBridge;

        std::array<int, kVoiceCount> mNote;         // last assigned MIDI note, -1 if never used
        std::array<bool, kVoiceCount> mKeyDown;
        std::array<bool, kVoiceCount> mSostenutoLatched;
        std::array<long, kVoiceCount> mAge;

        bool mSustainPedal = false;
        bool mSostenutoPedal = false;
        bool mUnaCorda = false;
    };
}
