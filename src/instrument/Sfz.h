/*
 *  Arstro DSP Library — SFZ: a multi-sample instrument's text read into regions (REQ-multisample-2).
 *
 *  Text in, regions out: the library never opens a file — a host reads the .sfz and, after `parseSfz`, decodes the
 *  sounds its regions name (relative to the .sfz's folder). instrument/README.md ## MultiSampler and SFZ (Z1).
 */
#pragma once
#include <string>
#include <vector>

namespace arstro
{
    struct SfzRegion
    {
        enum Loop { NoLoop = 0, OneShot, LoopContinuous, LoopSustain };
        std::string sample;             // as written, default_path before it
        int lokey = 0, hikey = 127, keycenter = 60;
        int lovel = 1, hivel = 127;
        double lorand = 0.0, hirand = 1.0;
        int seqLength = 1, seqPosition = 1;
        double tune = 0.0;              // cents
        int transpose = 0;              // semitones
        double volume = 0.0;            // dB
        double pan = 0.0;               // −100 … 100
        long long offset = 0, end = -1; // frames; −1: the sound's last
        Loop loop = NoLoop;
        long long loopStart = 0, loopEnd = -1;
        double attack = 0.0, decay = 0.0, sustain = 100.0, release = 0.03; // s, s, %, s
        int group = 0, offBy = 0;
    };

    struct SfzInstrument
    {
        std::vector<SfzRegion> regions;
        std::vector<std::string> problems; // what was not understood (an unknown opcode, a bad value) — said, not fatal
    };

    /** (Z1) the text's regions, every opcode inherited from its group and the global. */
    SfzInstrument parseSfz(const std::string &text);
    /** The sounds the regions name, each once, in the order first named — what a host decodes. */
    std::vector<std::string> sfzSounds(const SfzInstrument &sfz);
    /** A key, as SFZ writes it: a number 0 … 127 or a name (c4 = 60, `#` sharp, `b` flat); −1 when it is neither. */
    int sfzKey(const std::string &s);
}
