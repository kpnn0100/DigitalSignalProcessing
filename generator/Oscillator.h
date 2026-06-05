/*
 *  Gyrus Space DSP Library — ESP32DigitalSynth
 *
 *  Oscillator: band-limited (PolyBLEP) saw oscillator with built-in unison.
 *
 *  Inherits SignalGenerator, so it owns its ADSR envelope and is voiceable via
 *  noteOn/noteOff. Supports 1..5 detuned unison voices and a stereo spread, all
 *  in Sample. Waveform selection is kept open for future shapes (Open/Closed):
 *  generate() dispatches on mWaveform.
 *
 *  Unison/detune is folded into this one class (rather than a separate
 *  UnisonOscillator) to keep a single clean parameter surface and avoid nesting
 *  envelope-bearing generators inside each other.
 */
#pragma once
#include "../base/SignalGenerator.h"
#include <vector>
#include <cstdint>

namespace gyrus_space
{
    class Oscillator : public SignalGenerator
    {
    public:
        enum Waveform { Saw };
        enum PropertyIndex
        {
            voiceCountID, // 1..5 unison voices
            detuneID,     // total spread in cents across the voices
            spreadID,     // stereo width in cents (per-channel detune offset)
            propertyCount
        };
        static constexpr int kMaxVoices = 5;

        Oscillator();

        void setVoiceCount(int voices);
        void setDetuneCents(Sample cents);
        void setStereoSpreadCents(Sample cents);
        void setWaveform(Waveform w) { mWaveform = w; }

        void setFrequency(Sample hz) override;
        void update() override;
        void onSampleRateChanged() override;
        void onChannelCountChanged() override;

        // Experimental: switch all oscillators between the scalar band-limited
        // (PolyBLEP) path and a branchless block (SIMD-friendly) naive-saw path.
        static void setUseSimd(bool b) { sUseSimd = b; }
        static bool useSimd() { return sUseSimd; }

        // Block path: dispatches to scalar (PolyBLEP) or SIMD-friendly impl.
        void addBlock(Sample *buf, int frames, int channel, Sample level) override;

    protected:
        Sample generate(int channel) override;

    private:
        void recalcVoiceCents();   // from voiceCount + detune
        void recalcIncrements();   // per channel/voice phase increments
        void ensureChannels();
        // Branchless block oscillator (naive saw via fixed-point phase). No
        // per-sample branches, so it vectorizes; trades anti-aliasing for speed.
        void addBlockSimd(Sample *buf, int frames, int channel, Sample level);

        static bool sUseSimd;
        Waveform mWaveform = Saw;
        int mVoiceCount = 1;              // cached (avoids per-sample getProperty)
        Sample mNorm = 1;                 // cached 1/sqrt(voiceCount)
        std::vector<Sample> mVoiceCents;  // [kMaxVoices] symmetric detune per voice
        std::vector<Sample> mPhase;       // [channel*kMaxVoices + voice], in [0,1)
        std::vector<Sample> mInc;         // [channel*kMaxVoices + voice] phase increment
        std::vector<uint32_t> mPhaseU;    // fixed-point phase for the SIMD path
        std::vector<uint32_t> mIncU;      // fixed-point increment for the SIMD path
    };
}
