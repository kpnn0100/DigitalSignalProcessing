/*
 *  Arstro DSP Library — a registry instrument as a VST3 plugin (REQ-vst-1…4; Solaris R-VST-2…4).
 *
 *  A THIN wrapper: the sound is the library's own class, created by `DeviceRegistry::create` — the
 *  same object Solaris renders — and every parameter is the registry's spec. Nothing here knows a
 *  synth from a drum machine; `factory.cpp` is compiled once per plugin with the registry type and
 *  its frozen class ids.
 *
 *  - Parameters: one per registry parameter, its id = its index in the type (frozen: a host stores
 *    it), title / unit / steps / default from the spec, normalised by `normalizedFromValue` /
 *    `valueFromNormalized` (REQ-device-7) — the one mapping the library has.
 *  - Processing: an event bus in (notes), a stereo bus out. A block is split at every note and
 *    every parameter point at its sample offset, so each lands on its exact sample — as Solaris's
 *    engine splits at note events. No warm-up block: Solaris warms every device so an EFFECT's
 *    smoothed parameters settle, but an instrument writes its parameters straight through, so its
 *    first block is already Solaris's (the equivalence test renders Solaris's way, warm-up included).
 *  - State: text, `arstro-device 1`, `type=<t>`, then `<name>=<text>` per parameter — the registry's
 *    text (`paramToText`), the values a `.slp` stores.
 *  - The audio thread never allocates or blocks: buffers are sized in `setupProcessing`, a state
 *    arriving from the host's UI thread is picked up with a `try_lock`.
 */
#pragma once
#include "device/Device.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace arstro
{
namespace vst3
{
    /** Which registry type a plugin wraps, and its class ids (frozen once shipped). */
    struct PluginId
    {
        const char *type;
        Steinberg::FUID processor, controller;
    };

    /** The state's text and its reader (unknown names are skipped; a different type is refused). */
    std::string stateText(const DeviceType &t, const std::vector<double> &values);
    bool readState(const DeviceType &t, const std::string &text, std::vector<double> &values);

    class Processor : public Steinberg::Vst::AudioEffect
    {
    public:
        explicit Processor(const PluginId &id);

        Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement *inputs, Steinberg::int32 numIns,
                                                         Steinberg::Vst::SpeakerArrangement *outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup &setup) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData &data) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream *state) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream *state) SMTG_OVERRIDE;

        static constexpr int kMaxChanges = 2048; // notes + parameter points in one block; more are dropped

    private:
        struct Change
        {
            Steinberg::int32 offset;
            int kind;   // 0 parameter, 1 note on, 2 note off
            int index;  // parameter index or pitch
            double value; // engineering value or velocity 0…127
        };
        void makeDevice();
        void apply(const Change &c);
        void render(int from, int n);

        const DeviceType *mType;
        std::unique_ptr<Device> mDevice;
        std::unique_ptr<std::atomic<double>[]> mValues; // what getState reports, written by process
        std::vector<double> mPending;                   // a state from the host, waiting for the audio thread
        bool mPendingSet = false;
        std::mutex mPendingLock;
        std::vector<Sample> mL, mR;
        std::vector<Change> mChanges;
    };

    class Controller : public Steinberg::Vst::EditController
    {
    public:
        explicit Controller(const PluginId &id);
        Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) SMTG_OVERRIDE;
        Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream *state) SMTG_OVERRIDE;

    private:
        const DeviceType *mType;
    };

    /** A registry parameter as a VST3 parameter: the spec's mapping and text, both ways. */
    class RegistryParameter : public Steinberg::Vst::Parameter
    {
    public:
        RegistryParameter(const ParamSpec &spec, Steinberg::Vst::ParamID id);
        void toString(Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string) const SMTG_OVERRIDE;
        bool fromString(const Steinberg::Vst::TChar *string, Steinberg::Vst::ParamValue &valueNormalized) const SMTG_OVERRIDE;
        Steinberg::Vst::ParamValue toPlain(Steinberg::Vst::ParamValue valueNormalized) const SMTG_OVERRIDE;
        Steinberg::Vst::ParamValue toNormalized(Steinberg::Vst::ParamValue plainValue) const SMTG_OVERRIDE;

    private:
        ParamSpec mSpec;
    };
}
}
