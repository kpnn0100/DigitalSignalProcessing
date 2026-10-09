/*
 *  vst3_equivalence — the plugin IS the library's device, sample for sample (REQ-vst-5; Solaris R-VST-5).
 *
 *  An offline host (the SDK's hosting classes) loads each built bundle, sets its state as text,
 *  plays notes and a parameter automated mid-block at a block size that is neither 128 nor a power
 *  of two, and keeps the output. The same device from `DeviceRegistry::create`, given the same
 *  values, warmed the same way and split at the same sample offsets, must produce the same floats —
 *  exactly. The state read back must be the registry's text. And the output must not be silence, so
 *  equality cannot be the cheap kind.
 */
#include "base/AudioConfig.h"
#include "device/Device.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace arstro;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kBlockSize = 100; // not the warm block, not a power of two
    constexpr int kTotal = 48000;
    int failures = 0;

    void check(bool ok, const std::string &what)
    {
        std::printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
        if (!ok) ++failures;
    }

    struct Note { int at, pitch, vel; bool on; };
    struct Point { int at, index; double norm; };
    struct Scene
    {
        std::string type, bundle;
        std::vector<double> values; // engineering units, every parameter
        std::vector<Note> notes;
        std::vector<Point> points;
    };

    std::string stateOf(const DeviceType &t, const std::vector<double> &v)
    {
        std::string s = "arstro-device 1\ntype=" + t.name + "\n";
        for (size_t i = 0; i < t.params.size(); ++i) s += t.params[i].name + "=" + paramToText(t.params[i], v[i]) + "\n";
        return s;
    }

    // what the plugin must do, written out with the library alone
    void reference(const Scene &sc, std::vector<float> &L, std::vector<float> &R)
    {
        AudioConfig::instance().setSampleRate(kRate);
        const DeviceType &t = *DeviceRegistry::find(sc.type);
        auto d = DeviceRegistry::create(sc.type);
        for (int i = 0; i < (int)t.params.size(); ++i) d->setParam(i, sc.values[(size_t)i]);
        std::vector<Sample> l(std::max(kBlockSize, 128), 0.0), r(l.size(), 0.0);
        Sample *io[2] = {l.data(), r.data()};
        d->reset();
        d->process(io, 2, 128);
        d->reset();
        for (int pos = 0; pos < kTotal; pos += kBlockSize)
        {
            const int n = std::min(kBlockSize, kTotal - pos);
            struct C { int at, kind, index; double value; };
            std::vector<C> cs; // parameter points first, then notes, then ordered by offset (stable) — as the plugin
            for (const auto &p : sc.points)
                if (p.at >= pos && p.at < pos + n) cs.push_back({p.at - pos, 0, p.index, valueFromNormalized(t.params[(size_t)p.index], p.norm)});
            for (const auto &e : sc.notes)
                if (e.at >= pos && e.at < pos + n) cs.push_back({e.at - pos, e.on ? 1 : 2, e.pitch, (double)e.vel});
            std::stable_sort(cs.begin(), cs.end(), [](const C &a, const C &b) { return a.at < b.at; });
            std::fill(l.begin(), l.begin() + n, 0.0);
            std::fill(r.begin(), r.begin() + n, 0.0);
            int at = 0;
            auto run = [&](int from, int k) {
                if (k <= 0) return;
                Sample *seg[2] = {l.data() + from, r.data() + from};
                d->process(seg, 2, k);
            };
            for (const auto &c : cs)
            {
                run(at, c.at - at);
                at = std::max(at, c.at);
                if (c.kind == 0) d->setParam(c.index, c.value);
                else if (c.kind == 1) d->noteOn(c.index, (int)c.value);
                else d->noteOff(c.index);
            }
            run(at, n - at);
            for (int i = 0; i < n; ++i) { L.push_back((float)l[(size_t)i]); R.push_back((float)r[(size_t)i]); }
        }
    }

    bool hosted(const Scene &sc, std::vector<float> &L, std::vector<float> &R, std::string &stateBack, std::string &why)
    {
        const std::string path = std::string(ARSTRO_VST3_DIR) + "/" + sc.bundle + ".vst3";
        auto module = VST3::Hosting::Module::create(path, why);
        if (!module) return false;
        const auto &factory = module->getFactory();
        VST3::Hosting::ClassInfo info;
        bool found = false;
        for (const auto &ci : factory.classInfos())
            if (ci.category() == kVstAudioEffectClass) { info = ci; found = true; }
        if (!found) { why = "no audio processor class"; return false; }
        IPtr<HostApplication> host = owned(new HostApplication());
        auto comp = factory.createInstance<IComponent>(info.ID());
        if (!comp || comp->initialize(host) != kResultOk) { why = "the component did not initialise"; return false; }
        FUnknownPtr<IAudioProcessor> proc(comp);
        if (!proc) { why = "no IAudioProcessor"; return false; }

        const DeviceType &t = *DeviceRegistry::find(sc.type);
        const std::string state = stateOf(t, sc.values);
        IPtr<MemoryStream> in = owned(new MemoryStream());
        int32 put = 0;
        in->write(const_cast<char *>(state.data()), (int32)state.size(), &put);
        in->seek(0, IBStream::kIBSeekSet, nullptr);
        if (comp->setState(in) != kResultOk) { why = "setState refused its own text"; return false; }

        SpeakerArrangement out = SpeakerArr::kStereo;
        ProcessSetup setup{kOffline, kSample32, kBlockSize, kRate};
        if (proc->setBusArrangements(nullptr, 0, &out, 1) != kResultOk || proc->setupProcessing(setup) != kResultOk) { why = "setup refused"; return false; }
        comp->activateBus(kAudio, kOutput, 0, true);
        comp->activateBus(kEvent, kInput, 0, true);
        if (comp->setActive(true) != kResultOk) { why = "setActive refused"; return false; }
        proc->setProcessing(true);

        HostProcessData data;
        data.prepare(*comp, kBlockSize, kSample32);
        EventList events;
        ParameterChanges changes(16);
        for (int pos = 0; pos < kTotal; pos += kBlockSize)
        {
            const int n = std::min(kBlockSize, kTotal - pos);
            events.clear();
            changes.clearQueue();
            for (const auto &p : sc.points)
                if (p.at >= pos && p.at < pos + n)
                {
                    int32 qi = 0, pi = 0;
                    if (IParamValueQueue *q = changes.addParameterData((ParamID)p.index, qi)) q->addPoint(p.at - pos, p.norm, pi);
                }
            for (const auto &e : sc.notes)
                if (e.at >= pos && e.at < pos + n)
                {
                    Event ev{};
                    ev.busIndex = 0;
                    ev.sampleOffset = e.at - pos;
                    ev.type = e.on ? Event::kNoteOnEvent : Event::kNoteOffEvent;
                    if (e.on) { ev.noteOn.pitch = (int16)e.pitch; ev.noteOn.velocity = e.vel / 127.0f; ev.noteOn.noteId = -1; }
                    else { ev.noteOff.pitch = (int16)e.pitch; ev.noteOff.velocity = 0.0f; ev.noteOff.noteId = -1; }
                    events.addEvent(ev);
                }
            data.numSamples = n;
            data.inputEvents = &events;
            data.inputParameterChanges = &changes;
            if (proc->process(data) != kResultOk) { why = "process failed"; return false; }
            for (int i = 0; i < n; ++i) { L.push_back(data.outputs[0].channelBuffers32[0][i]); R.push_back(data.outputs[0].channelBuffers32[1][i]); }
        }
        IPtr<MemoryStream> back = owned(new MemoryStream());
        comp->getState(back);
        stateBack.assign(back->getData(), (size_t)back->getSize());
        proc->setProcessing(false);
        comp->setActive(false);
        comp->terminate();
        return true;
    }

    void scene(Scene sc)
    {
        const DeviceType &t = *DeviceRegistry::find(sc.type);
        std::vector<float> hl, hr, rl, rr;
        std::string back, why;
        const bool ok = hosted(sc, hl, hr, back, why);
        check(ok, sc.bundle + " loads, takes its state as text and plays" + (ok ? "" : ": " + why));
        if (!ok) return;
        reference(sc, rl, rr);
        size_t diff = 0;
        double rms = 0;
        for (size_t i = 0; i < hl.size() && i < rl.size(); ++i)
        {
            diff += (hl[i] != rl[i]) + (hr[i] != rr[i]);
            rms += (double)hl[i] * hl[i];
        }
        rms = std::sqrt(rms / std::max<size_t>(1, hl.size()));
        check(hl.size() == rl.size() && diff == 0, sc.bundle + ": the plugin equals the device sample for sample (" + std::to_string(diff) + " differ of " +
                                                       std::to_string(hl.size() * 2) + ")");
        check(rms > 1e-3, sc.bundle + ": and it is not silence (RMS " + std::to_string(rms) + ")");
        // the state it reports: every parameter in the registry's text, the automated one where automation left it
        std::vector<double> after = sc.values;
        for (const auto &p : sc.points) after[(size_t)p.index] = valueFromNormalized(t.params[(size_t)p.index], p.norm);
        check(back == stateOf(t, after), sc.bundle + ": its state reads back as the registry's text");
    }
}

