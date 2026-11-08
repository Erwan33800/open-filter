#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <string>

#include "params/ParamSpec.h"

// Noms des TEST_CASE en ASCII : voir CLAUDE.md.

using namespace openfilter::params;

namespace
{
    const ParamSpec& get (const std::string& id)
    {
        const ParamSpec* p = findParam (id);
        REQUIRE (p != nullptr);
        return *p;
    }

    std::string show (const std::string& id, double v) { return formatValue (get (id), v); }

    double parse (const std::string& id, const std::string& text)
    {
        double v = -12345.0;
        INFO ("text=" << text);
        REQUIRE (parseValue (get (id), text, v));
        return v;
    }
}

TEST_CASE ("Parameter list has the expected size and unique ids", "[params]")
{
    const auto& all = allParams();
    REQUIRE ((int) all.size() == kNumBands * 17 + 3 + kNumMacros); // 419

    std::set<std::string> ids, names;

    for (const auto& p : all)
    {
        REQUIRE (ids.insert (p.id).second);
        REQUIRE (names.insert (p.name).second);
        REQUIRE (! p.name.empty());
    }
}

TEST_CASE ("Published parameter ids are frozen", "[params][frozen]")
{
    // Cette liste ne doit JAMAIS changer pour un identifiant déjà publié (automation dans Live).
    // Pour ajouter un paramètre : ajouter un nouvel identifiant, jamais en modifier un.
    const char* band3[] = { "b3_freq", "b3_gain", "b3_q", "b3_type", "b3_slope", "b3_place", "b3_on", "b3_dynRange",
                            "b3_thresh", "b3_threshAuto", "b3_ratio", "b3_attack", "b3_attackAuto", "b3_release",
                            "b3_releaseAuto", "b3_dynSrc", "b3_used" };

    const auto& all = allParams();
    const size_t start = 2 * 17; // bande 3 = troisième bloc de 17

    for (size_t i = 0; i < 17; ++i)
        REQUIRE (all[start + i].id == band3[i]);

    REQUIRE (all.front().id == "b1_freq");
    REQUIRE (all[24 * 17 + 0].id == "out_gain");
    REQUIRE (all[24 * 17 + 1].id == "bypass");
    REQUIRE (all[24 * 17 + 2].id == "phase_invert");
    REQUIRE (all.back().id == "macro8");

    REQUIRE (get ("b3_freq").name == "B3 Freq");
    REQUIRE (get ("b3_dynRange").name == "B3 Dyn Range");
    REQUIRE (get ("b24_used").name == "B24 Used");
}

TEST_CASE ("Choice lists are frozen and keep reserved entries", "[params][frozen]")
{
    const auto& type = get ("b1_type");
    REQUIRE (type.choices.size() == 9);
    REQUIRE (type.choices[(size_t) BandType::Bell] == "Bell");
    REQUIRE (type.choices[(size_t) BandType::LowShelf] == "Low Shelf");
    REQUIRE (type.choices[(size_t) BandType::HighShelf] == "High Shelf");
    REQUIRE (type.choices[(size_t) BandType::LowCut] == "Low Cut");
    REQUIRE (type.choices[(size_t) BandType::HighCut] == "High Cut");
    REQUIRE (type.choices[(size_t) BandType::Notch] == "Notch");
    REQUIRE (type.choices[(size_t) BandType::BandPass] == "Band Pass");
    REQUIRE (type.choices[(size_t) BandType::Tilt] == "Tilt");
    REQUIRE (type.choices[(size_t) BandType::AllPass] == "All Pass");

    const auto& slope = get ("b1_slope");
    REQUIRE (slope.choices.size() == 17);
    REQUIRE (slope.choices[0] == "6 dB/oct");
    REQUIRE (slope.choices[1] == "12 dB/oct");
    REQUIRE (slope.choices[15] == "96 dB/oct");
    REQUIRE (slope.choices[16] == "Brickwall");
    REQUIRE (slope.def == 1.0); // 12 dB/oct par défaut

    const auto& place = get ("b1_place");
    REQUIRE (place.choices.size() == 5);
    REQUIRE (place.choices[(size_t) Placement::Stereo] == "Stereo");
    REQUIRE (place.choices[(size_t) Placement::Left] == "Left");
    REQUIRE (place.choices[(size_t) Placement::Right] == "Right");
    REQUIRE (place.choices[(size_t) Placement::Mid] == "Mid");
    REQUIRE (place.choices[(size_t) Placement::Side] == "Side");

    REQUIRE (get ("b1_dynSrc").choices.size() == 2);
}

