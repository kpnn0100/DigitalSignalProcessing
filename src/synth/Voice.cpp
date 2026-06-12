#include "Voice.h"
#include <cmath>

namespace arstro
{
    Voice::Voice()
    {
        for (int i = 0; i < kOscCount; ++i)
        {
            mOscLevel[i] = 1.0 / kOscCount;
            mOscTune[i] = 0.0;
        }
    }

    Sample Voice::midiToHz(int note) const
    {
        return 440.0 * std::pow(2.0, (note - 69) / 12.0);
    }

    void Voice::noteOn(int midiNote, Sample velocity)
    {
        mNote = midiNote;
        mAge = 0;
        Sample baseHz = midiToHz(midiNote);
        for (int i = 0; i < kOscCount; ++i)
        {
            mOsc[i].setFrequency(baseHz * std::pow(2.0, mOscTune[i] / 12.0));
            mOsc[i].noteOn(velocity);
        }
    }

    void Voice::noteOff()
    {
        for (int i = 0; i < kOscCount; ++i)
            mOsc[i].noteOff();
    }

    bool Voice::isActive() const
    {
        for (int i = 0; i < kOscCount; ++i)
            if (!mOsc[i].isFinished())
                return true;
        return false;
    }

    Sample Voice::render(int channel)
    {
        Sample sum = 0.0;
        for (int i = 0; i < kOscCount; ++i)
            sum += mOsc[i].out(0.0, channel) * mOscLevel[i];
        return sum;
    }

    void Voice::renderBlock(Sample *buf, int frames, int channel)
    {
        for (int i = 0; i < kOscCount; ++i)
            mOsc[i].addBlock(buf, frames, channel, mOscLevel[i]);
    }
}
