/*
 *  Arstro DSP Library — the editor SEAM of the VST3 plugins (REQ-vst-6; Solaris R-VST-7).
 *
 *  The plugins' own editor is drawn by Artboard in the Arstro look, which this repo does not carry: it
 *  is the umbrella's (`apps/solaris/plugins`). When the umbrella builds the plugins it compiles its
 *  editor INTO them and defines ARSTRO_VST3_EDITOR; the controller's `createView("editor")` then returns
 *  `createEditor(*this)`. Built from this repo alone there is no editor and the host draws its generic
 *  parameter view — nothing here depends on a window system.
 *
 *  The editor reaches the plugin only through `Controller`: the registry type it wraps, the values the
 *  host holds, and the host's edit calls — `editBegin` / `editPerform` / `editEnd`, in ENGINEERING
 *  units, normalised by the one shared mapping (REQ-device-7) so the editor, a host's automation and
 *  Solaris agree to the last digit.
 */
#pragma once
#include "pluginterfaces/gui/iplugview.h"

namespace arstro
{
namespace vst3
{
    class Controller;
    /** The plugin's own editor (defined by the umbrella's editor, linked in with ARSTRO_VST3_EDITOR). */
    Steinberg::IPlugView *createEditor(Controller &controller);
}
}
