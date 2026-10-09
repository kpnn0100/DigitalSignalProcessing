/*
 *  Arstro DSP Library — the VST3 factory, compiled once per plugin (ARSTRO_VST3_KIND).
 *
 *  The class ids below are FROZEN: a host stores them in every project that uses the plugin, so a
 *  changed id is a plugin that went missing. A new instrument gets new ids; these never move.
 */
#include "RegistryPlugin.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

using namespace Steinberg;

#if ARSTRO_VST3_KIND == 1
#define ARSTRO_PLUGIN_NAME "Arstro Basic Synth"
#define ARSTRO_PLUGIN_CATEGORY Vst::PlugType::kInstrumentSynth
static const arstro::vst3::PluginId kId{"synth", FUID(0x9FEB6596, 0xB6B044A4, 0xA46AC1C4, 0x145CD7D7),
                                        FUID(0x0E6DE505, 0xB9E5455A, 0x83F919C0, 0x3761CB37)};
#elif ARSTRO_VST3_KIND == 2
#define ARSTRO_PLUGIN_NAME "Arstro Drum Machine"
#define ARSTRO_PLUGIN_CATEGORY Vst::PlugType::kInstrumentDrum
static const arstro::vst3::PluginId kId{"drums", FUID(0xC571AC11, 0xA3B84436, 0x82B6CF3C, 0xB7034761),
                                        FUID(0x83323770, 0x4C5449F7, 0xA61A5CF9, 0x6C85C214)};
#else
#error "ARSTRO_VST3_KIND must be 1 (Basic Synth) or 2 (Drum Machine)"
#endif

static FUnknown *createProcessor(void *) { return static_cast<Vst::IAudioProcessor *>(new arstro::vst3::Processor(kId)); }
static FUnknown *createController(void *) { return static_cast<Vst::IEditController *>(new arstro::vst3::Controller(kId)); }

BEGIN_FACTORY_DEF("Arstro", "", "")
    DEF_CLASS2(INLINE_UID_FROM_FUID(kId.processor), PClassInfo::kManyInstances, kVstAudioEffectClass, ARSTRO_PLUGIN_NAME,
               Vst::kDistributable, ARSTRO_PLUGIN_CATEGORY, "1.0.0", kVstVersionString, createProcessor)
    DEF_CLASS2(INLINE_UID_FROM_FUID(kId.controller), PClassInfo::kManyInstances, kVstComponentControllerClass,
               ARSTRO_PLUGIN_NAME " Controller", 0, "", "1.0.0", kVstVersionString, createController)
END_FACTORY
