/*
 *  Arstro DSP Library — MultiSampler: a recorded instrument of many sounds, each a region of the keyboard and of
 *  velocity, read from SFZ (REQ-multisample-1, -3). A note plays every region it falls in — in turn (round robin), by
 *  chance, a region choking others — each its own sound, root, tuning, loop and envelope. instrument/README.md
 *  ## MultiSampler and SFZ (Z2)–(Z6).
 *
 *  The library never opens a file: a host gives the definition (`setDefinition`, the SFZ's regions) and each sound it
 *  names (`setSound`, copied) — both allocate, so neither on an audio thread. 32 voices, the oldest stolen;
 *  deterministic (the chance draws from a counter, never a clock); no allocation once given.
 */
#pragma once
#include "Instrument.h"
#include "Sfz.h"
#include "../envelope/ADSREnvelope.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
    class MultiSampler : public Instrument
    {
    public:
        static constexpr int kVoices = 32;
        struct Params
        {
            double volumeDb = 0.0;
            double velocity = 1.0;   // t: SFZ's amp_veltrack as a fraction (Z5)
            double bendRange = 2.0;  // semitones at full bend
        };
        MultiSampler();
        void setParams(const Params &p) { mP = p; }
        const Params &params() const { return mP; }
        /** The regions (from parseSfz). Sounds named before stay. */
        void setDefinition(const SfzInstrument &sfz);
        /** A sound a region names: `frames` frames of `channels` interleaved floats recorded at `rate` Hz — copied. */
        void setSound(const std::string &name, const float *interleaved, long long frames, int channels, double rate);
        int regions() const { return (int)mRegions.size(); }
        /** The region voice `v` plays (−1 none) — for tests. */
        int voiceRegion(int v) const;

        void noteOn(int note, int velocity) override;
        void noteOff(int note) override;
        void allNotesOff() override;
        void reset() override;
        void render(Sample *const *out, int channels, int frames) override;
        int activeVoices() const override;
        void pitchBend(Sample amount) override;

    private:
        struct Sound
        {
            std::vector<float> l, r;
            long long frames = 0;
            double rate = 48000.0;
        };
        struct Region
        {
            SfzRegion def;
            const Sound *sound = nullptr;
            long long count = 0;     // (Z2) notes that reached it
        };
        struct Voice
        {
            bool active = false, held = false;
            int note = -1, region = -1;
            double pos = 0.0, step = 1.0, gl = 1.0, gr = 1.0;
            long long age = 0;
            ADSREnvelope env;
        };
        void bind();
        void start(Voice &v, int region, int note, int velocity);
        double stepFor(const Region &r, int note) const;
        Params mP;
        std::vector<Region> mRegions;
        std::map<std::string, std::unique_ptr<Sound>> mSounds;
        std::vector<std::unique_ptr<Voice>> mVoices;
        long long mAge = 0;
        unsigned long long mDraw = 0; // (Z2) the chance's counter
        double mBend = 0.0;
    };
}
