/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  OutputBlock: the ONLY place Sample is quantized to integer samples.
 *
 *  Everything upstream is Sample. This sink clamps to [-1, 1] and scales to the
 *  configured output bit depth (AudioConfig::outputBitDepth(), default 16), then
 *  packs interleaved channels little-endian for the audio-over-UART transport.
 */
#pragma once
#include "../base/AudioConfig.h"
#include <cstdint>
#include <vector>

namespace arstro
{
    class OutputBlock
    {
    public:
        int bitDepth() const { return AudioConfig::instance().outputBitDepth(); }
        int bytesPerSample() const { return (bitDepth() + 7) / 8; }

        /** Clamp to [-1,1] and scale to a signed integer of the configured depth. */
        int32_t quantize(Sample sample) const
        {
            if (sample > 1.0) sample = 1.0;
            if (sample < -1.0) sample = -1.0;
            const int depth = bitDepth();
            const Sample maxAmp = (Sample)((1LL << (depth - 1)) - 1);
            return (int32_t)(sample * maxAmp);
        }

        /** Append one quantized sample to a little-endian byte buffer. */
        void appendSample(std::vector<uint8_t> &out, Sample sample) const
        {
            int32_t q = quantize(sample);
            int bytes = bytesPerSample();
            for (int b = 0; b < bytes; ++b)
                out.push_back((uint8_t)((q >> (8 * b)) & 0xFF));
        }

        /**
         * Append one interleaved frame (all channels) to a byte buffer.
         * @param perChannel pointer to channelCount() Sample samples.
         */
        void appendFrame(std::vector<uint8_t> &out, const Sample *perChannel) const
        {
            int ch = AudioConfig::instance().channelCount();
            for (int c = 0; c < ch; ++c)
                appendSample(out, perChannel[c]);
        }
    };
}
