#include "Compressor.h"
#include "../base/AudioConfig.h"
#include <cmath>

namespace arstro
{
    Compressor::Compressor() : SignalProcessor(propertyCount)
    {
        setSmoothEnable(false);
        Sample sr = AudioConfig::instance().sampleRate();
        initProperty(thresholdDbID, -18.0);
        initProperty(ratioID, 4.0);
        initProperty(attackID, 0.005 * sr);  // 5 ms
        initProperty(releaseID, 0.100 * sr); // 100 ms
        initProperty(makeupID, 1.0);
        ensureChannels();
        update();
    }

    void Compressor::ensureChannels()
    {
        int n = AudioConfig::instance().channelCount();
        if ((int)mEnv.size() != n)
            mEnv.assign(n, 0.0);
    }
    void Compressor::onChannelCountChanged() { ensureChannels(); }

    void Compressor::setThresholdDb(Sample db) { setProperty(thresholdDbID, db); }
    void Compressor::setRatio(Sample ratio) { setProperty(ratioID, ratio < 1.0 ? 1.0 : ratio); }
    void Compressor::setAttackMs(Sample ms) { setProperty(attackID, ms * 0.001 * AudioConfig::instance().sampleRate()); }
    void Compressor::setReleaseMs(Sample ms) { setProperty(releaseID, ms * 0.001 * AudioConfig::instance().sampleRate()); }
    void Compressor::setMakeupGain(Sample linear) { setProperty(makeupID, linear); }

    void Compressor::update()
    {
        Sample atk = getProperty(attackID);
        Sample rel = getProperty(releaseID);
        mAtkCoeff = atk > 0.0 ? std::exp(-1.0 / atk) : 0.0;
        mRelCoeff = rel > 0.0 ? std::exp(-1.0 / rel) : 0.0;
    }

    Sample Compressor::process(Sample in, int channel)
    {
        if (channel < 0 || channel >= (int)mEnv.size())
            return in;
        Sample level = std::fabs(in);
        Sample &env = mEnv[channel];
        // One-pole peak follower with separate attack/release.
        if (level > env)
            env = mAtkCoeff * (env - level) + level;
        else
            env = mRelCoeff * (env - level) + level;
        env = arstroFlush(env);

        const Sample eps = 1e-9;
        Sample envDb = 20.0 * std::log10(env + eps);
        Sample threshold = getProperty(thresholdDbID);
        Sample ratio = getProperty(ratioID);
        Sample gain = 1.0;
        if (envDb > threshold)
        {
            Sample reductionDb = (envDb - threshold) * (1.0 - 1.0 / ratio);
            gain = std::pow(10.0, -reductionDb / 20.0);
        }
        return in * gain * getProperty(makeupID);
    }
}
