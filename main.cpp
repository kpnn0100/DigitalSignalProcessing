#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include "GS_DSP.h"

int main() {
    // Example usage
    std::vector<double> sample{1,2,3,4,5,6,7};
    Delay delayModule(2.2,8.0);
    for (int i = 0; i<sample.size();i++)
    {
        sample[i] = delayModule.out(sample[i]);
    }
    int a;


    return 0;
}
