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
        void add(SignalProcessor * newProcessor);
        virtual double out(double in) override;

};
