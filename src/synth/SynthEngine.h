/*
 *  Arstro DSP Library — ESP32DigitalSynth
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

namespace arstro
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

        // ---- N-way voice split (generalises the halves above) ----
        // Same contract, any shard count: shard `s` of `n` renders a disjoint set
        // of Voice objects into its own accumulator, so shards may run on
        // different cores with no shared object access. finishBlock() sums the
        // accumulators in FIXED SHARD ORDER — float addition is not associative,
        // so completion-order summation would make the output depend on thread
        // timing (REQ-compute-3). See docs/parallel-architecture.md ## 1.2.
        void renderVoiceShard(int shard, int shardCount, int frames);
        /** Tells finishBlock() how many shard accumulators to sum. Set it before
         *  rendering the shards; renderVoiceHalf() and renderBlockBytesParallel()
         *  set it for you. */
        void setVoiceShardCount(int n);
        static constexpr int kMaxVoiceShards = 8;

        // Renders one block through ComputeConfig's executor, falling back to a
        // serial pass when the block is too small to be worth splitting
        // (ParallelExecutor::shouldParallelise). Bit-identical to the serial path.
        void renderBlockBytesParallel(std::vector<uint8_t> &outBytes, int frames);

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
        static void shardTask(void *ctx, int shard); // ParallelExecutor::Task trampoline
        struct ShardJob { SynthEngine *self; int shardCount; int frames; };

        std::vector<std::vector<Sample>> mScratch;  // per-channel summed/effects buffer
        // Per-shard voice accumulators [shard][ch][frames]. Shards 0/1 are what
        // renderVoiceHalf() writes, so the older two-way API is just this with
        // shardCount = 2 and needs no separate buffers.
        std::vector<std::vector<std::vector<Sample>>> mShardAcc;
        int mActiveShards = 2; // how many of mShardAcc finishBlock() must sum

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
