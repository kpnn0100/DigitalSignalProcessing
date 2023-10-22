/*
  ==============================================================================

    ProcessorRepeater.h
    Created: 21 Oct 2023 9:13:22pm
    Author:  PC

  ==============================================================================
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

