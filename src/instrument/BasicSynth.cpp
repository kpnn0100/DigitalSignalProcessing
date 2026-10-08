#include "BasicSynth.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    struct BasicSynth::Voice
    {
        Oscillator osc[2];
        StateVariableFilter filter;
        ADSREnvelope amp, fenv;
        Noise noise;
        int note = -1;
        bool active = false, held = false;
        long age = 0;
        Sample gain = 0.0; // velocity × volume for this note
    };

    namespace
    {
        Oscillator::Waveform toOsc(BasicSynth::Wave w)
        {
            switch (w)
            {
            case BasicSynth::Sine: return Oscillator::Sine;
            case BasicSynth::Square: return Oscillator::Square;
            case BasicSynth::Triangle: return Oscillator::Triangle;
            case BasicSynth::Saw: break;
            }
            return Oscillator::Saw;
        }

        // (Y1) equal temperament, A4 = 440 Hz.
        Sample midiHz(Sample note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }
    }

    BasicSynth::BasicSynth()
    {
        for (int i = 0; i < kVoices; ++i)
        {
            auto v = std::make_unique<Voice>();
            v->noise.setSeed(0x5EED0000u + (uint32_t)i); // (Y6) deterministic per voice
            for (auto &o : v->osc)
            {
                // The gate: the oscillator's own ADSR must not shape the sound (header comment).
                o.setAttackMs(0.0);
                o.setDecayMs(0.0);
                o.setSustain(1.0);
                o.setReleaseMs(0.0);
            }
            mVoices.push_back(std::move(v));
        }
        setParams(mParams);
    }

    BasicSynth::~BasicSynth() = default;

    void BasicSynth::applyTo(Voice &v)
    {
        const Osc *o[2] = {&mParams.osc1, &mParams.osc2};
        for (int k = 0; k < 2; ++k)
        {
            v.osc[k].setWaveform(toOsc(o[k]->wave));
            v.osc[k].setVoiceCount(o[k]->voices);
            v.osc[k].setDetuneCents(o[k]->detune);
            if (v.note >= 0) v.osc[k].setFrequency(oscFrequency(k, v.note));
        }
        v.filter.setMode(mParams.filterMode);
        v.filter.setResonance(mParams.resonance);
        const Env &a = mParams.ampEnv, &f = mParams.filterEnv;
        v.amp.setAttackMs(a.attack); v.amp.setDecayMs(a.decay); v.amp.setSustain(a.sustain); v.amp.setReleaseMs(a.release);
        v.fenv.setAttackMs(f.attack); v.fenv.setDecayMs(f.decay); v.fenv.setSustain(f.sustain); v.fenv.setReleaseMs(f.release);
    }

    void BasicSynth::setParams(const Params &p)
    {
        mParams = p;
        mParams.osc1.voices = std::clamp(mParams.osc1.voices, 1, Oscillator::kMaxVoices);
        mParams.osc2.voices = std::clamp(mParams.osc2.voices, 1, Oscillator::kMaxVoices);
        for (auto &v : mVoices)
            applyTo(*v);
    }

    Sample BasicSynth::oscFrequency(int which, int note) const
    {
        // (Y2) the note, moved by the oscillator's octave, semitones and cents.
        const Osc &o = which == 0 ? mParams.osc1 : mParams.osc2;
        return midiHz(note + 12.0 * o.octave + o.semi + o.fine / 100.0);
    }

    Sample BasicSynth::cutoffFor(int note, Sample env) const
    {
        // (Y3) exponential: the envelope and key tracking add OCTAVES, as an analog synth's CV does.
        const Sample oct = mParams.envAmount * env + mParams.keytrack * (note - 60) / 12.0;
        return std::clamp(mParams.cutoff * std::pow(2.0, oct), (Sample)20.0, (Sample)20000.0);
    }

    void BasicSynth::noteOn(int note, int velocity)
    {
        if (note < 0 || note > 127 || velocity <= 0) { noteOff(note); return; }
        // (Y5) the same note retriggers its voice; else a free voice; else the oldest is stolen.
        Voice *pick = nullptr;
        for (auto &v : mVoices)
            if (v->active && v->note == note) { pick = v.get(); break; }
        if (!pick)
            for (auto &v : mVoices)
                if (!v->active) { pick = v.get(); break; }
        if (!pick)
        {
            pick = mVoices[0].get();
            for (auto &v : mVoices)
                if (v->age < pick->age) pick = v.get();
        }
        Voice &v = *pick;
        v.note = note;
        v.active = v.held = true;
        v.age = ++mClock;
        // (Y4) level: volume × velocity scaled by the sensitivity.
        const Sample vel = std::clamp(velocity, 1, 127) / 127.0;
        v.gain = std::pow(10.0, mParams.volumeDb / 20.0) * (1.0 - mParams.velocity + mParams.velocity * vel);
        for (int k = 0; k < 2; ++k)
        {
            v.osc[k].setFrequency(oscFrequency(k, note));
            v.osc[k].noteOn(1.0);
        }
        v.amp.noteOn(1.0);
        v.fenv.noteOn(1.0);
    }

    void BasicSynth::noteOff(int note)
    {
        for (auto &v : mVoices)
            if (v->active && v->held && v->note == note)
            {
                v->held = false;
                v->amp.noteOff();
                v->fenv.noteOff();
            }
    }

    void BasicSynth::allNotesOff()
    {
        for (auto &v : mVoices)
            if (v->active && v->held)
            {
                v->held = false;
                v->amp.noteOff();
                v->fenv.noteOff();
            }
    }

    void BasicSynth::reset()
    {
        for (auto &v : mVoices)
        {
            v->active = v->held = false;
            v->note = -1;
            v->filter.reset();
            for (auto &o : v->osc) o.noteOff();
        }
    }

    int BasicSynth::activeVoices() const
    {
        int n = 0;
        for (auto &v : mVoices) n += v->active ? 1 : 0;
        return n;
    }

    void BasicSynth::render(Sample *const *out, int channels, int frames)
    {
        // Every channel the library is configured for is advanced (the envelopes keep per-channel
        // state and a voice ends only when all of them are idle); only `channels` are written.
        const int chans = AudioConfig::instance().channelCount();
        if ((int)mScratch.size() < frames) mScratch.assign(frames, 0.0);
        const Sample l1 = mParams.osc1.level, l2 = mParams.osc2.level, nz = mParams.noise;
        for (auto &vp : mVoices)
        {
            Voice &v = *vp;
            if (!v.active) continue;
            for (int c = 0; c < chans; ++c)
            {
                Sample *buf = mScratch.data();
                std::fill(buf, buf + frames, 0.0);
                v.osc[0].addBlock(buf, frames, c, l1);
                v.osc[1].addBlock(buf, frames, c, l2);
                Sample *dst = c < channels ? out[c] : nullptr;
                for (int i = 0; i < frames; ++i)
                {
                    const Sample x = buf[i] + (nz > 0 ? nz * v.noise.next() : 0.0);
                    const Sample y = v.filter.tick(x, c, cutoffFor(v.note, v.fenv.nextValue(c)));
                    const Sample s = y * v.amp.nextValue(c) * v.gain;
                    if (dst) dst[i] += s;
                }
            }
            if (v.amp.isFinished())
            {
                v.active = false;
                for (auto &o : v.osc) o.noteOff(); // close the gates: the tail has ended
            }
        }
    }
}
