#include "Block.h"

Block::Block()
{
    setSmoothEnable(false);
}

Block::~Block()
{
}

void Block::onInit()
{
    for (auto processor : processorList)
    {
        processor->onInit();
    }
}

void Block::onPropertyUpdated()
{
    if (isParallel)
    {
        // Calculate the maximum sample delay among all processors for parallel mode
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
        // Calculate the total delay for serial mode
        double delay = 0;
        for (auto processor : processorList)
        {
            delay += processor->getSampleDelay();
        }
        setSampleDelay(delay);
    }

    // Create Delay instances for each processor to synchronize their sample delays
    delaySyncMachine = vector<Delay>(processorList.size());
    for (int i = 0; i < processorList.size(); i++)
    {
        delaySyncMachine[i] = Delay(getSampleDelay() - processorList[i]->getSampleDelay());
    }
}

void Block::setIsParallel(bool newState)
{
    isParallel = newState; // Set the parallel processing mode
}

void Block::add(SignalProcessor *newProcessor)
{
    processorList.push_back(newProcessor); // Add a new processor to the list
    newProcessor->setParent(this);         // Set the parent of the new processor to this block
    callUpdate();                          // Update the block's state
}

void Block::remove(SignalProcessor *processor)
{
    SignalProcessor *rmProcessor = nullptr;
    for (int i = 0; i < processorList.size(); i++)
    {
        if (processorList[i] == processor)
        {
            rmProcessor = processor;
            processorList.erase(processorList.begin() + i);
            break;
        }
    }
    if (rmProcessor == nullptr)
    {
        std::cout<<"There no processor in the list matched"<<std::endl;
    }
}

void Block::setNeedAverage(bool needAverage)
{
    mNeedAverage = needAverage;
}
double Block::process(double in)
{
    double out = isParallel ? 0.0 : in;
    for (int i = 0; i < processorList.size(); i++)
        if (isParallel)
        {
            if (mNeedAverage)
            {
                out += delaySyncMachine[i].out(processorList[i]->out(in)) / (double)processorList.size();
            }
            else
            {
                out += delaySyncMachine[i].out(processorList[i]->out(in));
            }
        }
        else
        {
            out = processorList[i]->out(out);
        }
    // if (out < OUTPUT_MIN)
    //     return OUTPUT_MIN;
    // else if (out > OUTPUT_MAX)
    //     return OUTPUT_MAX;
    // else
    return out;
}

vector<SignalProcessor*> &Block::getProcessorList()
{
    return processorList;
}
