#include "ThreadPoolExecutor.h"
#include <thread>

namespace arstro
{
    ThreadPoolExecutor::ThreadPoolExecutor(int workers)
    {
        if (workers < 0) // kAutoWorkers
        {
            // Leave one hardware thread for the caller, which participates in
            // every job (see concurrency()).
            unsigned hw = std::thread::hardware_concurrency();
            workers = (hw > 1) ? (int)hw - 1 : 0;
        }
        if (workers > kMaxWorkers) workers = kMaxWorkers;

        mWorkerCount = workers;
        mThreads.reserve((size_t)workers);
        for (int i = 0; i < workers; ++i)
        {
            mThreads.emplace_back(new platform::Thread());
            mThreads.back()->start([this] { workerLoop(); }, "dsp-worker");
        }
    }

    ThreadPoolExecutor::~ThreadPoolExecutor()
    {
        {
            std::unique_lock<std::mutex> lk(mMutex);
            mStop = true;
            ++mGeneration; // wake every worker exactly as a job would
        }
        mWorkCv.notify_all();
        for (auto &t : mThreads)
            t->join();
    }

    int ThreadPoolExecutor::drainShards(const Job &job)
    {
        // Claiming is lock-free and self-balancing: a drainer that finishes early
        // takes the next shard instead of idling. `job` is a snapshot taken under
        // the mutex, so no drainer can observe a half-published job.
        int done = 0;
        for (;;)
        {
            const int shard = mNextShard.fetch_add(1, std::memory_order_relaxed);
            if (shard >= job.shards)
                return done;
            job.task(job.ctx, shard);
            ++done;
        }
    }

    void ThreadPoolExecutor::retire(int done)
    {
        bool finished;
        {
            std::unique_lock<std::mutex> lk(mMutex);
            mRemaining -= done;
            --mDrainersActive;
            finished = (mRemaining <= 0 && mDrainersActive == 0);
        }
        if (finished)
            mDoneCv.notify_all();
    }

    void ThreadPoolExecutor::workerLoop()
    {
        uint32_t seen = 0;
        for (;;)
        {
            Job job;
            {
                std::unique_lock<std::mutex> lk(mMutex);
                mWorkCv.wait(lk, [&] { return mStop || mGeneration != seen; });
                if (mStop)
                    return;
                seen = mGeneration;
                job = mJob; // snapshot under the lock
                // NOT ++mDrainersActive: run() already counted this worker in.
                // See the comment there — counting on arrival is what made an
                // earlier version deadlock.
            }
            retire(drainShards(job));
        }
    }

    void ThreadPoolExecutor::run(Task task, void *ctx, int shards)
    {
        if (shards <= 0)
            return;
        // One shard, or a pool with no workers: run inline. No wake-up at all —
        // the degenerate case must cost nothing (REQ-compute-5).
        if (shards == 1 || mWorkerCount == 0)
        {
            for (int i = 0; i < shards; ++i)
                task(ctx, i);
            return;
        }

        Job job;
        {
            std::unique_lock<std::mutex> lk(mMutex);
            mJob.task = task;
            mJob.ctx = ctx;
            mJob.shards = shards;
            job = mJob;
            mNextShard.store(0, std::memory_order_relaxed);
            mRemaining = shards;
            // Count every drainer this job INVITES — the caller plus every worker,
            // all of which notify_all() wakes — rather than counting them as they
            // arrive. Counting on arrival looks equivalent and is not: a worker
            // that had not yet woken was not counted, so run() could see
            // mDrainersActive == 0 and return while that worker was still about to
            // drain. It would then claim against the NEXT job's cursor using its
            // stale snapshot, and its final failed claim would still advance the
            // cursor — skipping one shard of the new job, which therefore never
            // ran and never decremented mRemaining. run() then waited forever.
            //
            // Counting invitations is safe because the previous run() cannot
            // return until every worker has retired, so at this instant all of
            // them are parked on mWorkCv (or about to re-check its predicate, which
            // is already true). Each wakes exactly once per generation and retires
            // exactly once.
            mDrainersActive = 1 + mWorkerCount;
            ++mGeneration;
        }
        mWorkCv.notify_all();

        // The caller is a drainer as well — otherwise one core would sit idle
        // waiting, and a 2-shard job on a 1-worker pool would gain nothing.
        const int done = drainShards(job);

        {
            std::unique_lock<std::mutex> lk(mMutex);
            mRemaining -= done;
            --mDrainersActive;
            // Wait for the shards AND for every drainer to have left. The second
            // condition is what makes back-to-back jobs safe: without it a
            // straggler would still be claiming from mNextShard when the next
            // run() resets it, consuming the next job's shard indices while
            // executing this job's task.
            mDoneCv.wait(lk, [&] { return mRemaining <= 0 && mDrainersActive == 0; });
        }
    }
}
