#include "GranularSampler.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        constexpr double kTau = 6.283185307179586;
    }

    GranularSampler::GranularSampler()
    {
        for (int i = 0; i < kVoices; ++i) mVoices.push_back(std::make_unique<Voice>());
        setParams(mP);
    }

    void GranularSampler::setParams(const Params &p)
    {
        mP = p;
        mP.size = std::clamp(mP.size, 5.0, 500.0);
        mP.density = std::clamp(mP.density, 1.0, 200.0);
        for (auto &v : mVoices)
        {
            v->env.setAttackMs(mP.attack);
            v->env.setDecayMs(mP.decay);
            v->env.setSustain(mP.sustain);
            v->env.setReleaseMs(mP.release);
        }
    }

    void GranularSampler::setSample(const float *x, long long frames, int channels, double rate)
    {
        const int c = std::max(1, channels);
        mFrames = x ? std::max(0LL, frames) : 0;
        mRate = rate > 0 ? rate : 48000.0;
        mL.resize((size_t)mFrames);
        mR.resize((size_t)mFrames);
        for (long long i = 0; i < mFrames; ++i)
        {
            mL[(size_t)i] = x[(size_t)(i * c)];
            mR[(size_t)i] = x[(size_t)(i * c + (c > 1 ? 1 : 0))];
        }
        reset();
    }

    long long GranularSampler::grainsStarted(int v) const
    {
        return v >= 0 && v < (int)mVoices.size() && mVoices[(size_t)v]->active ? mVoices[(size_t)v]->started : -1;
    }

    double GranularSampler::nextDraw(Voice &v) const
    {
        // a draw in [−1, 1] from the voice's counter (SplitMix64) — deterministic, never a clock
        unsigned long long z = (++v.draw) * 0x9E3779B97F4A7C15ULL + (unsigned long long)v.note * 0xD1B54A32D192ED03ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return (double)((z ^ (z >> 31)) >> 11) / 4503599627370496.0 - 1.0;
    }

    void GranularSampler::spawn(Voice &v)
    {
        // (G2) a grain: its place (the position, scanned, sprayed), its pitch (sprayed), its pan
        Grain *g = nullptr;
        for (auto &x : v.grains)
            if (!x.on) { g = &x; break; }
        if (!g) // the oldest (the furthest along) gives way
        {
            g = &v.grains[0];
            for (auto &x : v.grains)
                if (x.k > g->k) g = &x;
        }
        const double fs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        const double t = (double)v.clock / fs;
        const double u = nextDraw(v), u2 = nextDraw(v), u3 = nextDraw(v);
        const double place = std::clamp(mP.position + mP.scan * t + mP.spray * u, 0.0, 1.0);
        g->on = true;
        g->k = 0;
        g->length = std::max(2LL, (long long)std::llround(mP.size * fs / 1000.0));
        g->pos = place * (double)std::max(0LL, mFrames - 1);
        g->step = std::pow(2.0, ((double)(v.note - mP.root) + mP.pitch + mP.pspray * u2 / 100.0 + mBend * mBendRange) / 12.0) * mRate / fs;
        const double q = std::clamp(mP.spread, 0.0, 1.0) * u3;
        g->gl = std::min(1.0, 1.0 - q);
        g->gr = std::min(1.0, 1.0 + q);
        ++v.started;
    }

    void GranularSampler::noteOn(int note, int velocity)
    {
        if (note < 0 || note > 127) return;
        Voice *pick = nullptr;
        for (auto &v : mVoices)
            if (!v->active) { pick = v.get(); break; }
        if (!pick)
            for (auto &v : mVoices)
                if (!pick || v->age < pick->age) pick = v.get();
        Voice &v = *pick;
        v.active = v.held = true;
        v.note = note;
        v.vel = std::clamp(velocity, 1, 127) / 127.0;
        v.age = ++mAge;
        v.clock = 0;
        v.started = 0;
        v.draw = 0;
        for (auto &g : v.grains) g.on = false;
        v.env.reset();
        v.env.noteOn(1.0);
    }

    void GranularSampler::noteOff(int note)
    {
        for (auto &v : mVoices)
            if (v->active && v->held && v->note == note) { v->held = false; v->env.noteOff(); }
    }

    void GranularSampler::allNotesOff()
    {
        for (auto &v : mVoices)
            if (v->active && v->held) { v->held = false; v->env.noteOff(); }
    }

    void GranularSampler::reset()
    {
        for (auto &v : mVoices)
        {
            v->active = v->held = false;
            for (auto &g : v->grains) g.on = false;
            v->env.reset();
        }
        mBend = 0.0;
    }

    int GranularSampler::activeVoices() const
    {
        int n = 0;
        for (const auto &v : mVoices) n += v->active ? 1 : 0;
        return n;
    }

    void GranularSampler::pitchBend(Sample amount) { mBend = std::clamp((double)amount, -1.0, 1.0); } // the next grains

    void GranularSampler::render(Sample *const *out, int channels, int frames)
    {
        if (mFrames < 2) return;
        const int chans = AudioConfig::instance().channelCount();
        const double fs = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        const double every = fs / mP.density; // (G1) samples between grains
        const double norm = 1.0 / std::max(1.0, 0.5 * mP.density * mP.size / 1000.0); // (G4)
        const double vol = std::pow(10.0, mP.volumeDb / 20.0);
        auto at = [&](const std::vector<float> &x, long long i) { return i < 0 || i >= mFrames ? 0.0 : (double)x[(size_t)i]; };
        for (auto &vp : mVoices)
        {
            Voice &v = *vp;
            if (!v.active) continue;
            const double gain = vol * norm * (1.0 - mP.velocity + mP.velocity * v.vel);
            for (int i = 0; i < frames; ++i)
            {
                // (G1) a grain on the schedule counted from the note, while it is held or rings
                if ((double)v.clock >= std::floor((double)v.started * every)) spawn(v);
                double l = 0.0, r = 0.0;
                for (auto &g : v.grains)
                {
                    if (!g.on) continue;
                    // (G3) Hann-windowed, read between frames (Catmull-Rom)
                    const double w = 0.5 * (1.0 - std::cos(kTau * (double)g.k / (double)g.length));
                    const long long k = (long long)std::floor(g.pos);
                    const double t = g.pos - (double)k;
                    auto cr = [&](const std::vector<float> &x) {
                        const double y0 = at(x, k - 1), y1 = at(x, k), y2 = at(x, k + 1), y3 = at(x, k + 2);
                        return y1 + 0.5 * t * (y2 - y0) + t * t * (y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3) + 0.5 * t * t * t * (3.0 * (y1 - y2) + y3 - y0);
                    };
                    l += cr(mL) * w * g.gl;
                    r += cr(mR) * w * g.gr;
                    g.pos += g.step;
                    if (++g.k >= g.length) g.on = false;
                }
                const double e = v.env.nextValue(0) * gain;
                for (int c = 1; c < chans; ++c) v.env.nextValue(c);
                if (channels > 0) out[0][i] += (Sample)(l * e);
                if (channels > 1) out[1][i] += (Sample)(r * e);
                ++v.clock;
            }
            if (!v.held && v.env.isFinished()) v.active = false;
        }
    }
}