TEST_CASE ("Ranges and defaults follow the spec", "[params]")
{
    const auto& f = get ("b5_freq");
    REQUIRE (f.min == 10.0); REQUIRE (f.max == 30000.0); REQUIRE (f.def == 1000.0);

    const auto& g = get ("b5_gain");
    REQUIRE (g.min == -30.0); REQUIRE (g.max == 30.0); REQUIRE (g.def == 0.0);

    const auto& q = get ("b5_q");
    REQUIRE (q.min == 0.025); REQUIRE (q.max == 40.0); REQUIRE (q.def == 1.0);

    REQUIRE (get ("b5_on").def == 1.0);   // une bande existante est active
    REQUIRE (get ("b5_used").def == 0.0); // et n'existe pas tant qu'on ne la crée pas
    REQUIRE (get ("b5_dynRange").def == 0.0); // 0 dB = bande statique
    REQUIRE (get ("b5_ratio").def == 4.0);
    REQUIRE (get ("b5_threshAuto").def == 1.0);
    REQUIRE (get ("b5_attackAuto").def == 1.0);
    REQUIRE (get ("b5_releaseAuto").def == 1.0);

    REQUIRE (get ("out_gain").max == 36.0);
    REQUIRE (get ("bypass").def == 0.0);

    for (const auto& p : allParams())
    {
        INFO (p.id);
        REQUIRE (p.min < p.max);
        REQUIRE (p.def >= p.min);
        REQUIRE (p.def <= p.max);

        if (p.centre > 0.0)
        {
            REQUIRE (p.centre > p.min);
            REQUIRE (p.centre < p.max);
        }

        if (p.kind == Kind::Choice)
        {
            REQUIRE (p.choices.size() >= 2);
            REQUIRE (p.max == (double) p.choices.size() - 1.0);
        }
    }
}

TEST_CASE ("Only the helper flags are hidden from automation", "[params]")
{
    for (const auto& p : allParams())
    {
        const bool hidden = p.id.find ("_used") != std::string::npos || p.id.find ("Auto") != std::string::npos;
        INFO (p.id);
        REQUIRE (p.automatable == ! hidden);
    }
}

TEST_CASE ("Normalised mapping puts the centre value in the middle", "[params]")
{
    for (const char* id : { "b2_freq", "b2_q", "b2_attack", "b2_release", "b2_ratio" })
    {
        const auto& p = get (id);
        INFO (id);
        REQUIRE (std::abs (toNormalised (p, p.centre) - 0.5) < 1.0e-12);
        REQUIRE (std::abs (fromNormalised (p, 0.5) - p.centre) < 1.0e-9);
        REQUIRE (toNormalised (p, p.min) == 0.0);
        REQUIRE (toNormalised (p, p.max) == 1.0);
    }

    // Aller-retour sur toute la course.
    const auto& f = get ("b2_freq");
    for (int i = 0; i <= 100; ++i)
    {
        const double n = i / 100.0;
        REQUIRE (std::abs (toNormalised (f, fromNormalised (f, n)) - n) < 1.0e-9);
    }

    // Linéaire : le gain n'a pas de courbe.
    REQUIRE (std::abs (toNormalised (get ("b2_gain"), 0.0) - 0.5) < 1.0e-12);
}

