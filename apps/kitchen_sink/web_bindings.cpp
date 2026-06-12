/*
 *  Emscripten (embind) bindings: expose RackEngine to the web UI.
 *  Compiled only by build_web.sh (needs emcc). renderBlock returns a pointer
 *  into the wasm heap that JS reads as HEAPF32 (no JS-side malloc).
 */
#include <emscripten/bind.h>
#include "RackEngine.h"

using namespace emscripten;
using namespace arstro::kitchen;

// Render `frames` interleaved stereo frames; return the heap address as a number.
static uintptr_t renderBlock(RackEngine &e, int frames)
{
    return reinterpret_cast<uintptr_t>(e.renderInternal(frames));
}

EMSCRIPTEN_BINDINGS(arstro_kitchen)
{
    // Qualify: the library's `using namespace std` makes bare `function` ambiguous
    // with std::function once `using namespace emscripten` is in scope.
    emscripten::function("nodeTypesJson", &RackEngine::nodeTypesJson);

    class_<RackEngine>("RackEngine")
        .constructor<>()
        .function("setSampleRate", &RackEngine::setSampleRate)
        .function("setMasterLevel", &RackEngine::setMasterLevel)
        .function("setWaveform", &RackEngine::setWaveform)
        .function("setFrequency", &RackEngine::setFrequency)
        .function("setVoiceCount", &RackEngine::setVoiceCount)
        .function("setDetuneCents", &RackEngine::setDetuneCents)
        .function("setAttackMs", &RackEngine::setAttackMs)
        .function("setDecayMs", &RackEngine::setDecayMs)
        .function("setSustain", &RackEngine::setSustain)
        .function("setReleaseMs", &RackEngine::setReleaseMs)
        .function("noteOnMidi", &RackEngine::noteOnMidi)
        .function("noteOff", &RackEngine::noteOff)
        .function("nodeCount", &RackEngine::nodeCount)
        .function("nodeTypeAt", &RackEngine::nodeTypeAt)
        .function("addNode", &RackEngine::addNode)
        .function("removeNode", &RackEngine::removeNode)
        .function("moveNode", &RackEngine::moveNode)
        .function("setNodeParam", &RackEngine::setNodeParam)
        .function("renderBlock", &renderBlock, allow_raw_pointers());
}
