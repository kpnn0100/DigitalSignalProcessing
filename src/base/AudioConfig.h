/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  AudioConfig: single source of truth for global audio settings
 *  (sample rate, buffer size, channel count, output bit depth).
 *
 *  Platform-independent: contains no OS calls. Runtime changes should be made
 *  during setup (before the audio thread starts) or routed through the engine's
 *  lock-free command queue — see base/LockFreeQueue.h and base/README.md.
 */
#pragma once
#include "Sample.h"

namespace arstro
{
    /**
     * @brief Global audio configuration (Meyers singleton).
     *
     * This is the authoritative API for the global sample rate / buffer size /
     * channel count / output bit depth. SignalProcessor mirrors sampleRate and
     * bufferSize into its statics so existing modules keep compiling, but every
     * change goes through here so there is exactly one front door.
     */
    class AudioConfig
    {
    public:
        static AudioConfig &instance();

        Sample sampleRate() const { return mSampleRate; }
        int    bufferSize() const { return mBufferSize; }
        int    channelCount() const { return mChannelCount; }
        int    outputBitDepth() const { return mOutputBitDepth; }

        // Setup-time configuration. Notifies all live SignalProcessors.
        void setSampleRate(Sample sampleRate);
        void setBufferSize(int bufferSize);
        void setChannelCount(int channelCount);
        void setOutputBitDepth(int bits);

    private:
        AudioConfig() = default;
        AudioConfig(const AudioConfig &) = delete;
        AudioConfig &operator=(const AudioConfig &) = delete;

        Sample mSampleRate = 48000.0;
        int    mBufferSize = 128;
        int    mChannelCount = 2;
        int    mOutputBitDepth = 16;
    };
}
