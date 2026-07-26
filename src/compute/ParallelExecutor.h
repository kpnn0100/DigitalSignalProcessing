/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  ParallelExecutor: the block-grain work scheduler engines render through.
 *
 *  This is NOT the porting seam. `base/platform/Platform.h` is, and it already
 *  was — a new platform implements platform::Thread (and Mutex) and gets the
 *  thread-pool executor below for free. A platform with no threads at all needs
 *  nothing: SerialExecutor is the default and is always available.
 *  See docs/parallel-architecture.md § 3 (REQ-compute-2).
 *
 *  Contract, per REQ-compute-4: run() allocates nothing, takes no mutex, and does
 *  not block unboundedly. That is why the task is a plain function pointer plus a
 *  void* context rather than a std::function, which heap-allocates as soon as it
 *  captures more than a word or two — and nothing on the audio thread may
 *  allocate (docs/design.md, "audio thread never locks").
 *
 *  Contract, per REQ-compute-3: shards must touch DISJOINT state and the caller
 *  must combine their results in fixed shard order. Floating-point addition is not
 *  associative, so summing in completion order would make the rendered audio
 *  depend on thread timing.
 */
#pragma once

namespace arstro
{
    class ParallelExecutor
    {
    public:
        /** One unit of work. `shard` is in [0, shards); `ctx` is the caller's
         *  context, cast back inside the body. Must not throw. */
        using Task = void (*)(void *ctx, int shard);

        virtual ~ParallelExecutor() = default;

        /** Runs `task` for shard = 0..shards-1 and returns only when all are done.
         *  The calling thread participates, so `shards == 1` never touches a
         *  worker and costs nothing beyond the call. */
        virtual void run(Task task, void *ctx, int shards) = 0;

        /** How many shards this executor can actually run concurrently. 1 means
         *  fully serial. Engines may use this to choose their shard count; there
         *  is no point splitting work more finely than this
         *  (docs/parallel-architecture.md § 2 — Amdahl gives nothing past it). */
        virtual int concurrency() const = 0;

        /** Below a block-size floor, fan-out costs more than it saves
         *  (§ 2.1: ~1-2 % overhead at 128 frames, ~10 % at 16). One decision in
         *  one place instead of a heuristic scattered over the engines. */
        bool shouldParallelise(int frames) const
        {
            return concurrency() > 1 && frames >= kMinFramesForParallel;
        }

        /** Measured on this repo's graph: a 128-frame block is ~0.5 ms of work
         *  against a ~2-10 us wake-and-join, i.e. 1-2 % overhead. At 16 frames the
         *  same overhead is ~10 % and parallelism starts losing. */
        static constexpr int kMinFramesForParallel = 64;
    };

    /** The default, and the reference for REQ-compute-3's bit-identity test.
     *  Always available — a platform with no threading support uses this and is
     *  fully functional. */
    class SerialExecutor : public ParallelExecutor
    {
    public:
        void run(Task task, void *ctx, int shards) override
        {
            for (int i = 0; i < shards; ++i)
                task(ctx, i);
        }
        int concurrency() const override { return 1; }
    };
}
