#pragma once
#include "CircularList.h"
#include "SignalProcessor.h"
#include "Delay.h"
#include <vector>
using namespace std;
class Block : public SignalProcessor
{
    protected:
        bool isParallel = false;
        vector<SignalProcessor*> processorList;
        vector<Delay> delaySyncMachine;
    public:
        void update();
        void setIsParallel(bool newState);
        void add(SignalProcessor &newProcessor);
        virtual double out(double in) override;

};

#include "Block.h"

void Block::update()
{
    if (isParallel)
    {
        double sampleDelay = 0;
        for (auto processor : processorList)
        {
            if (processor->getSampleDelay() > sampleDelay)
            {
                setSampleDelay(processor->getSampleDelay());
            }
        }
    }
    else
    {
        double delay = 0;
        for (auto processor : processorList)
        {
            delay += processor->getSampleDelay();
        }
        setSampleDelay(delay);
    }
    delaySyncMachine = vector<Delay>(processorList.size());
    for (int i = 0; i < processorList.size(); i++)
    {
        delaySyncMachine[i] = Delay(getSampleDelay() - processorList[i]->getSampleDelay());
    }
}

void Block::setIsParallel(bool newState)
{
    isParallel = newState;
}

void Block::add(SignalProcessor &newProcessor)
{
    processorList.push_back(&newProcessor);
    callUpdate();
}

double Block::out(double in)
{
    if (isParallel)
    {
        double out = 0;
        for (int i = 0; i < processorList.size(); i++)
        {
            out += delaySyncMachine[i].out(processorList[i]->out(in));
        }
        out = out / processorList.size();
        if (out < 1 && out > -1)
            return out;
        if (out >= 1)
            return 1;
        if (out <= -1)
            return -1;
    }
    else
    {
        double out = in;
        for (int i = 0; i < processorList.size(); i++)
        {
            out = processorList[i]->out(out);
        }
        if (out < 1 && out > -1)
            return out;
        if (out >= 1)
            return 1;
        if (out <= -1)
            return -1;
    }
}