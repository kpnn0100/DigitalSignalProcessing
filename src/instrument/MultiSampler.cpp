#include "MultiSampler.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    MultiSampler::MultiSampler()
    {
        for (int i = 0; i < kVoices; ++i) mVoices.push_back(std::make_unique<Voice>());
    }

    void MultiSampler::bind()
    {
        for (auto &r : mRegions)
        {
            auto it = mSounds.find(r.def.sample);
            r.sound = it == mSounds.end() ? nullptr : it->second.get();
        }
    }

    void MultiSampler::setDefinition(const SfzInstrument &sfz)
    {
        reset();
        mRegions.clear();
        for (const auto &d : sfz.regions)
        {
            Region r;
            r.def = d;
            mRegions.push_back(r);
        }
        bind();
    }

    void MultiSampler::setSound(const std::string &name, const float *x, long long frames, int channels, double rate)
    {
        auto s = std::make_unique<Sound>();
        const int c = std::max(1, channels);
        s->frames = x ? std::max(0LL, frames) : 0;
        s->rate = rate > 0 ? rate : 48000.0;
        s->l.resize((size_t)s->frames);
        s->r.resize((size_t)s->frames);
        for (long long i = 0; i < s->frames; ++i)
        {
            s->l[(size_t)i] = x[(size_t)(i * c)];
            s->r[(size_t)i] = x[(size_t)(i * c + (c > 1 ? 1 : 0))]; // mono on both sides
        }
        mSounds[name] = std::move(s);
        bind();
    }

    int MultiSampler::voiceRegion(int v) const
    {
        return v >= 0 && v < (int)mVoices.size() && mVoices[(size_t)v]->active ? mVoices[(size_t)v]->region : -1;
    }

    double MultiSampler::stepFor(const Region &r, int note) const
    {
        // (Z3) the region's pitch — the note against its root, its transpose, tune and the bend — at the sound's rate
        const double out = std::max(8000.0, (double)AudioConfig::instance().sampleRate());
        const double semis = (note - r.def.keycenter + r.def.transpose) + r.def.tune / 100.0 + mBend * mP.bendRange;
        return std::pow(2.0, semis / 12.0) * (r.sound ? r.sound->rate : out) / out;
    }

    void MultiSampler::start(Voice &v, int region, int note, int velocity)
    {
        const Region &r = mRegions[(size_t)region];
        v.active = v.held = true;
        v.note = note;
        v.region = region;
        v.age = ++mAge;
        v.pos = (double)std::max(0LL, r.def.offset);
        v.step = stepFor(r, note);
        // (Z5) its level: volume, velocity (squared, by the sensitivity), the balance pan
        const double vel = std::clamp(velocity, 1, 127) / 127.0, t = std::clamp(mP.velocity, 0.0, 1.0);
        const double g = std::pow(10.0, r.def.volume / 20.0) * (1.0 - t + t * vel * vel);
        v.gl = g * std::min(1.0, 1.0 - r.def.pan / 100.0);
        v.gr = g * std::min(1.0, 1.0 + r.def.pan / 100.0);
        v.env.setAttackMs(r.def.attack * 1000.0);
        v.env.setDecayMs(r.def.decay * 1000.0);
        v.env.setSustain(r.def.sustain / 100.0);
        v.env.setReleaseMs(r.def.release * 1000.0);
        v.env.reset();
        v.env.noteOn(1.0);
    }

    void MultiSampler::noteOn(int note, int velocity)
    {
        if (note < 0 || note > 127) return;
        // (Z2) a draw for the chance, one a note-on — from a counter (deterministic)
        unsigned long long z = (++mDraw) * 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        const double draw = (double)((z ^ (z >> 31)) >> 11) / 9007199254740992.0;
        for (int i = 0; i < (int)mRegions.size(); ++i)
        {
            Region &r = mRegions[(size_t)i];
            const SfzRegion &d = r.def;
            if (note < d.lokey || note > d.hikey || velocity < d.lovel || velocity > d.hivel) continue;
            if (draw < d.lorand || draw >= d.hirand) continue;
            const long long c = ++r.count;
            if ((c - 1) % d.seqLength != d.seqPosition - 1) continue; // not its turn
            if (!r.sound || r.sound->frames <= 0) continue;
            // (Z6) it chokes the voices that are off by its group
            if (d.group != 0)
                for (auto &v : mVoices)
                    if (v->active && mRegions[(size_t)v->region].def.offBy == d.group)
                    {
                        v->env.setReleaseMs(6.0);
                        v->env.noteOff();
                        v->held = false;
                    }
            Voice *pick = nullptr;
            for (auto &v : mVoices)
                if (!v->active) { pick = v.get(); break; }
            if (!pick)
                for (auto &v : mVoices)
                    if (!pick || v->age < pick->age) pick = v.get();
            start(*pick, i, note, velocity);
        }
    }

    void MultiSampler::noteOff(int note)
    {
        for (auto &v : mVoices)
            if (v->active && v->held && v->note == note)
            {
                v->held = false;
                if (mRegions[(size_t)v->region].def.loop != SfzRegion::OneShot) v->env.noteOff(); // one_shot plays on (Z4)
            }
    }

    void MultiSampler::allNotesOff()
    {
        for (auto &v : mVoices)
            if (v->active && v->held)
            {
                v->held = false;
                if (mRegions[(size_t)v->region].def.loop != SfzRegion::OneShot) v->env.noteOff();
            }
    }

    void MultiSampler::reset()
    {
        for (auto &v : mVoices)
        {
            v->active = v->held = false;
            v->env.reset();
        }
        for (auto &r : mRegions) r.count = 0;
        mDraw = 0;
        mBend = 0.0;
    }

    int MultiSampler::activeVoices() const
    {
        int n = 0;
        for (const auto &v : mVoices) n += v->active ? 1 : 0;
        return n;
    }

    void MultiSampler::pitchBend(Sample amount)
    {
        mBend = std::clamp((double)amount, -1.0, 1.0);
        for (auto &v : mVoices)
            if (v->active) v->step = stepFor(mRegions[(size_t)v->region], v->note);
    }

    void MultiSampler::render(Sample *const *out, int channels, int frames)
    {
        const int chans = AudioConfig::instance().channelCount();
        const double vol = std::pow(10.0, mP.volumeDb / 20.0);
        for (auto &vp : mVoices)
        {
            Voice &v = *vp;
            if (!v.active) continue;
            const Region &r = mRegions[(size_t)v.region];
            const Sound &s = *r.sound;
            const SfzRegion &d = r.def;
            const long long last = d.end >= 0 ? std::min(d.end, s.frames - 1) : s.frames - 1;
            const long long ls = std::clamp(d.loopStart, 0LL, last), le = d.loopEnd >= 0 ? std::min(d.loopEnd, last) : last;
            const bool loops = d.loop == SfzRegion::LoopContinuous || (d.loop == SfzRegion::LoopSustain && v.held);
            auto at = [&](const std::vector<float> &x, long long i) { return (double)x[(size_t)std::clamp(i, 0LL, s.frames - 1)]; };
            for (int i = 0; i < frames; ++i)
            {
                // (Z4) a loop goes back by its length; past the end it is over
                if (loops && le > ls && v.pos > (double)le) v.pos -= (double)(le - ls + 1);
                if (v.pos > (double)last) { v.active = false; break; }
                // Catmull-Rom between frames (as Wavetable (W3))
                const long long k = (long long)v.pos;
                const double t = v.pos - (double)k;
                auto cr = [&](const std::vector<float> &x) {
                    const double y0 = at(x, k - 1), y1 = at(x, k), y2 = at(x, k + 1), y3 = at(x, k + 2);
                    return y1 + 0.5 * t * (y2 - y0) + t * t * (y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3) + 0.5 * t * t * t * (3.0 * (y1 - y2) + y3 - y0);
                };
                const double e = v.env.nextValue(0);
                for (int c = 1; c < chans; ++c) v.env.nextValue(c);
                const double gL = v.gl * e * vol, gR = v.gr * e * vol;
                if (channels > 0) out[0][i] += (Sample)(cr(s.l) * gL);
                if (channels > 1) out[1][i] += (Sample)(cr(s.r) * gR);
                v.pos += v.step;
            }
            if (v.active && v.env.isFinished()) v.active = false;
        }
    }
}
