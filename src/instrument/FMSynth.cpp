#include "FMSynth.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr int kTable = 4096;
        // (M3) for each algorithm: the operators modulating each operator (bit j set in kMods[a][k]), and the carriers
        constexpr unsigned kMods[8][4] = {
            {1u << 1, 1u << 2, 1u << 3, 0},             // 1: 4→3→2→1
            {1u << 1, (1u << 2) | (1u << 3), 0, 0},     // 2: (3, 4)→2→1
            {(1u << 1) | (1u << 3), 1u << 2, 0, 0},     // 3: 3→2→1, 4→1
            {(1u << 1) | (1u << 2), 0, 1u << 3, 0},     // 4: 4→3→1, 2→1
            {1u << 1, 0, 1u << 3, 0},                   // 5: 4→3, 2→1
            {1u << 3, 1u << 3, 1u << 3, 0},             // 6: 4→(3, 2, 1)
            {0, 0, 1u << 3, 0},                         // 7: 4→3; 2, 1
            {0, 0, 0, 0}};                              // 8: none
        constexpr unsigned kCarriers[8] = {1u, 1u, 1u, 1u, 1u | 4u, 1u | 2u | 4u, 1u | 2u | 4u, 15u};

        const std::vector<float> &table()
        {
            static const std::vector<float> t = [] {
                std::vector<float> v(kTable + 1);
                for (int i = 0; i <= kTable; ++i) v[(size_t)i] = (float)std::sin(2.0 * kPi * i / kTable);
                return v;
            }();
            return t;
        }
        Sample midiHz(Sample note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }
    }

    struct FMSynth::Voice
    {
        ADSREnvelope env[kOps];
        double phase[kOps] = {0, 0, 0, 0}, inc[kOps] = {0, 0, 0, 0};
        Sample fb1 = 0.0, fb2 = 0.0; // operator 4's last two outputs (M2)
        int note = -1;
        bool active = false, held = false;
        long age = 0;
        Sample vel = 1.0;
        Sample applied = -1000.0;
    };

    bool FMSynth::modulates(int a, int j, int k)
    {
        if (a < 1 || a > 8 || j < 0 || j >= kOps || k < 0 || k >= kOps) return false;
        return (kMods[a - 1][k] >> j) & 1u;
    }
    bool FMSynth::carrier(int a, int k) { return a >= 1 && a <= 8 && k >= 0 && k < kOps && ((kCarriers[a - 1] >> k) & 1u); }

    Sample FMSynth::sine(double cycles)
    {
        // (M6) a table read linearly
        const auto &t = table();
        const double x = (cycles - std::floor(cycles)) * kTable;
        const int i = (int)x;
        const double f = x - i;
        return t[(size_t)i] + f * (t[(size_t)i + 1] - t[(size_t)i]);
    }

    FMSynth::FMSynth()
    {
        table(); // made here, not on a host's audio thread
        mRamp.assign(4096, 0.0);
        for (int i = 0; i < kVoices; ++i) mVoices.push_back(std::make_unique<Voice>());
        setParams(mParams);
    }

    FMSynth::~FMSynth() = default;

    void FMSynth::applyTo(Voice &v)
    {
        for (int k = 0; k < kOps; ++k)
        {
            const Op &o = mParams.op[(size_t)k];
            v.env[k].setAttackMs(o.attack);
            v.env[k].setDecayMs(o.decay);
            v.env[k].setSustain(o.sustain);
            v.env[k].setReleaseMs(o.release);
        }
        if (v.note >= 0) { v.applied = -1000.0; retune(v); }
    }

    void FMSynth::setParams(const Params &p)
    {
        mParams = p;
        mParams.algorithm = std::clamp(mParams.algorithm, 1, 8);
        for (auto &o : mParams.op) o.ratio = std::clamp(o.ratio, (Sample)0.5, (Sample)16.0);
        for (auto &v : mVoices) applyTo(*v);
    }

    Sample FMSynth::modulation() const
    {
        const Sample fs = std::max((Sample)8000.0, (Sample)AudioConfig::instance().sampleRate());
        const Sample vib = mMod > 0.0 ? mMod * mParams.vibrato * std::sin(2.0 * kPi * mParams.vibratoRate * (Sample)mPos / fs) : 0.0;
        return mBend * mParams.bendRange + vib;
    }

    void FMSynth::retune(Voice &v)
    {
        // (M1) each operator's step from the note, bend and vibrato — re-set only when the pitch moved
        const Sample p = (Sample)v.note + modulation();
        if (p == v.applied) return;
        v.applied = p;
        const Sample fs = std::max((Sample)8000.0, (Sample)AudioConfig::instance().sampleRate()), f0 = midiHz(p);
        for (int k = 0; k < kOps; ++k)
        {
            const Op &o = mParams.op[(size_t)k];
            v.inc[k] = f0 * o.ratio * std::pow(2.0, o.fine / 1200.0) / fs;
        }
    }

    void FMSynth::noteOn(int note, int velocity)
    {
        if (note < 0 || note > 127) return;
        Voice *pick = nullptr;
        for (auto &v : mVoices)
            if (!v->active) { pick = v.get(); break; }
        if (!pick) // the oldest is stolen
            for (auto &v : mVoices)
                if (!pick || v->age < pick->age) pick = v.get();
        Voice &v = *pick;
        v.note = note;
        v.active = v.held = true;
        v.age = ++mClock;
        v.vel = std::clamp(velocity, 1, 127) / 127.0;
        for (int k = 0; k < kOps; ++k) v.phase[k] = 0.0; // (M1) key sync: every operator from 0
        v.fb1 = v.fb2 = 0.0;
        v.applied = -1000.0;
        retune(v);
        for (auto &e : v.env) e.noteOn(1.0);
    }

    void FMSynth::noteOff(int note)
    {
        for (auto &v : mVoices)
            if (v->active && v->held && v->note == note)
            {
                v->held = false;
                for (auto &e : v->env) e.noteOff();
            }
    }

    void FMSynth::allNotesOff()
    {
        for (auto &v : mVoices)
            if (v->active && v->held)
            {
                v->held = false;
                for (auto &e : v->env) e.noteOff();
            }
    }

    void FMSynth::reset()
    {
        for (auto &v : mVoices)
        {
            v->active = v->held = false;
            v->note = -1;
            for (auto &e : v->env) e.reset();
        }
        mPos = 0;
        mBend = mMod = 0.0;
        mVolGain = -1.0;
    }

    int FMSynth::activeVoices() const
    {
        int n = 0;
        for (const auto &v : mVoices) n += v->active ? 1 : 0;
        return n;
    }

    void FMSynth::pitchBend(Sample amount) { mBend = std::clamp(amount, (Sample)-1.0, (Sample)1.0); }
    void FMSynth::modWheel(Sample amount) { mMod = std::clamp(amount, (Sample)0.0, (Sample)1.0); }

    void FMSynth::render(Sample *const *out, int channels, int frames)
    {
        const int chans = AudioConfig::instance().channelCount();
        if ((int)mRamp.size() < frames) mRamp.assign((size_t)frames, 0.0);
        // the volume linear across the block from where the last ended (as BasicSynth's (Y4))
        const Sample vol = std::pow(10.0, mParams.volumeDb / 20.0);
        if (mVolGain < 0.0) mVolGain = vol;
        for (int i = 0; i < frames; ++i) mRamp[(size_t)i] = mVolGain + (vol - mVolGain) * (Sample)(i + 1) / (Sample)frames;
        mVolGain = vol;
        const int a = mParams.algorithm;
        int carriers = 0;
        for (int k = 0; k < kOps; ++k) carriers += carrier(a, k) ? 1 : 0;
        const Sample norm = 1.0 / std::max(1, carriers), fbk = mParams.feedback * kPi;
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
                Sample gainOf[kOps];
                for (int k = 0; k < kOps; ++k)
                {
                    const Op &o = mParams.op[(size_t)k];
                    gainOf[k] = o.level * (1.0 - o.velocity + o.velocity * v.vel);
                }
                for (int i = 0; i < n; ++i)
                {
                    // (M1)–(M2) operators 4 → 1, each hearing those above it
                    Sample y[kOps] = {0, 0, 0, 0};
                    for (int k = kOps - 1; k >= 0; --k)
                    {
                        Sample e = v.env[k].nextValue(0);
                        for (int c = 1; c < chans; ++c) v.env[k].nextValue(c); // every channel's state moves (FM is mono)
                        double beta = k == kOps - 1 ? fbk * 0.5 * (v.fb1 + v.fb2) : 0.0;
                        for (int j = k + 1; j < kOps; ++j)
                            if ((kMods[a - 1][k] >> j) & 1u) beta += kIndex * y[j];
                        y[k] = sine(v.phase[k] + beta / (2.0 * kPi)) * gainOf[k] * e;
                        v.phase[k] += v.inc[k];
                        v.phase[k] -= std::floor(v.phase[k]);
                    }
                    v.fb2 = v.fb1;
                    v.fb1 = y[kOps - 1];
                    Sample s = 0.0;
                    for (int k = 0; k < kOps; ++k)
                        if ((kCarriers[a - 1] >> k) & 1u) s += y[k];
                    s *= norm * mRamp[(size_t)(done + i)]; // (M4)
                    for (int c = 0; c < channels; ++c) out[c][done + i] += s;
                }
                // a voice ends when its carriers' envelopes have
                bool sounding = false;
                for (int k = 0; k < kOps; ++k)
                    if (carrier(a, k) && !v.env[k].isFinished()) sounding = true;
                if (!sounding && !v.held) v.active = false;
            }
            mPos += n;
            done += n;
        }
    }
}
