/*
 *  Arstro DSP — piano_demo
 *
 *  PianoEngine: fixed-size polyphony pool of PianoVoice sharing one PianoBridge,
 *  with MIDI note on/off and the three pedals (REQ-piano-8, REQ-piano-10). Mirrors
 *  src/synth/VoiceManager's fixed-array + oldest-steals-first allocation pattern,
 *  but VoiceManager is hardcoded to Oscillator-owning Voice objects and has no
 *  concept of a shared cross-voice coupling bus (see src/physical/README.md "why
 *  new classes") — a piano-specific engine composes PianoVoice + PianoBridge
 *  directly rather than editing VoiceManager for a shape it isn't built for.
 */
#pragma once
#include "../../src/synth_dsp.h"
#include <array>
#include <cstdint>
#include <vector>

namespace arstro
{
    class PianoEngine
    {
    public:
        static constexpr int kVoiceCount = 8; // matches VoiceManager's existing convention

        PianoEngine();

        void noteOnMidi(int midiNote, Sample velocity);
        void noteOff(int midiNote);
        void panic(); // all notes off, pedals cleared

        void setSustainPedal(bool on);
        void setSostenutoPedal(bool on);
        void setUnaCorda(bool on);

        // ─────────────────── live voicing/tuning controls ───────────────────
        // A generic parameter bank so a host UI can build one control per entry
        // from the metadata alone (name/range/default) without hard-coding the
        // list. Multiplier params (Mul) scale the per-note register default so the
        // keyboard's natural scaling is preserved; the rest are absolute. Neutral
        // defaults reproduce the shipped model exactly. Applied on the audio thread
        // (PianoEngine is single-threaded; the host queues changes — see PianoApp).
        enum Tune
        {
            TuneHardness = 0,   // hammer felt stiffness ×  (voicing: soft ↔ bright)
            TuneFeltCurve,      // hammer nonlinear exponent p (felt hardness curve)
            TuneFeltHysteresis, // ε_branch load/unload asymmetry (README ## 6)
            TuneFeltRelax,      // Stulov relaxation depth ×  (rate-dependent loss, ## 6)
            TuneDecay,          // string T60 ×  (sustain length)
            TuneBrightness,     // T60 at 5 kHz (how fast the highs die)
            TuneInharmonicity,  // stiffness/inharmonicity ×  (metallic stretch)
            TuneUnisonDetune,   // unison detune in cents (chorus/beating)
            TuneBassGrowl,      // longitudinal tension coupling κ (phantom partials, ## 12)
            TuneAttackGlide,    // tension-modulation κ_t ×  (attack pitch glide, ## 12.5)
            TuneTrebleShimmer,  // duplex/aliquot drive (## 8.1)
            TuneAftersound,     // horizontal-polarisation share (README ## 5b, M11): note sings
            TuneBody,           // soundboard series mix (README ## 8.2, M10): 0 raw ↔ 1 through-board
            TuneBodyResonance,  // soundboard modal-resonance level (bridge radiationGain)
            TuneMasterGain,     // output level into the soft limiter
            TuneCount
        };
        struct TuneSpec { const char *name; double min, max, def; const char *unit; };
        static TuneSpec tuneSpec(int param);        // metadata for control `param` in [0,TuneCount)
        void setTuning(int param, double value);    // set one param, applied live to all voices
        double tuningValue(int param) const;         // current value (for control initialisation)

        // Renders `frames` stereo samples as interleaved 16-bit PCM, appended to `out`.

        // Renders `frames` stereo samples as interleaved 16-bit PCM, appended to `out`.
        void renderBlockBytes(std::vector<uint8_t> &out, int frames);

        // Renders `frames` stereo samples straight into a caller-owned interleaved
        // float buffer. Preferred on a live audio thread: it allocates nothing, and
        // it skips the round trip through 16-bit PCM that renderBlockBytes() does
        // (a live float sink would only have to convert straight back, losing
        // resolution on the way through for no reason).
        void renderBlockFloat(float *interleaved, int frames);

    private:
        void renderFrame(Sample &outL, Sample &outR); // one sample, both paths
        int allocateVoice();
        void refreshDamperHeld(int i);
        static Sample midiToHz(int note);

        std::array<PianoVoice, kVoiceCount> mVoices;
        PianoBridge mBridge;

        std::array<int, kVoiceCount> mNote;         // last assigned MIDI note, -1 if never used
        std::array<bool, kVoiceCount> mKeyDown;
        std::array<bool, kVoiceCount> mSostenutoLatched;
        std::array<long, kVoiceCount> mAge;

        bool mSustainPedal = false;
        bool mSostenutoPedal = false;
        bool mUnaCorda = false;

        // Live voicing state (see Tune). Values mirror the shipped defaults so a
        // fresh engine sounds identical until a control is moved.
        std::array<double, TuneCount> mTune;
        Sample mMasterGain = 0.22; // README (PianoEngine): the soft-limiter drive, now tunable
        void applyTuningToVoice(int i); // (re)apply mTune to voice i at its current pitch
        static Sample defaultInharmonicity(Sample f0Hz); // mirrors PianoVoice's pitch curve
    };
}
