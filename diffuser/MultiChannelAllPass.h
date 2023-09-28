#pragma once
#include "../base/SignalProcessor.h"
#include "../simpleProcessor/Delay.h"

class MultiChannelAllPass : public Delay
{
private:
    int mChannelCount;
    double mDelayInMs;
public:
    MultiChannelAllPass();
    void setChannelCount(int channelCount);
    void setDelayInMs(double delay);
    double process(double in) override;
    void onSampleRateChanged() override;
};
