/*
 *  Arstro DSP Library — FMSynth: four sine operators by phase modulation (REQ-fm-1…3).
 *
 *  Each operator is a sine whose phase the operators above it move (an ALGORITHM — the TX81Z's eight), each with its
 *  own ADSR and level; operator 4 may feed itself back. The carriers are heard, summed. Math and constants:
 *  instrument/README.md ## FMSynth (M1)–(M6).
 *
 *  Pitch (bend, vibrato) is re-set on the absolute 32-sample grid counted from `reset`, as BasicSynth's (Y7) — so a
 *  render is the same however a host chops time. Deterministic; no allocation after construction.
 */
#pragma once
#include "Instrument.h"
#include "../envelope/ADSREnvelope.h"
#include <array>
#include <memory>
#include <vector>

namespace arstro
{
    class FMSynth : public Instrument
    {
    public:
        struct Op
        {
            Sample ratio = 1.0;      // r_k: 0.5 … 16 times the note
            Sample fine = 0.0;       // c_k: cents
            Sample level = 1.0;      // L_k: 0 … 1 (a modulator's: its index, a carrier's: its loudness)
            Sample attack = 2.0, decay = 400.0, sustain = 0.6, release = 300.0; // ms, ms, 0 … 1, ms
            Sample velocity = 0.7;   // s_k: 0 … 1
        };
        struct Params
        {
            int algorithm = 1;       // 1 … 8 (M3)
            Sample feedback = 0.0;   // F: 0 … 1 — operator 4 on itself (M2)
            std::array<Op, 4> op = {Op{1.0, 0.0, 1.0, 2.0, 400.0, 0.6, 300.0, 0.7}, Op{1.0, 0.0, 0.6, 2.0, 600.0, 0.3, 300.0, 0.7},
                                    Op{2.0, 0.0, 0.3, 2.0, 600.0, 0.3, 300.0, 0.7}, Op{3.5, 0.0, 0.2, 2.0, 600.0, 0.3, 300.0, 0.7}};
            Sample volumeDb = -12.0; // −60 … +6
            Sample bendRange = 2.0;  // semitones at full bend
            Sample vibrato = 0.5;    // semitones at full mod wheel
            Sample vibratoRate = 5.5; // Hz
        };

        static constexpr int kVoices = 16;
        static constexpr int kOps = 4;
        static constexpr int kControl = 32;       // samples between pitch updates, from `reset`
        static constexpr double kIndex = 8.0;     // (M2) I: radians a modulator at level 1 swings its carrier

        FMSynth();
        ~FMSynth() override;
        void setParams(const Params &p);
        const Params &params() const { return mParams; }

        void noteOn(int note, int velocity) override;
        void noteOff(int note) override;
        void allNotesOff() override;
        void reset() override;
        void render(Sample *const *out, int channels, int frames) override;
        int activeVoices() const override;
        void pitchBend(Sample amount) override;   // −1 … +1: ± bendRange semitones
        void modWheel(Sample amount) override;    // 0 … 1: the vibrato's depth

        /** (M3) does operator j (0-based) modulate operator k in algorithm a (1 … 8)? Is k heard? */
        static bool modulates(int a, int j, int k);
        static bool carrier(int a, int k);
        /** (M6) the sine of a phase in cycles, by the table. */
        static Sample sine(double cycles);

    private:
        struct Voice;
        void retune(Voice &v);
        void applyTo(Voice &v);
        Sample modulation() const;
        std::vector<std::unique_ptr<Voice>> mVoices;
        std::vector<Sample> mRamp;
        Params mParams;
        long mClock = 0;
        long long mPos = 0;
        Sample mBend = 0.0, mMod = 0.0, mVolGain = -1.0;
    };
}
