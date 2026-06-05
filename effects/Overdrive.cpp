#include "Overdrive.h"
#include "../base/AudioConfig.h"
#include <cmath>

namespace gyrus_space
{
    Overdrive::Overdrive() : SignalProcessor(propertyCount)
    {
        setSmoothEnable(false);
        initProperty(driveID, 2.0);
        initProperty(toneID, 5000.0);
        initProperty(levelID, 1.0);
        ensureChannels();
        update();
    }

    void Overdrive::ensureChannels()
    {
        int n = AudioConfig::instance().channelCount();
        if ((int)mTone.size() != n)
        {
            mTone.clear();
            mTone.resize(n);
            for (auto &f : mTone)
            {
                f.setSmoothEnable(false);
                f.setCutoffFrequency(getProperty(toneID));
            }
        }
    }
    void Overdrive::onChannelCountChanged() { ensureChannels(); }

    void Overdrive::setDrive(Sample drive) { setProperty(driveID, drive); }
    void Overdrive::setToneHz(Sample hz) { setProperty(toneID, hz); }
    void Overdrive::setLevel(Sample level) { setProperty(levelID, level); }

    void Overdrive::update()
    {
        for (auto &f : mTone)
            f.setCutoffFrequency(getProperty(toneID));
    }

    Sample Overdrive::process(Sample in, int channel)
    {
        if (channel < 0 || channel >= (int)mTone.size())
            return in;
        Sample drive = getProperty(driveID);
        Sample shaped = std::tanh(drive * in);
        Sample toned = mTone[channel].out(shaped, 0);
        return toned * getProperty(levelID);
    }
}
