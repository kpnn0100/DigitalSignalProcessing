/*
 *  Gyrus Space DSP Library — ESP32DigitalSynth
 *
 *  SynthEngine: top-level instrument. 8-note polyphony -> shared effects chain
 *  (compressor -> overdrive -> chorus -> repeater -> reverb) -> output.
 *
 *  Thread-safety: control events (param/note) are pushed from the comms thread
 *  via a lock-free queue and drained on the audio thread at each block boundary,
 *  so renderBlock never locks. For single-threaded tests, the apply* methods can
 *  be called directly. Build the graph before starting the audio thread.
 */
#pragma once
#include "VoiceManager.h"
#include "../base/Block.h"
#include "../base/Vec.h"
#include "../base/LockFreeQueue.h"
#include "../effects/Compressor.h"
#include "../effects/Overdrive.h"
#include "../effects/Chorus.h"
#include "../effects/Repeater.h"
#include "../reverb/Reverb.h"
#include "../output/OutputBlock.h"
#include <vector>
#include <cstdint>

namespace gyrus_space
{
    class SynthEngine
    {
    public:
        SynthEngine();

        // ---- thread-safe producers (comms thread) ----
        void pushParam(uint16_t id, Sample value);
        void pushNoteOn(int midiNote, Sample velocity);
        void pushNoteOff(int midiNote);

        // ---- direct apply (single-threaded use / tests) ----
        void applyParam(uint16_t id, Sample value);
        void noteOn(int midiNote, Sample velocity);
        void noteOff(int midiNote);

        // ---- audio thread (single-threaded convenience) ----
        // Renders `frames` interleaved Sample frames (channelCount per frame).
        void renderBlockDouble(std::vector<Sample> &outInterleaved, int frames);
        // Renders `frames` and packs to bytes at the configured output bit depth.
        void renderBlockBytes(std::vector<uint8_t> &outBytes, int frames);

        // ---- multi-core block API (voice-split) ----
        // The voice pool is split into halves; each half touches a DISJOINT set of
        // Voice objects (so no C++ object is accessed by two cores at once). The
        // shared effects run single-core in finishBlock(). Usage:
        //   beginBlock(frames);                         // drain control, size buffers
        //   <parallel> renderVoiceHalf(0,frames) || renderVoiceHalf(1,frames);
        //   finishBlock(frames);   // sum halves -> effects -> master (one core)
        //   packBytes(out, frames);
        void beginBlock(int frames);
        void renderVoiceHalf(int half, int frames); // half 0/1; parallel-safe
        void finishBlock(int frames);               // sum + effects + master (serial)
        void packBytes(std::vector<uint8_t> &outBytes, int frames);
        int channels() const;

        int activeVoices() { return mVoices.activeVoices(); }

    private:
        struct Command
        {
            enum Type : uint8_t { Param, NoteOn, NoteOff } type = Param;
            uint16_t id = 0;
            Sample value = 0.0;
            int note = 0;
        };
        void drainCommands();
        Sample renderSample(int channel);
        std::vector<std::vector<Sample>> mScratch;  // per-channel summed/effects buffer
        std::vector<std::vector<Sample>> mScratchA; // voice-half 0 accumulator [ch][frames]
        std::vector<std::vector<Sample>> mScratchB; // voice-half 1 accumulator [ch][frames]

        VoiceManager mVoices;
        Block mEffects; // serial: comp -> od -> chorus -> repeater -> reverb
        Compressor mCompressor;
        Overdrive mOverdrive;
        Chorus mChorus;
        Repeater mRepeater;
        Reverb mReverb;
        OutputBlock mOutput;
        Sample mMasterLevel = 0.8;

        LockFreeQueue<Command> mQueue{1024};
    };
}
