/*
 *  Arstro DSP Library — Sampler: a recorded sound played by notes (REQ-inst-sampler-1; Solaris R-EDM-8).
 *
 *  Chromatic: a note plays the sound pitched by its distance from the root key (an octave up = twice
 *  the rate). One-shot: every note plays it as recorded, to its end, whatever the note-off — a drum
 *  hit. A span (start, end) of the file, optionally reversed; an amplitude ADSR per voice (the
 *  library's ADSREnvelope); velocity → level by a sensitivity; 16 voices, the same note retriggering,
 *  else an idle voice, else the oldest stolen. README §Sampler, (S1)–(S5).
 *
 *  The library never opens a file: the HOST decodes and hands the frames in (`setSample`, a copy —
 *  it allocates, so never on the audio thread). Composition: pitch by resampling (linear
 *  interpolation of the recorded frames) — the library has no resampler to reuse; the envelope is
 *  ADSREnvelope, not a copy of one.
 */
#pragma once
#include "Instrument.h"
#include "../envelope/ADSREnvelope.h"
#include <memory>
#include <vector>

namespace arstro
{
    class Sampler : public Instrument
    {
    public:
        static constexpr int kVoices = 16;
        enum Mode { Chromatic = 0, OneShot = 1 };
        struct Params
        {
            Mode mode = Chromatic;
            int root = 60;               // the key that plays the sound as recorded
            double start = 0.0, end = 1.0; // the span, as fractions of the sound
            bool reverse = false;
            double attackMs = 0.0, decayMs = 0.0, sustain = 1.0, releaseMs = 30.0;
            double levelDb = 0.0;
            double velocity = 1.0;       // sensitivity: 0 = every note full, 1 = level ∝ velocity
        };

        Sampler();
        void setParams(const Params &p);
        const Params &params() const { return mP; }
        /** The sound: `frames` frames of `channels` interleaved floats recorded at `rate` Hz — copied.
         *  Mono plays on both sides. nullptr / 0 frames = no sound (every note is silence). */
        void setSample(const float *interleaved, long long frames, int channels, double rate);
        long long sampleFrames() const { return mFrames; }

        void noteOn(int note, int velocity) override;
        void noteOff(int note) override;
        void allNotesOff() override;
        void reset() override;
        void render(Sample *const *out, int channels, int frames) override;
        int activeVoices() const override;

    private:
        struct Voice
        {
            bool active = false, held = false;
            int note = -1;
            double pos = 0.0, inc = 0.0, gain = 1.0;
            long long age = 0;
            ADSREnvelope env;
        };
        void spanOf(long long &a, long long &b) const;
        void applyEnvelope(Voice &v) const;

        Params mP;
        std::vector<float> mL, mR;
        long long mFrames = 0;
        double mRate = 48000.0;
        std::vector<std::unique_ptr<Voice>> mVoices;
        long long mAge = 0;
    };
}
