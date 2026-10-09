/*
 *  Arstro DSP Library — Device and DeviceRegistry: every instrument and effect behind one face,
 *  every parameter described ONCE (REQ-device-1…4).
 *
 *  A host (the Solaris DAW) does not know a `Compressor` from a `BasicSynth`. It knows a Device:
 *  process a block, take notes if it is an instrument, read and write parameters by index or by
 *  name. And it knows a DeviceType: the registry entry that says what the device is called, what
 *  kind it is, and — for every parameter — its name, label, unit, range, default and, for a
 *  choice, its values. That entry is the ONLY description of a parameter anywhere in the suite:
 *  Solaris's `.slp` keys, its command addresses, its generated API document and its UI's knobs are
 *  all read from it (Solaris R-DSP-2). A range restated somewhere else is a range that will drift.
 *
 *  Values are ENGINEERING units (Hz, dB, ms, semitones, 0..1) — a choice is its index — and every
 *  write is clamped to the spec's range, rounded if the spec is an integer or a choice, and a
 *  non-finite value becomes the default, so no input can put a device into a state its spec
 *  does not describe.
 */
#pragma once
#include "../base/Sample.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
    struct ParamSpec
    {
        std::string name;                 // "filter.cutoff" — the key a host stores
        std::string label;                // "Cutoff" — what a knob says
        std::string unit;                 // "Hz", "dB", "ms", "st", "ct", "oct", "" (a 0..1 amount or a choice)
        double min = 0.0, max = 1.0, def = 0.0;
        std::vector<std::string> choices; // non-empty → a choice; the value is the index
        bool integer = false;             // octave, unison voices
        bool logScale = false;            // a knob taper hint (frequencies, times)

        bool isChoice() const { return !choices.empty(); }
        /** The value a write of `v` actually stores. */
        double clamp(double v) const;
    };

    /*
     *  A parameter's TWO other faces, derived from its spec and nothing else (REQ-device-7):
     *
     *  - the normalised 0…1 a plugin host and a knob speak — linear over [min, max]; a `logScale`
     *    spec with min > 0 is logarithmic (equal ratios, equal travel); an integer or a choice is
     *    DISCRETE by VST3's convention: steps = max − min (an integer) or choices − 1, normalised
     *    k / steps, and back min(steps, ⌊n · (steps + 1)⌋), so every step owns an equal slice;
     *  - its text — the choice's name, else the shortest decimal that reads back to the same double
     *    with at least one decimal ("900.0", "0.35") — what a project file and a plugin's state store.
     *
     *  One copy, so a VST3 plugin, the Solaris service and its panel agree to the last digit.
     */
    int paramSteps(const ParamSpec &spec);
    double normalizedFromValue(const ParamSpec &spec, double value);
    double valueFromNormalized(const ParamSpec &spec, double normalized);
    std::string paramToText(const ParamSpec &spec, double value);
    /** A choice's name or a number (clamped); false (nothing written) when it is neither. */
    bool paramFromText(const ParamSpec &spec, const std::string &text, double &value);

    enum class DeviceKind { Instrument, Effect };

    class Device;

    struct DeviceType
    {
        std::string name;    // "synth" — the registry key
        std::string label;   // "Basic Synth"
        DeviceKind kind = DeviceKind::Effect;
        std::string summary;
        std::vector<ParamSpec> params;
        /** An instrument whose keys MEAN something (a kit's pads): note → its name, ascending.
         *  Empty for a melodic instrument. A host names its piano-roll keys from it (REQ-device-6). */
        std::vector<std::pair<int, std::string>> noteNames;
        std::function<std::unique_ptr<Device>(const DeviceType &)> create;

        /** −1 when there is no such parameter. */
        int paramIndex(const std::string &name) const;
    };

    class Device
    {
    public:
        explicit Device(const DeviceType &type);
        virtual ~Device() = default;

        const DeviceType &type() const { return *mType; }
        bool isInstrument() const { return mType->kind == DeviceKind::Instrument; }

        int paramCount() const { return (int)mValues.size(); }
        double param(int index) const;
        /** Clamped per the spec; false when there is no such index (nothing changes). */
        bool setParam(int index, double value);
        /** False when there is no such name (nothing changes). */
        bool setParam(const std::string &name, double value);

        /** An effect transforms `io` in place; an instrument ADDS its sound into it. */
        virtual void process(Sample *const *io, int channels, int frames) = 0;
        virtual void noteOn(int note, int velocity) { (void)note; (void)velocity; }
        virtual void noteOff(int note) { (void)note; }
        virtual void allNotesOff() {}
        /** Clear all state: tails, voices, filter memory (a seek). */
        virtual void reset() = 0;
        /** Voices still sounding (instruments); 0 for an effect. */
        virtual int activeVoices() const { return 0; }

    protected:
        /** Push one (already clamped) value into the DSP object. */
        virtual void apply(int index, double value) = 0;
        /** Push every value — the derived constructor's last line. */
        void applyAll();

    private:
        const DeviceType *mType;
        std::vector<double> mValues;
    };

    class DeviceRegistry
    {
    public:
        /** Every device type, instruments first, in a stable order. */
        static const std::vector<DeviceType> &types();
        /** nullptr when there is no such type. */
        static const DeviceType *find(const std::string &name);
        /** nullptr when there is no such type. */
        static std::unique_ptr<Device> create(const std::string &name);
    };
}
