#include "AudioConfig.h"
#include "SignalProcessor.h"

namespace arstro
{
    AudioConfig &AudioConfig::instance()
    {
        static AudioConfig sInstance;
        return sInstance;
    }

    void AudioConfig::setSampleRate(Sample sampleRate)
    {
        mSampleRate = sampleRate;
        // Mirror into SignalProcessor and notify every live processor.
        SignalProcessor::setSampleRate(sampleRate);
    }

    void AudioConfig::setBufferSize(int bufferSize)
    {
        mBufferSize = bufferSize;
        SignalProcessor::setBufferSize(bufferSize);
    }

    void AudioConfig::setChannelCount(int channelCount)
    {
        if (channelCount < 1)
            channelCount = 1;
        mChannelCount = channelCount;
        // Per-channel state is (re)sized by processors on this notification.
        SignalProcessor::notifyChannelCountChanged();
    }

    void AudioConfig::setOutputBitDepth(int bits)
    {
        mOutputBitDepth = bits;
    }
}
