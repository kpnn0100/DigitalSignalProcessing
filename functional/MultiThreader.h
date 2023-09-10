/*
  ==============================================================================

    MultiThreader.h
    Created: 9 Sep 2023 7:14:54pm
    Author:  PC

  ==============================================================================
*/

#pragma once
#include <vector>
#include <thread>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <queue>
using namespace std;

class MultiThreader
{
private:
    void ThreadLoop();
    int mIsJobDone;
    bool mNeedToTerminate = false;           // Tells threads to stop looking for jobs
    std::mutex queue_mutex;                  // Prevents data races to the job queue
    std::condition_variable mutex_condition; // Allows threads to wait on new jobs or termination 
    std::vector<std::thread> threads;
    std::queue<std::function<void()>> jobs;
public:
    void start();
    void queueJob(std::function<void()> job);
    void stop();
    bool isBusy();
};
