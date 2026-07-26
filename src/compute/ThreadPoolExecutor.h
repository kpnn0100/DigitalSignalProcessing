/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  ThreadPoolExecutor: persistent workers, lock-free shard claiming, no
 *  allocation in run(). Built entirely on platform::Thread, so it works on every platform that
 *  has implemented the porting seam and needs no per-platform code of its own
 *  (REQ-compute-2). See docs/parallel-architecture.md ## 3 and src/compute/README.md.
 *
 *  Dispatch/join uses a mutex + condition_variable; shard CLAIMING — the hot path
 *  — is lock-free. The mutex is held only to publish a job and to account for a
 *  finished one, never while DSP work runs, so no thread can be descheduled while
 *  holding it. REQ-compute-4 was amended to permit this: an all-atomic handshake
 *  was tried first and deadlocked (a worker waking late for job N would claim
 *  against job N+1's cursor and silently skip one of its shards). Idle workers
 *  block on the condition variable rather than spinning, so an idle pool costs
 *  nothing.
 */
#pragma once
#include "ParallelExecutor.h"
#include "../base/platform/Platform.h"
#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <cstdint>

namespace arstro
{
    class ThreadPoolExecutor : public ParallelExecutor
    {
    public:
        /** `workers` counts threads IN ADDITION to the caller, which always
         *  participates. kAutoWorkers (the default) sizes the pool to the
         *  hardware, capped by kMaxWorkers; an explicit 0 means "no worker
         *  threads", i.e. run() executes inline — a real configuration on a
         *  single-core target, not just a test case. Threads are created here,
         *  never in run(). */
        explicit ThreadPoolExecutor(int workers = kAutoWorkers);
        ~ThreadPoolExecutor() override;

        ThreadPoolExecutor(const ThreadPoolExecutor &) = delete;
        ThreadPoolExecutor &operator=(const ThreadPoolExecutor &) = delete;

        void run(Task task, void *ctx, int shards) override;
        int concurrency() const override { return mWorkerCount + 1; }

        /** Number of worker threads spawned (excludes the calling thread). */
        int workerCount() const { return mWorkerCount; }

        static constexpr int kAutoWorkers = -1;
        static constexpr int kMaxWorkers = 31;

    private:
        void workerLoop();

        struct Job
        {
            Task task = nullptr;
            void *ctx = nullptr;
            int shards = 0;
        };

        /** Claim and run shards until none are left; returns how many this drainer
         *  ran. Lock-free and self-balancing: a drainer that finishes early takes
         *  the next shard instead of idling. `job` is a snapshot taken under the
         *  mutex so no drainer can observe a half-published job. */
        int drainShards(const Job &job);
        /** Account for `done` shards and leave the drain phase, waking run() if
         *  this was the last drainer out. */
        void retire(int done);

        // Dispatch/join handshake. The mutex is held only to publish a job and to
        // account for a finished one — never while any DSP work runs, and never by
        // a thread that could be descheduled mid-computation. Shard CLAIMING (the
        // hot path) stays lock-free via mNextShard. See REQ-compute-4, which was
        // amended to permit exactly this after a hand-rolled all-atomic handshake
        // proved subtly racy across back-to-back jobs.
        std::mutex mMutex;
        std::condition_variable mWorkCv; // caller -> workers: a job is ready
        std::condition_variable mDoneCv; // last drainer -> caller: job complete
        Job mJob;                        // guarded by mMutex
        uint32_t mGeneration = 0;        // guarded by mMutex; bumped once per run()
        int mRemaining = 0;              // guarded by mMutex; shards not yet finished
        int mDrainersActive = 0;         // guarded by mMutex
        bool mStop = false;              // guarded by mMutex
        std::atomic<int> mNextShard{0};  // lock-free claim cursor

        // Held by pointer because platform::Thread owns a std::thread and declares
        // a destructor, which suppresses its move constructor — so it cannot live
        // directly in a resizable vector. Allocated once at construction, never
        // from the audio thread.
        std::vector<std::unique_ptr<platform::Thread>> mThreads;
        int mWorkerCount = 0;
    };
}
