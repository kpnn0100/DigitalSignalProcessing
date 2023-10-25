/*
 *  Gyrus Space DSP Library
 *  Author: kpnn0100
 *  Organization: Gyrus Space
 *
 *  Description:
 *  This is an open-source Digital Signal Processing (DSP) library developed by Gyrus Space.
 *  It provides various functions and utilities for processing digital signals and audio data.
 *  The library is free to use and open for contributions from the community.
 *
 *  License: MIT License
 *  GitHub Repository: https://github.com/kpnn0100/DigitalSignalProcessing
 *
 *  Disclaimer: This library is provided as-is without any warranties. The author and organization
 *  shall not be held liable for any damages or liabilities arising from the use of this library.
 */

/*
 *  Created by kpnn0100
 *  Version 1.0: create repeater to repeat a processor
 */

#pragma once
#include "../base/SignalProcessor.h"
#include "../base/Block.h"
template<class ProcessorType>
class ProcessorRepeater : public ProcessorType
{
    static_assert(std::is_base_of<SignalProcessor, ProcessorType>::value, "Template must be SignalProcessor's child");
private:
    std::vector<ProcessorType> filterList;
public:
    ProcessorRepeater();
    void setRepeaterCount(int repeaterCount);
    virtual void onPropertyChanged(int propertyID, double value) override;
    virtual double process(double in) override;
};
template<class ProcessorType>
ProcessorRepeater<ProcessorType>::ProcessorRepeater()
{
    filterList.resize(1);
}
template<class ProcessorType>
void ProcessorRepeater<ProcessorType>::setRepeaterCount(int repeaterCount)
{
    if (filterList.size() == repeaterCount)
        return;
    if (repeaterCount < 1)
        return;
    while (filterList.size() < repeaterCount)
    {
        filterList.push_back(filterList.back());
    }
    if (filterList.size() > repeaterCount)
    {
        filterList.resize(repeaterCount);
    }
}

template<class ProcessorType>
inline void ProcessorRepeater<ProcessorType>::onPropertyChanged(int propertyID, double value)
{
    for (auto &filter : filterList)
    {
        filter.setProperty(propertyID, value);
    }
}

template<class ProcessorType>
inline double ProcessorRepeater<ProcessorType>::process(double in)
{
    double out = in;
    for (auto & filter : filterList)
    {
        out = filter.out(out);
    }
    return out;
}

