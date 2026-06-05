/*
 *  Gyrus Space DSP Library — ESP32DigitalSynth
 *
 *  LockFreeQueue: single-producer / single-consumer wait-free ring buffer.
 *
 *  Used to hand control events (parameter changes, note on/off) from the comms
 *  thread (producer) to the audio thread (consumer) without locking the audio
 *  thread. Portable: relies only on std::atomic, which is available on Xtensa
 *  (ESP32), x86 and x64. No OS calls — see base/README.md (Concurrency model).
 */
#pragma once
#include <atomic>
#include <vector>
#include <cstddef>

namespace gyrus_space
{
    /**
     * @brief Wait-free SPSC ring buffer.
     *
     * One thread calls push(), one thread calls pop(). Capacity is fixed at
     * construction (rounded up to a power of two). When full, push() returns
     * false rather than blocking — control events are dropped only under extreme
     * backpressure, never the audio thread.
     */
    template <typename T>
    class LockFreeQueue
    {
    public:
        explicit LockFreeQueue(size_t minCapacity = 256)
        {
            size_t cap = 1;
            while (cap < minCapacity)
                cap <<= 1;
            mCapacity = cap;
            mMask = cap - 1;
            mBuffer.resize(cap);
            mHead.store(0, std::memory_order_relaxed);
            mTail.store(0, std::memory_order_relaxed);
        }

        /** Producer side. Returns false if the queue is full. */
        bool push(const T &item)
        {
            const size_t head = mHead.load(std::memory_order_relaxed);
            const size_t next = (head + 1) & mMask;
            if (next == mTail.load(std::memory_order_acquire))
                return false; // full
            mBuffer[head] = item;
            mHead.store(next, std::memory_order_release);
            return true;
        }

        /** Consumer side. Returns false if the queue is empty. */
        bool pop(T &out)
        {
            const size_t tail = mTail.load(std::memory_order_relaxed);
            if (tail == mHead.load(std::memory_order_acquire))
                return false; // empty
            out = mBuffer[tail];
            mTail.store((tail + 1) & mMask, std::memory_order_release);
            return true;
        }

        bool empty() const
        {
            return mHead.load(std::memory_order_acquire) ==
                   mTail.load(std::memory_order_acquire);
        }

        size_t capacity() const { return mCapacity; }

    private:
        std::vector<T> mBuffer;
        size_t mCapacity = 0;
        size_t mMask = 0;
        std::atomic<size_t> mHead{0}; // written by producer
        std::atomic<size_t> mTail{0}; // written by consumer
    };
}
