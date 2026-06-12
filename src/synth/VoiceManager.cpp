#include "VoiceManager.h"

namespace arstro
{
    int VoiceManager::allocateVoice()
    {
        // Prefer a free (finished) voice.
        for (int i = 0; i < kVoiceCount; ++i)
            if (!mVoices[i].isActive())
                return i;
        // Otherwise steal the oldest.
        int oldest = 0;
        long maxAge = -1;
        for (int i = 0; i < kVoiceCount; ++i)
        {
            if (mVoices[i].age() > maxAge)
            {
                maxAge = mVoices[i].age();
                oldest = i;
            }
        }
        return oldest;
    }

    void VoiceManager::noteOn(int midiNote, Sample velocity)
    {
        int idx = allocateVoice();
        mVoices[idx].noteOn(midiNote, velocity);
    }

    void VoiceManager::noteOff(int midiNote)
    {
        for (int i = 0; i < kVoiceCount; ++i)
            if (mVoices[i].isActive() && mVoices[i].note() == midiNote)
                mVoices[i].noteOff();
    }

    Sample VoiceManager::render(int channel)
    {
        Sample sum = 0.0;
        for (int i = 0; i < kVoiceCount; ++i)
            if (mVoices[i].isActive())
                sum += mVoices[i].render(channel);
        return sum;
    }

    void VoiceManager::renderBlock(Sample *buf, int frames, int channel)
    {
        renderBlockRange(buf, frames, channel, 0, kVoiceCount);
    }

    void VoiceManager::renderBlockRange(Sample *buf, int frames, int channel, int v0, int v1)
    {
        for (int i = v0; i < v1; ++i)
            if (mVoices[i].isActive())
                mVoices[i].renderBlock(buf, frames, channel);
    }

    void VoiceManager::tick()
    {
        for (int i = 0; i < kVoiceCount; ++i)
            if (mVoices[i].isActive())
                mVoices[i].tick();
    }

    void VoiceManager::advance(int frames)
    {
        for (int i = 0; i < kVoiceCount; ++i)
            if (mVoices[i].isActive())
                mVoices[i].advanceAge(frames);
    }

    int VoiceManager::activeVoices() const
    {
        int n = 0;
        for (int i = 0; i < kVoiceCount; ++i)
            if (mVoices[i].isActive())
                ++n;
        return n;
    }
}
