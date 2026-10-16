#pragma once
/*
 *  Onsets — where sounds begin (REQ-onset-1): energy-flux peaks, for slicing a recording at its transients.
 *
 *  Not a meter of a stream but an analysis of a whole sound, done once, off any audio thread. The math is in
 *  src/meter/README.md (O1)…(O5).
 */
#include <vector>

namespace arstro
{
    struct OnsetParams
    {
        double sensitivity = 0.5; // 0 … 1: how small a rise counts — (O3)'s δ = 3 + 12·(1 − sensitivity) dB
        double minGapMs = 50.0;   // (O4): two onsets at least this far apart
        double floorDb = -60.0;   // (O4): a frame below this is silence — no onset in it
    };
    /** The samples where sounds begin in `mono` (`frames` long, at `rate` Hz), ascending. */
    std::vector<long long> detectOnsets(const float *mono, long long frames, double rate, const OnsetParams &p = OnsetParams());
}
