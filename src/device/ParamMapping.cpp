/*
 *  Arstro DSP Library — a parameter's normalised and text faces (REQ-device-7). See Device.h.
 */
#include "Device.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace arstro
{
    namespace
    {
        bool logTaper(const ParamSpec &s) { return s.logScale && !s.isChoice() && !s.integer && s.min > 0.0 && s.max > s.min; }
    }

    int paramSteps(const ParamSpec &s)
    {
        if (s.isChoice()) return std::max(0, (int)s.choices.size() - 1);
        if (s.integer) return std::max(0, (int)std::lround(s.max - s.min));
        return 0;
    }

    double normalizedFromValue(const ParamSpec &s, double value)
    {
        const double v = s.clamp(value);
        const int steps = paramSteps(s);
        if (s.isChoice() || s.integer)
            return steps == 0 ? 0.0 : (v - (s.isChoice() ? 0.0 : s.min)) / steps;
        if (!(s.max > s.min)) return 0.0;
        const double n = logTaper(s) ? std::log(v / s.min) / std::log(s.max / s.min) : (v - s.min) / (s.max - s.min);
        return std::clamp(n, 0.0, 1.0);
    }

    double valueFromNormalized(const ParamSpec &s, double normalized)
    {
        const double n = std::isfinite(normalized) ? std::clamp(normalized, 0.0, 1.0) : normalizedFromValue(s, s.def);
        const int steps = paramSteps(s);
        if (s.isChoice() || s.integer)
        {
            const double k = std::min((double)steps, std::floor(n * (steps + 1)));
            return s.clamp((s.isChoice() ? 0.0 : s.min) + k);
        }
        if (logTaper(s)) return s.clamp(s.min * std::pow(s.max / s.min, n));
        return s.clamp(s.min + n * (s.max - s.min));
    }

    std::string paramToText(const ParamSpec &s, double value)
    {
        const double v = s.clamp(value);
        if (s.isChoice()) return s.choices[(size_t)v];
        if (v == 0.0) return "0.0"; // also −0
        // the suite's canonical number (Solaris's `.slp`): the shortest fixed text that reads back to
        // the same double, at least one decimal — or, far from 1, the shortest %g that does
        char buf[64];
        if (std::fabs(v) >= 1e-6 && std::fabs(v) < 1e15)
        {
            for (int d = 1; d <= 17; ++d)
            {
                std::snprintf(buf, sizeof buf, "%.*f", d, v);
                if (std::strtod(buf, nullptr) == v) return buf;
            }
            std::snprintf(buf, sizeof buf, "%.*f", 17, v);
            return buf;
        }
        for (int p = 1; p <= 17; ++p)
        {
            std::snprintf(buf, sizeof buf, "%.*g", p, v);
            if (std::strtod(buf, nullptr) == v) break;
        }
        return buf;
    }

    bool paramFromText(const ParamSpec &s, const std::string &text, double &value)
    {
        if (s.isChoice())
            for (size_t i = 0; i < s.choices.size(); ++i)
                if (s.choices[i] == text) { value = (double)i; return true; }
        if (text.empty()) return false;
        char *end = nullptr;
        const double v = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0') return false;
        value = s.clamp(v);
        return true;
    }
}