int main()
{
    // Basic Synth: every parameter moved off its default (37 % along its own taper) but those that
    // decide whether it is heard, so silence cannot pass; a chord and a line, notes landing mid-block; the
    // filter cutoff automated at a sample that is not a block start
    {
        Scene sc;
        sc.type = "synth";
        sc.bundle = "ArstroBasicSynth";
        const DeviceType &t = *DeviceRegistry::find("synth");
        for (const auto &p : t.params) sc.values.push_back(valueFromNormalized(p, 0.37));
        for (size_t i = 0; i < t.params.size(); ++i)
            for (const char *loud : {"level", "sustain", "volume", "cutoff", "attack", "mode"}) // what decides whether it is heard
                if (t.params[i].name.find(loud) != std::string::npos) sc.values[i] = t.params[i].def;
        sc.notes = {{37, 60, 100, true}, {37, 64, 90, true}, {251, 67, 127, true}, {9000, 60, 0, false}, {12345, 72, 64, true},
                    {20011, 64, 0, false}, {20011, 67, 0, false}, {30000, 72, 0, false}, {30050, 48, 110, true}, {44444, 48, 0, false}};
        const int cut = t.paramIndex("filter.cutoff");
        sc.points = {{4567, cut, 0.8}, {16001, cut, 0.2}};
        scene(sc);
    }
    // Drum Machine: the kit's pads (the registry's note names), a two-bar pattern off the grid
    {
        Scene sc;
        sc.type = "drums";
        sc.bundle = "ArstroDrumMachine";
        const DeviceType &t = *DeviceRegistry::find("drums");
        for (const auto &p : t.params) sc.values.push_back(p.def);
        for (int k = 0; k < 16; ++k)
        {
            const int at = 13 + k * 2997;
            sc.notes.push_back({at, k % 4 == 0 ? 36 : 42, k % 2 ? 80 : 120, true});
            if (k % 8 == 4) sc.notes.push_back({at + 7, 38, 110, true});
        }
        const int decay = t.paramIndex(t.params[0].name);
        sc.points = {{24000 + 33, decay, 0.9}};
        scene(sc);
    }
    std::printf("\n%d failed\n", failures);
    return failures ? 1 : 0;
}
