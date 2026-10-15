#include "Piano.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    Piano::Piano() { setParams(mP); }

    void Piano::setParams(const Params &p)
    {
        for (int i = 0; i < PianoEngine::TuneCount; ++i)
            if (p.tune[(size_t)i] != mEngine.tuningValue(i)) mEngine.setTuning(i, p.tune[(size_t)i]);
        if (p.sustain != mP.sustain) mEngine.setSustainPedal(p.sustain);
        if (p.soft != mP.soft) mEngine.setUnaCorda(p.soft);
        mP = p;
        mGain = std::pow(10.0, mP.volumeDb / 20.0); // (P2)
    }

    void Piano::noteOn(int note, int velocity)
    {
        if (note < 0 || note > 127) return;
        mEngine.noteOnMidi(note, (Sample)std::clamp(velocity, 1, 127) / 127.0); // (P1)
    }

    void Piano::noteOff(int note) { mEngine.noteOff(note); }
    void Piano::allNotesOff() { mEngine.releaseAll(); } // (P3): the pedals stay
    void Piano::reset() { mEngine.silence(); }           // (P3)
    void Piano::render(Sample *const *out, int channels, int frames) { mEngine.renderAdd(out, channels, frames, mGain); }
    int Piano::activeVoices() const { return mEngine.activeVoices(); }
}
