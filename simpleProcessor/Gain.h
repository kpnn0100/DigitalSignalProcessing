#pragma once
#include "CircularList.h"
#include "SignalProcessor.h"
class Gain : public SignalProcessor
{
protected:
    double mGainFactor;
public:
    Gain();
    ~Gain();
    Gain(double newGain);
    void setGain(double newGain);
    virtual double out(double in);

};
