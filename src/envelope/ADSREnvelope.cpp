#include "ADSREnvelope.h"
#include "../base/AudioConfig.h"

namespace arstro
{
    ADSREnvelope::ADSREnvelope() : SignalProcessor(propertyCount)
    {
        // Envelope timing must NOT be smoothed/interpolated like a tone control;
        // its own state machine is the smoother.
        setSmoothEnable(false);
        initProperty(attackID, 0.005 * AudioConfig::instance().sampleRate());  // 5 ms
        initProperty(decayID, 0.060 * AudioConfig::instance().sampleRate());   // 60 ms
        initProperty(sustainID, 0.7);
        initProperty(releaseID, 0.200 * AudioConfig::instance().sampleRate()); // 200 ms
        ensureChannels();
        update(); // cache the defaults for the hot path
    }

    void ADSREnvelope::ensureChannels()
    {
        int n = AudioConfig::instance().channelCount();
        if ((int)mState.size() != n)
            mState.assign(n, ChannelState{});
    }

    void ADSREnvelope::onChannelCountChanged()
    {
        ensureChannels();
    }

    void ADSREnvelope::setAttackMs(Sample ms)
    {
        setProperty(attackID, ms * 0.001 * AudioConfig::instance().sampleRate());
    }
    void ADSREnvelope::setDecayMs(Sample ms)
    {
        setProperty(decayID, ms * 0.001 * AudioConfig::instance().sampleRate());
    }
    void ADSREnvelope::setSustain(Sample level)
    {
        if (level < 0.0) level = 0.0;
        if (level > 1.0) level = 1.0;
        setProperty(sustainID, level);
    }
    void ADSREnvelope::setReleaseMs(Sample ms)
    {
        setProperty(releaseID, ms * 0.001 * AudioConfig::instance().sampleRate());
    }

    void ADSREnvelope::noteOn(Sample velocity)
    {
        ensureChannels();
        if (velocity < 0.0) velocity = 0.0;
        if (velocity > 1.0) velocity = 1.0;
        mPeak = velocity;
        for (auto &s : mState)
        {
            s.stage = Attack;
            s.sampleInStage = 0;
            // Retrigger from current level for click-free restarts.
        }
    }

    void ADSREnvelope::noteOff()
    {
        for (auto &s : mState)
        {
            if (s.stage != Idle)
            {
                s.releaseStartLevel = s.level;
                s.stage = Release;
                s.sampleInStage = 0;
            }
        }
    }

    bool ADSREnvelope::isFinished() const
    {
        for (const auto &s : mState)
            if (s.stage != Idle)
                return false;
        return true;
    }

    void ADSREnvelope::update()
    {
        mAtk = getProperty(attackID);
        mDec = getProperty(decayID);
        mSus = getProperty(sustainID);
        mRel = getProperty(releaseID);
    }

    Sample ADSREnvelope::process(Sample in, int channel)
    {
        return in * nextValue(channel);
    }

    Sample ADSREnvelope::nextValue(int channel)
    {
        if (channel < 0 || channel >= (int)mState.size())
            return (Sample)0;
        ChannelState &s = mState[channel];

        const Sample attack = mAtk;
        const Sample decay = mDec;
        const Sample sustain = mSus;
        const Sample release = mRel;

        switch (s.stage)
        {
        case Idle:
            s.level = 0.0;
            break;
        case Attack:
            if (attack <= 0.0)
            {
                s.level = mPeak;
                s.stage = Decay;
                s.sampleInStage = 0;
            }
            else
            {
                s.level = mPeak * ((Sample)s.sampleInStage / attack);
                if (++s.sampleInStage >= (long)attack)
                {
                    s.level = mPeak;
                    s.stage = Decay;
                    s.sampleInStage = 0;
                }
            }
            break;
        case Decay:
        {
            Sample target = sustain * mPeak;
            if (decay <= 0.0)
            {
                s.level = target;
                s.stage = Sustain;
                s.sampleInStage = 0;
            }
            else
            {
                Sample r = (Sample)s.sampleInStage / decay;
                s.level = mPeak + (target - mPeak) * r;
                if (++s.sampleInStage >= (long)decay)
                {
                    s.level = target;
                    s.stage = Sustain;
                    s.sampleInStage = 0;
                }
            }
            break;
        }
        case Sustain:
            s.level = sustain * mPeak;
            break;
        case Release:
            if (release <= 0.0)
            {
                s.level = 0.0;
                s.stage = Idle;
            }
            else
            {
                Sample r = (Sample)s.sampleInStage / release;
                s.level = s.releaseStartLevel * (1.0 - r);
                if (++s.sampleInStage >= (long)release)
                {
                    s.level = 0.0;
                    s.stage = Idle;
                }
            }
            break;
        }
        return s.level;
    }
}
