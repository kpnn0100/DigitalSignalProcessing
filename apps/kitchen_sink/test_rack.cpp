/*
 *  Native smoke test for RackEngine (the engine the WASM web UI drives).
 *  Exercises the catalogue, chain editing, parameters, notes, and render.
 *  Exits non-zero on failure (used by CTest target `rack_smoke`).
 */
#include "RackEngine.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace arstro::kitchen;

static int gFail = 0;
static void check(bool ok, const char *what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++gFail;
}

static double peak(const std::vector<float> &b)
{
    double p = 0;
    for (float s : b) p = std::fmax(p, std::fabs((double)s));
    return p;
}
static bool finite(const std::vector<float> &b)
{
    for (float s : b) if (!std::isfinite(s)) return false;
    return true;
}

int main()
{
    // Catalogue is non-empty and self-describing (drives the UI).
    check(!nodeTypes().empty(), "catalogue non-empty");
    std::string json = RackEngine::nodeTypesJson();
    check(json.find("Reverb") != std::string::npos && json.find("width") != std::string::npos,
          "nodeTypesJson lists Reverb + width");

    RackEngine eng;
    eng.setSampleRate(48000);

    // Build a chain: add one of every type, set each param to its midpoint.
    for (int t = 0; t < (int)nodeTypes().size(); ++t)
    {
        eng.addNode(t);
        const auto &params = nodeTypes()[t].params;
        for (int p = 0; p < (int)params.size(); ++p)
            eng.setNodeParam(eng.nodeCount() - 1, p, 0.5 * (params[p].min + params[p].max));
    }
    check(eng.nodeCount() == (int)nodeTypes().size(), "added one node per type");

    // Reorder + remove edge cases (must not crash / must bound-check).
    eng.moveNode(0, 1);
    eng.moveNode(0, -1);          // out of range -> no-op
    eng.moveNode(eng.nodeCount() - 1, 1); // out of range -> no-op
    eng.setNodeParam(999, 0, 1.0); // out of range -> no-op
    eng.addNode(999);              // unknown type -> no-op
    eng.removeNode(999);           // out of range -> no-op

    const int frames = 256, ch = 2;
    std::vector<float> buf(frames * ch, 0.0f);

    // Full one-of-everything chain: must stay finite (it includes long delays,
    // so we don't require audible output within this short window).
    eng.noteOnMidi(69, 0.9);       // A4
    bool allFinite = true;
    for (int blk = 0; blk < 40; ++blk)
    {
        eng.render(buf.data(), frames);
        allFinite = allFinite && finite(buf);
    }
    check(allFinite, "full chain renders finite");
    eng.noteOff(60);               // wrong note -> ignored (still sounding)
    eng.noteOff(69);               // correct note -> release

    // Latency-free chain (LowPass + Reverb): a note must produce audible output.
    {
        RackEngine light;
        light.setSampleRate(48000);
        light.addNode(2); // LowPass
        light.addNode(8); // Reverb
        light.noteOnMidi(69, 0.9);
        double total = 0;
        for (int blk = 0; blk < 60; ++blk)
        {
            light.render(buf.data(), frames);
            total = std::fmax(total, peak(buf));
        }
        check(total > 0.0001, "note produced sound through LowPass+Reverb");
    }

    // Remove all nodes; a bare generator still renders.
    while (eng.nodeCount() > 0) eng.removeNode(0);
    check(eng.nodeCount() == 0, "chain emptied");
    eng.noteOnMidi(72, 0.8);
    eng.render(buf.data(), frames);
    check(finite(buf) && peak(buf) > 0.0001, "bare generator renders");

    std::printf("\n%s\n", gFail == 0 ? "rack_smoke OK" : "rack_smoke FAILED");
    return gFail == 0 ? 0 : 1;
}
