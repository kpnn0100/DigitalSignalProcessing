# Gyrus Space DSP Library

![Gyrus Space Logo](https://raw.githubusercontent.com/kpnn0100/DigitalSignalProcessing/main/gyrus_space.png)

## Table of Contents

- [Introduction](#introduction)
- [Getting Started](#getting-started)
- [Usage](#usage)
- [API Reference](#api-reference)
- [Contributing](#contributing)
- [License](#license)
- [Disclaimer](#disclaimer)

## Introduction

Welcome to the Gyrus Space DSP Library, an open-source Digital Signal Processing (DSP) library developed by Gyrus Space. This library provides various functions and utilities for processing digital signals and audio data. It is designed to be flexible, modular, and easy to use in a variety of applications.

The best thing about this library is that all processor modules can smoothly change their properties when a new property is set based on their buffer size. This feature can be easily implemented in a derived class by defining a property count. The best example to see how this feature is useful is dynamic delay, where the delay can be smoothly changed, resulting in the Doppler effect.

## Getting Started

To use the Gyrus Space DSP Library in your project, follow these simple steps:

1. Clone the repository or download the source code from the [GitHub repository](https://github.com/kpnn0100/DigitalSignalProcessing).

2. Include the main header file `gyrus_space_dsp.h` in your C++ code:

```cpp
#include "gyrus_space_dsp.h"
```

3. Begin by creating instances of the provided signal processing modules and start using their functionality.

## Usage

Here's a basic example of how to create and use a signal processing module from the Gyrus Space DSP Library:

```cpp
#include "gyrus_space_dsp.h"

int main() {
    // Create a signal processor instance
    Gain processor;

    // Set the sample rate
    processor.setSampleRate(44100.0);
    processor.setGain(2.0);
    // Process an input signal
    double inputSignal = 0.5;
    double outputSignal = processor.out(inputSignal);

    // Print the processed output signal
    std::cout << "Input Signal: " << inputSignal << ", Output Signal: " << outputSignal << std::endl;

    return 0;
}
```

## API Reference
(Update later)

## Contributing

**We welcome contributions to the Gyrus Space DSP Library! If you'd like to contribute, please follow these steps:**

1. **Fork the [GitHub repository](https://github.com/kpnn0100/DigitalSignalProcessing).**

2. **Create a new branch for your feature or bug fix.**

3. **Make your changes and write tests if applicable.**

4. **Ensure that your code passes all tests.**

5. **Submit a pull request to the `main` branch of the original repository.**

---

**When creating a new class based on this library, please make sure to follow these guidelines:**

1. **Inherit from the `SignalProcessor` class.**

2. **Define a property list by defining an enumeration, like this:**
   ```cpp
   enum PropertyIndex {
       delayID,
       decayID,
       propertyCount
   };
   ```

3. **Call the constructor of the base class with an integer argument to define the number of properties, as follows:**
   ```cpp
   Reverb::Reverb() : SignalProcessor(propertyCount) {
   ```

4. **Create a wrapper for getting and setting properties if you want (recommended), like this:**
   ```cpp
   void Reverb::setDecayInMs(double decay) {
       setProperty(decayID, decay);
   }
   ```

5. **Override the `onPropertyUpdated()` method, which will be called if a property changes. This is where you onPropertyUpdated the state of the processor.**

6. **Override the `double process(double in)` method, which is the main processor for data.**

---

By following these guidelines, you can contribute effectively to the Gyrus Space DSP Library and create new classes that seamlessly integrate with the existing architecture while maintaining code quality and consistency.

## License

This project is licensed under the [MIT License](LICENSE).

## Disclaimer

This library is provided as-is without any warranties. The author and organization shall not be held liable for any damages or liabilities arising from the use of this library.

---

Author: kpnn0100
Organization: Gyrus Space
GitHub Repository: [https://github.com/kpnn0100/DigitalSignalProcessing](https://github.com/kpnn0100/DigitalSignalProcessing)