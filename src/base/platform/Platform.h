/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  Platform adapter: the ONLY platform-specific code in the library.
 *
 *  The DSP core (SignalProcessor and all modules) contains no OS calls and is
 *  therefore inherently portable. Applications (native harness / ESP32 firmware)
 *  use this thin adapter to spawn the audio + comms threads uniformly.
 *
 *    - Thread : std::thread        (Linux/Windows/macOS)
 *               xTaskCreatePinnedToCore (ESP32 / ESP-IDF)
 *    - Mutex  : std::mutex / FreeRTOS mutex  — setup/non-realtime paths only,
 *               NEVER used inside process().
 *
 *  Select the backend by defining ESP_PLATFORM (ESP-IDF defines it automatically).
 */
#pragma once
#include <functional>

namespace arstro
{
    namespace platform
    {
#if defined(ESP_PLATFORM)
        // ---------------- ESP32 / FreeRTOS backend ----------------
        // Implemented in the firmware component (Platform_esp32.cpp) so the
        // library stays free of ESP-IDF headers. Declarations only here.
        class Thread
        {
        public:
            Thread() = default;
            // coreId: 0 or 1 on the dual-core S3; priority: FreeRTOS priority.
            void start(std::function<void()> fn,
                       const char *name = "dsp",
                       int coreId = 1,
                       int priority = 5,
                       int stackBytes = 8192);
            void join();
            ~Thread();
        private:
            void *mHandle = nullptr;
            std::function<void()> mFn;
        };

        class Mutex
        {
        public:
            Mutex();
            ~Mutex();
            void lock();
            void unlock();
        private:
            void *mHandle = nullptr;
        };
#else
        // ---------------- Desktop (std) backend ----------------
        // Header-only so the native harness needs no extra .cpp.
        // (std::thread / std::mutex pulled in lazily.)
    }
}
#include <thread>
#include <mutex>
namespace arstro
{
    namespace platform
    {
        class Thread
        {
        public:
            Thread() = default;
            void start(std::function<void()> fn,
                       const char * /*name*/ = "dsp",
                       int /*coreId*/ = 1,
                       int /*priority*/ = 5,
                       int /*stackBytes*/ = 0)
            {
                mThread = std::thread(std::move(fn));
            }
            void join()
            {
                if (mThread.joinable())
                    mThread.join();
            }
            ~Thread()
            {
                if (mThread.joinable())
                    mThread.join();
            }
        private:
            std::thread mThread;
        };

        class Mutex
        {
        public:
            void lock() { mMutex.lock(); }
            void unlock() { mMutex.unlock(); }
        private:
            std::mutex mMutex;
        };
#endif

        /** RAII lock for setup/non-realtime paths. */
        class ScopedLock
        {
        public:
            explicit ScopedLock(Mutex &m) : mMutex(m) { mMutex.lock(); }
            ~ScopedLock() { mMutex.unlock(); }
        private:
            Mutex &mMutex;
        };
    }
}
