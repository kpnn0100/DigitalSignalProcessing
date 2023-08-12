#pragma once
#include "CircularList.h"
#include <math.h>

class SignalProcessor
{
private:
    SignalProcessor* mParent = nullptr;

protected:
    double mSampleDelay = 0;
    static double mSampleRate;

    void setSampleDelay(double newSampleDelay);

    void callUpdate();

public:
    SignalProcessor();
    void setSampleRate(double sampleRate);

    void setParent(SignalProcessor* parent);

    virtual void update();

    virtual double out(double in) = 0;

    double getSampleDelay();

    virtual ~SignalProcessor();
};
