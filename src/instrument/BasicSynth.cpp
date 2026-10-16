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
        // (Y6) one generator PER CHANNEL: a shared one would hand each channel whichever stretch of
        // the sequence the block size left it, and the output would depend on how a host chops time.
        std::vector<Noise> noise;
        uint32_t seed = 0;
        int note = -1;
        bool active = false, held = false;
        long age = 0;
        Sample gain = 0.0; // velocity × volume for this note
        // (Y8) the pitch: the note, or a glide from the last — linear in semitones, `left` of `total` samples to go
        Sample pitch = 0.0, from = 0.0, to = 0.0;
        long long left = 0, total = 0;
        Sample applied = -1000.0;          // the pitch the oscillators were last set to (with bend and vibrato)
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
        mScratch.assign(4096, 0.0); // a host's blocks up to 4096 frames never allocate in render()
        mRamp.assign(4096, 0.0);
        for (int i = 0; i < kVoices; ++i)
        {
            auto v = std::make_unique<Voice>();
            v->seed = 0x5EED0000u + 16u * (uint32_t)i; // (Y6) deterministic per voice and channel
            // made HERE, not on a voice's first note: a host's audio thread must not allocate
            for (int c = 0; c < AudioConfig::instance().channelCount(); ++c) v->noise.emplace_back(v->seed + (uint32_t)c);
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
            v.osc[k].setStereoSpreadCents(o[k]->width); // REQ-synth2-6
            if (v.note >= 0) v.osc[k].setFrequency(oscFrequency(k, v.applied > -999.0 ? v.applied : (Sample)v.note));
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

    Sample BasicSynth::oscFrequency(int which, Sample pitch) const
    {
        // (Y2) the pitch, moved by the oscillator's octave, semitones and cents.
        const Osc &o = which == 0 ? mParams.osc1 : mParams.osc2;
        return midiHz(pitch + 12.0 * o.octave + o.semi + o.fine / 100.0);
    }

    Sample BasicSynth::modulation() const
    {
        // (Y9) the wheel's bend, and the vibrato: a sine at vibratoRate, its phase from `reset` — the same however
        // time is chopped
        const Sample fs = std::max((Sample)8000.0, (Sample)AudioConfig::instance().sampleRate());
        const Sample vib = mMod > 0.0 ? mMod * mParams.vibrato * std::sin(2.0 * 3.14159265358979323846 * mParams.vibratoRate * (Sample)mPos / fs) : 0.0;
        return mBend * mParams.bendRange + vib;
    }

    void BasicSynth::retune(Voice &v)
    {
        // (Y8) the glide's place now; (Y7) the oscillators re-set only when the pitch moved — a still note never is
        if (v.left > 0 && v.total > 0) v.pitch = v.to - (v.to - v.from) * (Sample)v.left / (Sample)v.total;
        else v.pitch = v.to;
        const Sample p = v.pitch + modulation();
        if (p == v.applied) return;
        v.applied = p;
        for (int k = 0; k < 2; ++k) v.osc[k].setFrequency(oscFrequency(k, p));
    }

    Sample BasicSynth::voicePitch(int i) const
    {
        if (i < 0 || i >= (int)mVoices.size() || !mVoices[(size_t)i]->active) return -1.0;
        return mVoices[(size_t)i]->applied;
    }

    void BasicSynth::pitchBend(Sample amount) { mBend = std::clamp(amount, (Sample)-1.0, (Sample)1.0); }
    void BasicSynth::modWheel(Sample amount) { mMod = std::clamp(amount, (Sample)0.0, (Sample)1.0); }

    void BasicSynth::monoOn(int note, int velocity)
    {
        // (Y10) one voice: the key on top of the stack sounds; legato slides to a key pressed while one is held
        // without striking again; glide (both modes) slides from where the voice was
        for (int i = 0; i < mStackSize; ++i)
            if (mStack[i] == note) { std::copy(mStack + i + 1, mStack + mStackSize, mStack + i); --mStackSize; break; }
        if (mStackSize == 16) { std::copy(mStack + 1, mStack + 16, mStack); --mStackSize; }
        mStack[mStackSize++] = note;
        Voice &v = *mVoices[0];
        const bool held = v.active && v.held;
        const long long glide = (long long)std::llround(mParams.glide * 0.001 * std::max((Sample)8000.0, (Sample)AudioConfig::instance().sampleRate()));
        if (v.active && glide > 0) { v.from = v.pitch; v.left = v.total = glide; }
        else { v.from = (Sample)note; v.left = v.total = 0; }
        v.to = (Sample)note;
        v.note = note;
        if (mParams.mode == Legato && held) { retune(v); return; } // no new attack
        v.active = v.held = true;
        v.age = ++mClock;
        const Sample vel = std::clamp(velocity, 1, 127) / 127.0;
        v.gain = 1.0 - mParams.velocity + mParams.velocity * vel;
        retune(v);
        for (auto &o : v.osc) o.noteOn(1.0);
        v.amp.noteOn(1.0);
        v.fenv.noteOn(1.0);
    }

    void BasicSynth::monoOff(int note)
    {
        int at = -1;
        for (int i = 0; i < mStackSize; ++i)
            if (mStack[i] == note) at = i;
        if (at < 0) return;
        std::copy(mStack + at + 1, mStack + mStackSize, mStack + at);
        --mStackSize;
        Voice &v = *mVoices[0];
        if (!v.active || !v.held || v.note != note) return; // a key under the one sounding: gone from the stack only
        if (mStackSize > 0)
        {
            // back to the key still held, without a new attack (glide, if set, slides there)
            const int back = mStack[mStackSize - 1];
            const long long glide = (long long)std::llround(mParams.glide * 0.001 * std::max((Sample)8000.0, (Sample)AudioConfig::instance().sampleRate()));
            v.from = v.pitch;
            v.to = (Sample)back;
            v.left = v.total = glide;
            v.note = back;
            retune(v);
            return;
        }
        v.held = false;
        v.amp.noteOff();
        v.fenv.noteOff();
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
        if (mParams.mode != Poly) { monoOn(note, velocity); return; }
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
        // (Y4) level: velocity scaled by the sensitivity, here — the VOLUME is applied as the block renders (D-8:
        // set or automated while a note sounds, it moves that note, as the kit's and the piano's do)
        const Sample vel = std::clamp(velocity, 1, 127) / 127.0;
        v.gain = 1.0 - mParams.velocity + mParams.velocity * vel;
        v.from = v.to = v.pitch = (Sample)note;
        v.left = v.total = 0;
        v.applied = -1000.0; // a new note: its pitch set whatever was there
        retune(v);
        for (int k = 0; k < 2; ++k) v.osc[k].noteOn(1.0);
        v.amp.noteOn(1.0);
        v.fenv.noteOn(1.0);
    }

    void BasicSynth::noteOff(int note)
    {
        if (mParams.mode != Poly) { monoOff(note); return; }
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
        mStackSize = 0;
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
            v->left = v->total = 0;
            v->applied = -1000.0;
        }
        mVolGain = -1.0; // a reset starts at the volume as it stands
        mPos = 0;        // (Y7) the control grid and the vibrato start again
        mStackSize = 0;
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
        if ((int)mScratch.size() < frames) mScratch.assign(frames, 0.0); // grows once, to the host's largest block
        if ((int)mRamp.size() < frames) mRamp.assign(frames, 0.0);
        // (Y4) the volume, linear across the block from where the last one ended — a change never steps
        const Sample vol = std::pow(10.0, mParams.volumeDb / 20.0);
        if (mVolGain < 0.0) mVolGain = vol;
        for (int i = 0; i < frames; ++i) mRamp[(size_t)i] = mVolGain + (vol - mVolGain) * (Sample)(i + 1) / (Sample)frames;
        mVolGain = vol;
        const Sample l1 = mParams.osc1.level, l2 = mParams.osc2.level, nz = mParams.noise;
        // (Y7) in pieces on the absolute control grid: at each grid point every sounding voice's pitch (glide, bend,
        // vibrato) is re-set if it moved — so how a host chops time never changes a sample
        int done = 0;
        while (done < frames)
        {
            const int into = (int)(mPos % kControl);
            const int n = std::min(frames - done, kControl - into);
            if (into == 0)
                for (auto &vp : mVoices)
                    if (vp->active) retune(*vp);
            for (auto &vp : mVoices)
            {
                Voice &v = *vp;
                if (!v.active) continue;
                while ((int)v.noise.size() < chans) v.noise.emplace_back(v.seed + (uint32_t)v.noise.size());
                for (int c = 0; c < chans; ++c)
                {
                    Sample *buf = mScratch.data();
                    std::fill(buf, buf + n, 0.0);
                    v.osc[0].addBlock(buf, n, c, l1);
                    v.osc[1].addBlock(buf, n, c, l2);
                    Sample *dst = c < channels ? out[c] + done : nullptr;
                    for (int i = 0; i < n; ++i)
                    {
                        const Sample x = buf[i] + (nz > 0 ? nz * v.noise[c].next() : 0.0);
                        const Sample y = v.filter.tick(x, c, cutoffFor(v.note, v.fenv.nextValue(c)));
                        const Sample s = y * v.amp.nextValue(c) * v.gain * mRamp[(size_t)(done + i)];
                        if (dst) dst[i] += s;
                    }
                }
                if (v.left > 0) v.left = std::max(0LL, v.left - n); // (Y8) the glide moves on with the samples
                if (v.amp.isFinished())
                {
                    v.active = false;
                    for (auto &o : v.osc) o.noteOff(); // close the gates: the tail has ended
                }
            }
            mPos += n;
            done += n;
        }
    }
}
