/*
 *  Arstro DSP Library — DrumMachine: ten synthesized pads on the General MIDI drum notes
 *  (REQ-drum-1…5).
 *
 *      36 kick · 37 rim · 38 snare · 39 clap · 41 low tom · 42 closed hat · 45 mid tom
 *      46 open hat · 48 high tom · 56 cowbell
 *
 *  Every pad is SYNTHESIZED — no sample files — from the library's primitives: `Phasor` (sweeping
 *  sines, metallic squares), `Noise` (seeded), `StateVariableFilter` (band/high/low-pass), and
 *  `DecayEnvelope` (struck-sound T60 envelopes). The models follow the classic analog machines
 *  (TR-808/909 topology): a kick is a sine whose pitch falls exponentially, a snare is two tuned
 *  sines plus filtered noise, a hat is six detuned squares through a band-pass and a high-pass.
 *  Each pad has tune, decay, tone, level and pan. The closed hat chokes the open hat, as on every
 *  drum machine. Each pad is monophonic: a re-hit restarts it.
 *
 *  Deterministic: every pad's noise is seeded from its index, and `reset()` reseeds, so the same
 *  pattern from a reset renders the same bytes. Math and every voicing constant:
 *  instrument/README.md ## DrumMachine.
 */
#pragma once
#include "Instrument.h"
#include "../generator/Noise.h"
#include "../generator/Phasor.h"
#include "../envelope/DecayEnvelope.h"
#include "../equalizer/StateVariableFilter.h"
#include <memory>

namespace arstro
{
    class DrumMachine : public Instrument
    {
    public:
        enum Pad { Kick = 0, Rim, Snare, Clap, LowTom, ClosedHat, MidTom, OpenHat, HighTom, Cowbell, PadCount };

        struct PadParams
        {
            Sample tune = 0.0;     // semitones, −12..+12
            Sample decay = 300.0;  // ms to −60 dB (each pad has its own default)
            Sample tone = 0.5;     // 0..1 — what it means is per pad (README)
            Sample levelDb = 0.0;  // −60..+6
            Sample pan = 0.0;      // −1..+1
        };

        DrumMachine();
        ~DrumMachine() override;

        static int noteFor(Pad p);
        /** The pad a note plays, or −1. */
        static int padFor(int note);
        static const char *padName(Pad p); // "kick", "rim", "snare", … — the registry's prefixes
        static PadParams defaults(Pad p);

        void setPad(Pad p, const PadParams &params);
        const PadParams &pad(Pad p) const;
        void setVolumeDb(Sample db) { mVolumeDb = db; }
        Sample volumeDb() const { return mVolumeDb; }

        void noteOn(int note, int velocity) override;
        void noteOff(int note) override; // drums ignore note-off (they ring out)
        void allNotesOff() override {}
        void reset() override;
        void render(Sample *const *out, int channels, int frames) override;
        int activeVoices() const override;

    private:
        struct Voice;
        Sample tick(Voice &v, Pad p, Sample invFs, Sample tuneRatio);
        std::unique_ptr<Voice> mVoices[PadCount];
        PadParams mPads[PadCount];
        Sample mVolumeDb = -6.0;
    };
}
