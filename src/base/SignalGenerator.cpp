#include "SignalGenerator.h"

namespace arstro
{
    SignalGenerator::SignalGenerator(int propertyCount) : SignalProcessor(propertyCount)
    {
    }

    void SignalGenerator::setFrequency(Sample hz)
    {
        mFrequency = hz;
    }

    void SignalGenerator::noteOn(Sample velocity)
    {
        mEnvelope.noteOn(velocity);
    }

    void SignalGenerator::noteOff()
    {
        mEnvelope.noteOff();
    }

    Sample SignalGenerator::process(Sample /*in*/, int channel)
    {
        Sample raw = generate(channel);
        return mEnvelope.out(raw, channel);
    }

    void SignalGenerator::addBlock(Sample *buf, int frames, int channel, Sample level)
    {
        // Hot path: non-virtual envelope tick, no per-sample getProperty.
        for (int i = 0; i < frames; ++i)
            buf[i] += level * generate(channel) * mEnvelope.nextValue(channel);
    }
}