TEST_CASE ("Values are displayed as Live users expect", "[params][format]")
{
    REQUIRE (show ("b1_freq", 80.0) == "80.0 Hz");
    REQUIRE (show ("b1_freq", 999.9) == "999.9 Hz");
    REQUIRE (show ("b1_freq", 2500.0) == "2.50 kHz");
    REQUIRE (show ("b1_freq", 20000.0) == "20.00 kHz");
    REQUIRE (show ("b1_gain", 3.0) == "+3.0 dB");
    REQUIRE (show ("b1_gain", -6.25) == "-6.2 dB");
    REQUIRE (show ("b1_gain", 0.0) == "+0.0 dB");
    REQUIRE (show ("b1_q", 0.707) == "0.71");
    REQUIRE (show ("b1_attack", 12.0) == "12.0 ms");
    REQUIRE (show ("b1_ratio", 4.0) == "4.0:1");
    REQUIRE (show ("b1_ratio", 20.0) == "inf:1");
    REQUIRE (show ("out_gain", -100.0) == "-inf dB");
    REQUIRE (show ("out_gain", 6.0) == "+6.0 dB");
    REQUIRE (show ("b1_on", 1.0) == "On");
    REQUIRE (show ("b1_on", 0.0) == "Off");
    REQUIRE (show ("b1_type", 3.0) == "Low Cut");
    REQUIRE (show ("b1_slope", 3.0) == "24 dB/oct");
    REQUIRE (show ("macro1", 0.5) == "50 %");
}

TEST_CASE ("Typed values are understood", "[params][format]")
{
    REQUIRE (parse ("b1_freq", "2k") == 2000.0);
    REQUIRE (parse ("b1_freq", "2.5 kHz") == 2500.0);
    REQUIRE (parse ("b1_freq", "440") == 440.0);
    REQUIRE (parse ("b1_freq", " 80 Hz ") == 80.0);
    REQUIRE (parse ("b1_freq", "1") == 10.0);          // borné à la plage
    REQUIRE (parse ("b1_freq", "99k") == 30000.0);
    REQUIRE (parse ("b1_gain", "+3") == 3.0);
    REQUIRE (parse ("b1_gain", "-6 dB") == -6.0);
    REQUIRE (std::abs (parse ("b1_gain", "2x") - 6.0206) < 1.0e-3); // « 2x » = +6 dB
    REQUIRE (parse ("b1_gain", "100") == 30.0);
    REQUIRE (parse ("out_gain", "-inf") == -100.0);
    REQUIRE (parse ("b1_attack", "0.5 s") == 200.0);   // borné
    REQUIRE (parse ("b1_attack", "25 ms") == 25.0);
    REQUIRE (parse ("b1_ratio", "8:1") == 8.0);
    REQUIRE (parse ("b1_ratio", "inf") == 20.0);
    REQUIRE (parse ("b1_on", "off") == 0.0);
    REQUIRE (parse ("b1_type", "notch") == 5.0);
    REQUIRE (parse ("macro1", "25%") == 0.25);

    double v = 0.0;
    REQUIRE (! parseValue (get ("b1_freq"), "abc", v));
    REQUIRE (! parseValue (get ("b1_freq"), "", v));
    REQUIRE (! parseValue (get ("b1_freq"), "5 dB", v));
    REQUIRE (! parseValue (get ("b1_type"), "banana", v));
    REQUIRE (! parseValue (get ("b1_gain"), "-2x", v));
}

TEST_CASE ("Display and typing round trip", "[params][format]")
{
    for (double f : { 20.0, 80.0, 440.0, 999.0, 1500.0, 12000.0 })
    {
        const auto& p = get ("b1_freq");
        double back = 0.0;
        REQUIRE (parseValue (p, formatValue (p, f), back));
        REQUIRE (std::abs (back / f - 1.0) < 0.01);
    }
}
