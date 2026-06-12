/*
 *  Arstro DSP Library — ESP32DigitalSynth
 *
 *  SignalGenerator: base for sound sources (things that PRODUCE signal).
 *
 *  A generator is a *voiceable* source: it owns its own ADSR amplitude envelope
 *  (the envelope is part of the generator, never an effects-chain stage) and
 *  exposes noteOn/noteOff. Subclasses implement generate(channel) to produce the
 *  raw, un-enveloped waveform for a given channel, managing their own per-channel
 *  phase/state. process() = generate(channel) * envelope.
 *
 *  Frequency is stored as a plain member (Hz); subclasses convert to a per-sample
 *  phase increment using AudioConfig::sampleRate(). No wall-clock time.
 */
#pragma once
#include "SignalProcessor.h"
#include "../envelope/ADSREnvelope.h"

namespace arstro
{
    class SignalGenerator : public SignalProcessor
    {
    public:
        /** @param propertyCount number of properties the concrete generator declares. */
        explicit SignalGenerator(int propertyCount);

        virtual void setFrequency(Sample hz); // base note frequency in Hz
        Sample frequency() const { return mFrequency; }

        // Voicing — forwarded to the owned envelope.
        void noteOn(Sample velocity);
        void noteOff();
        bool isFinished() const { return mEnvelope.isFinished(); }

        /** Block path: add `level * (generate*envelope)` to buf for one channel.
         *  Virtual so a subclass can supply a faster block implementation. */
        virtual void addBlock(Sample *buf, int frames, int channel, Sample level);

        // ADSR pass-throughs (the envelope lives here, on the source).
        void setAttackMs(Sample ms) { mEnvelope.setAttackMs(ms); }
        void setDecayMs(Sample ms) { mEnvelope.setDecayMs(ms); }
        void setSustain(Sample level) { mEnvelope.setSustain(level); }
        void setReleaseMs(Sample ms) { mEnvelope.setReleaseMs(ms); }

    protected:
        /** Subclass: raw waveform sample for this channel; advance own phase here. */
        virtual Sample generate(int channel) = 0;

        /** generate(channel) * envelope(channel). Input is ignored (a source). */
        Sample process(Sample in, int channel) override;

        ADSREnvelope mEnvelope;
        Sample mFrequency = 440.0;
    };
}
