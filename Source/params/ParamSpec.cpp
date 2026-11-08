#include "ParamSpec.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace openfilter::params
{
namespace
{
    ParamSpec makeFloat (std::string id, std::string name, Unit unit, double min, double max, double def, double centre = 0.0)
    {
        ParamSpec s;
        s.id = std::move (id); s.name = std::move (name);
        s.kind = Kind::Float; s.unit = unit;
        s.min = min; s.max = max; s.def = def; s.centre = centre;
        return s;
    }

    ParamSpec makeBool (std::string id, std::string name, bool def, bool automatable = true)
    {
        ParamSpec s;
        s.id = std::move (id); s.name = std::move (name);
        s.kind = Kind::Bool; s.min = 0.0; s.max = 1.0; s.def = def ? 1.0 : 0.0;
        s.automatable = automatable;
        return s;
    }

    ParamSpec makeChoice (std::string id, std::string name, std::vector<std::string> choices, int def)
    {
        ParamSpec s;
        s.id = std::move (id); s.name = std::move (name);
        s.kind = Kind::Choice; s.choices = std::move (choices);
        s.min = 0.0; s.max = (double) s.choices.size() - 1.0; s.def = def;
        return s;
    }

    std::vector<std::string> slopeChoices()
    {
        std::vector<std::string> v;

        for (int db = 6; db <= 96; db += 6)
            v.push_back (std::to_string (db) + " dB/oct");

        v.push_back ("Brickwall"); // réservé
        return v;
    }

    std::vector<ParamSpec> buildAll()
    {
        std::vector<ParamSpec> all;
        all.reserve ((size_t) kNumBands * 17 + 3 + kNumMacros);

        for (int n = 1; n <= kNumBands; ++n)
        {
            const std::string p = "b" + std::to_string (n) + "_";
            const std::string N = "B" + std::to_string (n) + " ";

            all.push_back (makeFloat (p + "freq", N + "Freq", Unit::Hz, 10.0, 30000.0, 1000.0, 1000.0));
            all.push_back (makeFloat (p + "gain", N + "Gain", Unit::Db, -30.0, 30.0, 0.0));
            all.push_back (makeFloat (p + "q", N + "Q", Unit::Q, 0.025, 40.0, 1.0, 1.0));
            all.push_back (makeChoice (p + "type", N + "Type",
                                       { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt", "All Pass" }, 0));
            all.push_back (makeChoice (p + "slope", N + "Slope", slopeChoices(), 1)); // 12 dB/oct
            all.push_back (makeChoice (p + "place", N + "Place", { "Stereo", "Left", "Right", "Mid", "Side" }, 0));
            all.push_back (makeBool (p + "on", N + "On", true));
            all.push_back (makeFloat (p + "dynRange", N + "Dyn Range", Unit::Db, -30.0, 30.0, 0.0));
            all.push_back (makeFloat (p + "thresh", N + "Threshold", Unit::Db, -60.0, 0.0, -30.0));
            all.push_back (makeBool (p + "threshAuto", N + "Threshold Auto", true, false));
            all.push_back (makeFloat (p + "ratio", N + "Ratio", Unit::Ratio, 1.0, 20.0, 4.0, 4.0));
            all.push_back (makeFloat (p + "attack", N + "Attack", Unit::Ms, 0.1, 200.0, 10.0, 10.0));
            all.push_back (makeBool (p + "attackAuto", N + "Attack Auto", true, false));
            all.push_back (makeFloat (p + "release", N + "Release", Unit::Ms, 10.0, 2000.0, 200.0, 200.0));
            all.push_back (makeBool (p + "releaseAuto", N + "Release Auto", true, false));
            all.push_back (makeChoice (p + "dynSrc", N + "Dyn Source", { "Input", "Sidechain" }, 0));
            all.push_back (makeBool (p + "used", N + "Used", false, false));
        }

        all.push_back (makeFloat ("out_gain", "Output Gain", Unit::Db, -100.0, 36.0, 0.0));
        all.push_back (makeBool ("bypass", "Bypass", false));
        all.push_back (makeBool ("phase_invert", "Phase Invert", false));

        for (int m = 1; m <= kNumMacros; ++m)
            all.push_back (makeFloat ("macro" + std::to_string (m), "Macro " + std::to_string (m), Unit::Percent, 0.0, 1.0, 0.0));

        return all;
    }

    std::string trim (const std::string& s)
    {
        size_t a = 0, b = s.size();

        while (a < b && std::isspace ((unsigned char) s[a])) ++a;
        while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
        return s.substr (a, b - a);
    }

    std::string lower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        return s;
    }

    std::string printf1 (const char* fmt, double v)
    {
        char buf[64];
        std::snprintf (buf, sizeof (buf), fmt, v);
        return buf;
    }
}

const std::vector<ParamSpec>& allParams()
{
    static const std::vector<ParamSpec> all = buildAll();
    return all;
}

std::string bandParamId (int band, const std::string& param)
{
    return "b" + std::to_string (band) + "_" + param;
}

const ParamSpec* findParam (const std::string& id)
{
    for (const auto& p : allParams())
        if (p.id == id)
            return &p;

    return nullptr;
}

double skewForCentre (double min, double max, double centre)
{
    if (centre <= min || centre >= max)
        return 1.0;

    return std::log (0.5) / std::log ((centre - min) / (max - min));
}

double toNormalised (const ParamSpec& spec, double value)
{
    const double lin = std::clamp ((value - spec.min) / (spec.max - spec.min), 0.0, 1.0);
    return spec.centre > 0.0 ? std::pow (lin, skewForCentre (spec.min, spec.max, spec.centre)) : lin;
}

double fromNormalised (const ParamSpec& spec, double normalised)
{
    const double n = std::clamp (normalised, 0.0, 1.0);
    const double lin = spec.centre > 0.0 ? std::pow (n, 1.0 / skewForCentre (spec.min, spec.max, spec.centre)) : n;
    return spec.min + lin * (spec.max - spec.min);
}

std::string formatValue (const ParamSpec& spec, double v)
{
    switch (spec.kind)
    {
        case Kind::Bool:   return v >= 0.5 ? "On" : "Off";
        case Kind::Choice:
        {
            const auto i = (size_t) std::clamp ((int) std::lround (v), 0, (int) spec.choices.size() - 1);
            return spec.choices[i];
        }
        case Kind::Float: break;
    }

    switch (spec.unit)
    {
        case Unit::Hz:      return v >= 1000.0 ? printf1 ("%.2f kHz", v / 1000.0) : printf1 ("%.1f Hz", v);
        case Unit::Db:      return v <= -99.9 ? "-inf dB" : printf1 ("%+.1f dB", v);
        case Unit::Q:       return printf1 ("%.2f", v);
        case Unit::Ms:      return printf1 ("%.1f ms", v);
        case Unit::Ratio:   return v >= 19.95 ? "inf:1" : printf1 ("%.1f:1", v);
        case Unit::Percent: return printf1 ("%.0f %%", v * 100.0);
        case Unit::None:    break;
    }
    return printf1 ("%.3f", v);
}

bool parseValue (const ParamSpec& spec, const std::string& text, double& value)
{
    std::string t = lower (trim (text));

    if (t.empty())
        return false;

    if (spec.kind == Kind::Bool)
    {
        if (t == "on" || t == "1" || t == "true")  { value = 1.0; return true; }
        if (t == "off" || t == "0" || t == "false") { value = 0.0; return true; }
        return false;
    }

    if (spec.kind == Kind::Choice)
    {
        for (size_t i = 0; i < spec.choices.size(); ++i)
            if (lower (spec.choices[i]) == t) { value = (double) i; return true; }
        return false;
    }

    if (spec.unit == Unit::Db && (t == "-inf" || t == "-inf db"))
    {
        value = spec.min;
        return true;
    }

    char* end = nullptr;
    const double number = std::strtod (t.c_str(), &end);

    if (end == t.c_str())
        return false;

    std::string suffix = trim (std::string (end));
    double v = number;

    switch (spec.unit)
    {
        case Unit::Hz:
            if (suffix == "k" || suffix == "khz")      v = number * 1000.0;
            else if (suffix.empty() || suffix == "hz") v = number;
            else return false;
            break;
        case Unit::Db:
            if (suffix == "x")                         // « 2x » = gain linéaire de 2, soit +6,02 dB
            {
                if (number <= 0.0) return false;
                v = 20.0 * std::log10 (number);
            }
            else if (! (suffix.empty() || suffix == "db")) return false;
            break;
        case Unit::Ms:
            if (suffix == "s")                         v = number * 1000.0;
            else if (! (suffix.empty() || suffix == "ms")) return false;
            break;
        case Unit::Ratio:
            if (suffix == ":1" || suffix == "x" || suffix.empty()) v = number;
            else if (suffix == "inf") v = spec.max;
            else return false;
            break;
        case Unit::Percent:
            if (suffix == "%") v = number / 100.0;
            else if (! suffix.empty()) return false;
            break;
        case Unit::Q:
        case Unit::None:
            if (! suffix.empty()) return false;
            break;
    }

    value = std::clamp (v, spec.min, spec.max);
    return std::isfinite (value);
}

}
