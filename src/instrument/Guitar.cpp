#include "Guitar.h"
#include "../base/AudioConfig.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        constexpr double kE2 = 82.40688922821748; // (GT2) the sustain's anchor: the guitar's lowest open string
        // (GT5) a flat-top's first three body modes: the air (Helmholtz) mode, the top plate's first and second.
        // Empirical — inside the ranges measured on steel-string guitars — not derived from a body's dimensions.
        struct Mode { double hz, t60, weight; };
        constexpr Mode kBodyModes[3] = {{100.0, 0.25, 1.5}, {200.0, 0.15, 2.0}, {400.0, 0.08, 1.2}};
    }

    Guitar::Guitar()
    {
        for (int k = 0; k < 3; ++k)
        {
            mBody[(size_t)k].setFrequencyHz(kBodyModes[k].hz);
            mBody[(size_t)k].setDecaySeconds(kBodyModes[k].t60);
        }
        setParams(mP);
        reset();
    }

    double Guitar::sustainAt(double hz) const
    {
        // (GT2) a higher string rings shorter: T60_1 = sustain·(f_E2/f0)^0.5 — tuned by ear against recorded guitars,
        // not derived (a real string's loss depends on its gauge, which this model does not carry)
        return mP.sustain * std::sqrt(kE2 / std::max(hz, 1.0));
    }

    void Guitar::setParams(const Params &p)
    {
        mP = p;
        mGain = std::pow(10.0, mP.volumeDb / 20.0);
        for (Voice &v : mVoices)
            if (v.on)
                tune(v);
    }

    void Guitar::tune(Voice &v)
    {
        // (GT1) the pitch, the bend included; (GT2) the string
        const double hz = 440.0 * std::pow(2.0, (v.note + mBend - 69.0) / 12.0);
        v.string.setFundamentalHz(hz);
        v.string.setInharmonicity(mP.stiffness * 1e-4);
        v.string.setBaseDecaySeconds(sustainAt(hz));
        v.string.setBrightnessDecaySeconds(mP.brightness);
        v.string.setStrikePosition(mP.position);
        v.string.setModalMass(1.0);
    }

    void Guitar::noteOn(int note, int velocity)
    {
        // (GT8) the same note re-plucks its string; else a free string; else the oldest is stolen
        Voice *v = nullptr;
        for (Voice &w : mVoices)
            if (w.on && w.note == note) v = &w;
        if (!v)
            for (Voice &w : mVoices)
                if (!w.on) { v = &w; break; }
        if (!v)
        {
            v = &mVoices[0];
            for (Voice &w : mVoices)
                if (w.age < v->age) v = &w;
        }
        v->note = note;
        v->on = v->held = true;
        v->age = ++mPlucks;
        v->t = 0;
        v->peak = 0.0;
        tune(*v);
        v->string.reset();                    // the damper lifted …
        v->string.setDamperEngageMs(5.0);
        v->string.setDamperEngagement(mP.mute); // (GT4) … but for the palm, if it rests on the strings
        // (GT3) the pluck: its strength by the velocity, its brightness too — a harder pluck leaves the pick faster
        const double vel = std::clamp(velocity, 1, 127) / 127.0;
        const double strength = 1.0 - mP.velocity + mP.velocity * vel;
        v->string.pluck(0.25 * strength, mP.pick * (0.5 + 0.5 * vel));
        mBodyOn = true;
        mBodyT = 0;
        mBodyPeak = 0.0;
    }

    void Guitar::noteOff(int note)
    {
        for (Voice &v : mVoices)
            if (v.on && v.held && v.note == note)
            {
                v.held = false;                       // (GT4) the finger lifts off the fret
                v.string.setDamperEngageMs(std::max(1.0, mP.release));
                v.string.setDamperEngagement(1.0);
            }
    }

    void Guitar::allNotesOff()
    {
        for (Voice &v : mVoices)
            if (v.on && v.held) noteOff(v.note);
    }

    void Guitar::endVoice(Voice &v)
    {
        v.on = v.held = false;
        v.note = -1;
        v.string.silence(); // nothing left, so a host that skips a silent instrument hears what one that does not hears
    }

    void Guitar::reset()
    {
        for (Voice &v : mVoices)
            endVoice(v);
        for (StringResonator &b : mBody)
            b.reset();
        mPlucks = 0;
        mBodyOn = false;
        mBodyPeak = 0.0;
        mBodyT = 0;
    }

    void Guitar::pitchBend(Sample amount)
    {
        // (GT1) at once: the host splits its blocks at events (Solaris R-DSP-5), so a bend lands on the same sample
        // however time is cut — and nothing else moves the pitch inside a block, so no control grid is needed
        const double b = std::clamp((double)amount, -1.0, 1.0) * mP.bendRange;
        if (b == mBend) return;
        mBend = b;
        for (Voice &v : mVoices)
            if (v.on) tune(v);
    }

    int Guitar::activeVoices() const
    {
        int n = 0;
        for (const Voice &v : mVoices)
            n += v.on ? 1 : 0;
        return n > 0 ? n : (mBodyOn ? 1 : 0); // the body's tail is sound too
    }

    void Guitar::render(Sample *const *out, int channels, int frames)
    {
        for (int i = 0; i < frames; ++i)
        {
            double y = 0.0;
            for (Voice &v : mVoices)
            {
                if (!v.on) continue;
                const double s = v.string.out(0.0, 0);
                y += s;
                // (GT7) a voice ends when a whole window of its own samples stays under −100 dB
                v.peak = std::max(v.peak, std::fabs(s));
                if (++v.t % kEndWindow == 0)
                {
                    if (v.peak < kSilent) endVoice(v);
                    else v.peak = 0.0;
                }
            }
            // (GT5) the body, shared, driven by every string at once
            double b = 0.0;
            if (mBodyOn)
            {
                for (int k = 0; k < 3; ++k)
                    b += kBodyModes[k].weight * mBody[(size_t)k].out(y, 0);
                mBodyPeak = std::max(mBodyPeak, std::fabs(b) + std::fabs(y));
                if (++mBodyT % kEndWindow == 0)
                {
                    bool any = false;
                    for (const Voice &v : mVoices) any = any || v.on;
                    if (!any && mBodyPeak < kSilent)
                    {
                        mBodyOn = false; // and its modes zeroed, so a skipped host and a running one agree
                        for (StringResonator &m : mBody) m.reset();
                    }
                    mBodyPeak = 0.0;
                }
            }
            // (GT6) mono, onto every channel
            const Sample o = (Sample)(mGain * (y + mP.body * b));
            for (int c = 0; c < channels; ++c)
                out[c][i] += o;
        }
    }
}
