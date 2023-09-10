/*
  ==============================================================================

    MultiThreader.cpp
    Created: 9 Sep 2023 7:14:54pm
    Author:  PC

  ==============================================================================
*/

#include "MultiThreader.h"
void MultiThreader::start() {
    mIsJobDone = 0;
    const uint32_t num_threads = std::thread::hardware_concurrency(); // Max # of threads the system supports
    for (uint32_t ii = 0; ii < num_threads; ++ii) {
        threads.emplace_back(std::thread(&MultiThreader::ThreadLoop, this));
    }
}
void MultiThreader::ThreadLoop() {
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            mutex_condition.wait(lock, [this] {
                return !jobs.empty() || mNeedToTerminate;
                });
            if (mNeedToTerminate) {
                return;
            }
            job = jobs.front();
            jobs.pop();
            job();
            mIsJobDone--;
        }
    }
}
void MultiThreader::queueJob(std::function<void()> job) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        mIsJobDone++;
        jobs.push(job);
    }
    mutex_condition.notify_one();
}
bool MultiThreader::isBusy() {
    bool poolbusy;
    {
        poolbusy = !jobs.empty() || !(mIsJobDone==0);
    }
    return poolbusy;
}
void MultiThreader::stop() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        mNeedToTerminate = true;
    }
    mutex_condition.notify_all();
    for (std::thread& active_thread : threads) {
        active_thread.join();
    }
    threads.clear();
}