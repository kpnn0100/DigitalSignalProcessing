#pragma once
/*
 *  Arstro DSP Library — Wavetable: frames (single cycles) read at any pitch with nothing above Nyquist, moved through
 *  by a position (REQ-wavetable-1, -2). Every frame is band-limited once, at nine levels of halving bandwidth, by the
 *  library's FFT; a read mixes two levels that both lie below Nyquist (never an alias), reads each between its points
 *  (Catmull-Rom) and between two frames (linear). src/generator/README.md ## Wavetable (W1)–(W6).
 *
 *  Built once (it allocates); `read` allocates nothing and changes nothing — any thread, any number of oscillators.
 *  The built-ins are made on first use and live for the program.
 */
#include "../base/Sample.h"
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
    class Wavetable
    {
    public:
        static constexpr int kFrameSize = 2048;   // the points of a cycle given
        static constexpr int kMaxHarmonics = 256; // (W1) H_0
        static constexpr int kLevels = 9;         // H_l = 256 / 2^l: 256 … 1
        static constexpr int kMaxFrames = 256;

        /** (W1) From `cycles` (each kFrameSize points; 1 … kMaxFrames of them). */
        explicit Wavetable(const std::vector<std::vector<float>> &cycles);
        int frames() const { return mFrames; }
        /** (W2)–(W4) one point: `phase` 0…1, `position` 0…1, `dt` the phase step of the highest voice reading it. */
        Sample read(Sample phase, Sample position, Sample dt) const;
        /** One level of one frame, as stored (for tests): its N_l points. */
        const float *level(int frame, int lvl, int &points) const;

        /** (W5) the built-ins: Basic, Pulse, Harmonics, Formant, Sync, Fold. */
        static const std::vector<std::string> &builtInNames();
        static const Wavetable &builtIn(int index);
        /** (W6) a mono sound cut into cycles of kFrameSize (at most kMaxFrames); null when it is empty. */
        static std::unique_ptr<Wavetable> fromSample(const float *mono, long long frames);

    private:
        Sample readLevel(int lvl, int frame, Sample phase) const;
        int mFrames = 0;
        int mPoints[kLevels] = {0};
        std::vector<float> mData[kLevels]; // [frame · N_l + i]
    };
}
