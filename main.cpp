#include <iostream>
#include <vector>
#include <sndfile.h>
#include "gyrus_space_dsp.h"
using namespace gyrus_space;
std::vector<float> readWavToVector(const char *filename)
{
    std::vector<float> samples;

    SF_INFO sfinfo;
    SNDFILE *file = sf_open(filename, SFM_READ, &sfinfo);

    if (!file)
    {
        std::cerr << "Error: Failed to open the WAV file." << std::endl;
        return samples;
    }

    // Calculate the total number of samples in the file
    const sf_count_t num_samples = sfinfo.frames * sfinfo.channels;

    // Resize the vector to hold all the samples
    samples.resize(num_samples);

    // Read the audio samples into the vector
    sf_readf_float(file, samples.data(), num_samples);

    // Close the file
    sf_close(file);

    return samples;
}
bool writeVectorToWav(const char *filename, const std::vector<float> &samples, int sampleRate)
{
    SF_INFO sfinfo;
    sfinfo.samplerate = sampleRate;
    sfinfo.channels = 1;                             // Change this to the number of audio channels (e.g., 2 for stereo).
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT; // Use floating-point format.

    SNDFILE *file = sf_open(filename, SFM_WRITE, &sfinfo);

    if (!file)
    {
        std::cerr << "Error: Failed to open the output WAV file." << std::endl;
        return false;
    }

    // Write the audio samples to the file
    sf_writef_float(file, samples.data(), static_cast<sf_count_t>(samples.size()));

    // Close the file
    sf_close(file);

    return true;
}
int main()
{
    const char *filename = "sample.wav";
    const char *outputFile = "output.wav";
    int numberOfAllpass = 5;
    double baseDelay = 48.0;
    SignalProcessor::setBufferSize(0);
    Reverb mReverb;
    int sampleRate = 44100;
    mReverb.setSampleRate(44100);
    mReverb.setDelayInMs(100);
    mReverb.setDecayInMs(2000.0);
    
    std::vector<float> audioSamples = readWavToVector(filename);

    if (audioSamples.empty())
    {
        std::cerr << "Error reading WAV file." << std::endl;
        return 1;
    }
    for (int i = 0; i < sampleRate * 4; i++)
    {
        audioSamples.push_back(0.0);
    }
    for (int i = 0; i < audioSamples.size(); i++)
    {
        
        double data = audioSamples[i];
        audioSamples[i] = mReverb.out(data);//+data;
        if (i>44100)
        {
            int breakhere = 2;
        }
        // for (int j = 0; j < numberOfAllpass; j++)
        // {   
        //     audioSamples[i] = mAllPass[j].out(audioSamples[i]);
        // }
    }
    // Now you can work with the audio samples stored in the vector
    // For example, you can iterate through the vector or perform various operations.
    if (writeVectorToWav(outputFile, audioSamples, sampleRate))
    {
        std::cout << "WAV file successfully written." << std::endl;
    }
    else
    {
        std::cerr << "Error writing WAV file." << std::endl;
        return 1;
    }
    return 0;
}
