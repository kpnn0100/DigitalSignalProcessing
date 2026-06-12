/*
 *  Arstro Kitchen Sink — RackEngine
 *
 *  A signal generator (Oscillator + its ADSR) feeding a user-ordered list of
 *  effect nodes. The UI (native CLI or the WASM web app) drives it: add/remove/
 *  reorder nodes, set per-node parameters, play notes, render audio.
 *
 *  The node catalogue is data: each type carries its factory and a list of
 *  parameter descriptors (name/min/max/default + an apply function). The UI is
 *  generated from this catalogue, so adding a processor type here gives it UI for
 *  free (Open/Closed).
 */
#pragma once
#include "../../src/synth_dsp.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace kitchen
{
    // One adjustable parameter of a node: range + how to apply it.
    struct ParamDesc
    {
        std::string name;
        double min, max, def;
        std::function<void(SignalProcessor &, double)> apply;
    };

    // A processor type the rack can instantiate.
    struct NodeType
    {
        std::string name;
        std::function<std::unique_ptr<SignalProcessor>()> make;
        std::vector<ParamDesc> params;
    };

    // The catalogue of effect types the rack offers (registry).
    const std::vector<NodeType> &nodeTypes();

    class RackEngine
    {
    public:
        RackEngine();

        // ---- global ----
        void setSampleRate(double sr); // match the AudioContext sample rate
        void setMasterLevel(double level);

        // ---- generator (one oscillator + its ADSR) ----
        void setWaveform(int w);       // reserved (Saw only today)
        void setFrequency(double hz);
        void setVoiceCount(int v);
        void setDetuneCents(double c);
        void setAttackMs(double ms);
        void setDecayMs(double ms);
        void setSustain(double s);
        void setReleaseMs(double ms);
        void noteOnMidi(int midi, double velocity); // monophonic, last-note priority
        void noteOff(int midi);

        // ---- effect chain ----
        int  nodeCount() const;
        int  nodeTypeAt(int i) const;               // index into nodeTypes()
        void addNode(int typeIndex);                // append
        void removeNode(int i);
        void moveNode(int i, int dir);              // reorder by -1 / +1
        void setNodeParam(int i, int paramIdx, double value);

        // ---- render: fill `frames` interleaved stereo float frames ----
        void render(float *interleaved, int frames);
        // Render into an internal buffer and return it (WASM bridge: JS reads
        // HEAPF32 at this pointer — no JS-side malloc needed).
        const float *renderInternal(int frames);

        // Catalogue as JSON (types + each param's name/min/max/def) for the UI.
        static std::string nodeTypesJson();

    private:
        static double midiToHz(int midi);
        Oscillator mOsc;
        std::vector<std::unique_ptr<SignalProcessor>> mNodes;
        std::vector<int> mNodeTypeIdx;
        double mMaster = 0.5;
        int mCurrentNote = -1;
        std::vector<float> mScratch; // internal render buffer (WASM bridge)
    };
}
}
