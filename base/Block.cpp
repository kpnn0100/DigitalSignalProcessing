#include "Block.h"

void Block::update()
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

void Block::add(SignalProcessor* newProcessor)
{
    processorList.push_back(newProcessor); // Add a new processor to the list
    newProcessor->setParent(this); // Set the parent of the new processor to this block
    callUpdate(); // Update the block's state
}

double Block::process(double in)
{
    if (isParallel)
    {
        double out = 0;

        // Process each processor's output using synchronized delays
        for (int i = 0; i < processorList.size(); i++)
        {
            out += delaySyncMachine[i].out(processorList[i]->out(in));
        }

        // Average the outputs and ensure the value is within the range [-1, 1]
        out = out / processorList.size();
        if (out < -1)
            return -1;
        else if (out > 1)
            return 1;
        else
            return out;
    }
    else
    {
        double out = in;

        // Process the input through each processor sequentially
        for (int i = 0; i < processorList.size(); i++)
        {
            out = processorList[i]->out(out);
        }

        // Ensure the output value is within the range [-1, 1]
        if (out < -1)
            return -1;
        else if (out > 1)
            return 1;
        else
            return out;
    }
}
