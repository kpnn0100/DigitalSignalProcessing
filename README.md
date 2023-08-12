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

We welcome contributions to the Gyrus Space DSP Library! If you'd like to contribute, please follow these steps:

1. Fork the [GitHub repository](https://github.com/kpnn0100/DigitalSignalProcessing).
2. Create a new branch for your feature or bug fix.
3. Make your changes and write tests if applicable.
4. Ensure that your code passes all tests.
5. Submit a pull request to the `main` branch of the original repository.

## License

This project is licensed under the [MIT License](LICENSE).

## Disclaimer

This library is provided as-is without any warranties. The author and organization shall not be held liable for any damages or liabilities arising from the use of this library.

---

Author: kpnn0100
Organization: Gyrus Space
GitHub Repository: [https://github.com/kpnn0100/DigitalSignalProcessing](https://github.com/kpnn0100/DigitalSignalProcessing)