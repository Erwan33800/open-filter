#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "dsp/Biquad.h"
#include "dsp/FilterDesign.h"

// Les noms des TEST_CASE restent en ASCII : voir CLAUDE.md (console Windows et ctest).

using namespace openfilter::dsp;

namespace
{
    constexpr double kSampleRates[] = { 44100.0, 48000.0, 96000.0 };

    FilterParams make (FilterType t, double f, double g, double q, int slope = 12)
    {
        FilterParams p;
        p.type = t; p.freq = f; p.gainDb = g; p.q = q; p.slopeDb = slope;
        return p;
    }

    // Écart maximal (dB) entre la cascade numérique et la référence analogique, de 20 Hz à 20 kHz,
    // en ignorant les zones où la référence tombe sous floorDb (fond de bande atténuée, creux du notch).
    double maxErrorDb (const FilterParams& p, double fs, double floorDb)
    {
        const FilterStages s = designFilter (p, fs);
        double worst = 0.0;

        for (int i = 0; i < 400; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 399.0);

            if (f > 20000.0 || f > 0.49 * fs)
                break;

            const double analog = analogMagnitudeDb (p, f);

            if (analog < floorDb)
                continue;

            worst = std::max (worst, std::abs (magnitudeDb (s, f, fs) - analog));
        }
        return worst;
    }
}

TEST_CASE ("Bell reaches the requested gain at its center frequency", "[filters]")
{
    for (double fs : kSampleRates)
        for (double f0 : { 50.0, 440.0, 1000.0, 5000.0, 12000.0 })
            for (double q : { 0.3, 1.0, 4.0, 20.0 })
                for (double g : { -24.0, -6.0, 3.0, 12.0, 30.0 })
                {
                    const auto s = designFilter (make (FilterType::Bell, f0, g, q), fs);
                    INFO ("fs=" << fs << " f0=" << f0 << " q=" << q << " g=" << g);
                    REQUIRE (std::abs (magnitudeDb (s, f0, fs) - g) < 0.01);
                }
}

TEST_CASE ("Shelves reach half of their gain at the corner frequency", "[filters]")
{
    for (double fs : kSampleRates)
        for (FilterType t : { FilterType::LowShelf, FilterType::HighShelf })
            for (double f0 : { 100.0, 1000.0, 6000.0 })
                for (double q : { 0.5, 0.71, 1.5 })
                    for (double g : { -12.0, 6.0, 12.0 })
                    {
                        const auto s = designFilter (make (t, f0, g, q), fs);
                        INFO ("fs=" << fs << " f0=" << f0 << " q=" << q << " g=" << g);
                        REQUIRE (std::abs (magnitudeDb (s, f0, fs) - g / 2.0) < 0.01);
                    }
}

TEST_CASE ("Bell and shelves are transparent far from the band", "[filters]")
{
    const double fs = 48000.0;
    const auto bell = designFilter (make (FilterType::Bell, 1000.0, 12.0, 4.0), fs);
    REQUIRE (std::abs (magnitudeDb (bell, 10.0, fs)) < 0.01);

    const auto low = designFilter (make (FilterType::LowShelf, 200.0, 9.0, 0.71), fs);
    REQUIRE (std::abs (magnitudeDb (low, 10.0, fs) - 9.0) < 0.05);

    const auto high = designFilter (make (FilterType::HighShelf, 8000.0, -6.0, 0.71), fs);
    REQUIRE (std::abs (magnitudeDb (high, 10.0, fs)) < 0.01);
}

TEST_CASE ("A band at 0 dB is removed from processing", "[filters]")
{
    for (FilterType t : { FilterType::Bell, FilterType::LowShelf, FilterType::HighShelf })
        REQUIRE (designFilter (make (t, 1000.0, 0.0, 1.0), 48000.0).count == 0);

    for (FilterType t : { FilterType::LowCut, FilterType::HighCut, FilterType::Notch, FilterType::BandPass })
        REQUIRE (designFilter (make (t, 1000.0, 0.0, 0.71), 48000.0).count >= 1);
}

TEST_CASE ("Bell follows the analog curve at low center frequencies", "[filters][accuracy]")
{
    for (double fs : kSampleRates)
        for (double f0 : { 100.0, 500.0, 1000.0 })
            for (double q : { 1.0, 2.0, 4.0, 10.0 })
                for (double g : { -12.0, -6.0, 6.0, 12.0 })
                {
                    const double err = maxErrorDb (make (FilterType::Bell, f0, g, q), fs, -80.0);
                    INFO ("fs=" << fs << " f0=" << f0 << " q=" << q << " g=" << g << " err=" << err);
                    REQUIRE (err < 0.1);
                }
}

TEST_CASE ("Bell stays close to the analog curve near Nyquist", "[filters][accuracy]")
{
    // Un bilinéaire classique (RBJ) s'écarte de plus de 7 dB ici ; la méthode corrigée reste sous 1,5 dB.
    REQUIRE (maxErrorDb (make (FilterType::Bell, 15000.0, 12.0, 1.0), 44100.0, -80.0) < 1.5);
    REQUIRE (maxErrorDb (make (FilterType::Bell, 10000.0, -12.0, 1.0), 44100.0, -80.0) < 1.0);
}

TEST_CASE ("Shelves follow the analog curve at low corner frequencies", "[filters][accuracy]")
{
    for (double fs : kSampleRates)
        for (FilterType t : { FilterType::LowShelf, FilterType::HighShelf })
            for (double f0 : { 100.0, 300.0, 1000.0 })
                for (double q : { 0.5, 0.71, 1.0, 2.0 })
                    for (double g : { -12.0, -6.0, 6.0, 12.0 })
                    {
                        const double err = maxErrorDb (make (t, f0, g, q), fs, -80.0);
                        INFO ("fs=" << fs << " type=" << (int) t << " f0=" << f0 << " q=" << q << " g=" << g << " err=" << err);
                        REQUIRE (err < 0.2);
                    }
}

TEST_CASE ("A cut is the exact inverse of the boost with the same settings", "[filters]")
{
    for (double fs : kSampleRates)
        for (FilterType t : { FilterType::Bell, FilterType::LowShelf, FilterType::HighShelf })
            for (double f0 : { 80.0, 1000.0, 9000.0 })
                for (double q : { 0.5, 1.0, 5.0 })
                {
                    const auto up   = designFilter (make (t, f0, 9.0, q), fs);
                    const auto down = designFilter (make (t, f0, -9.0, q), fs);

                    for (double f : { 25.0, 200.0, 1000.0, 4000.0, 15000.0 })
                    {
                        INFO ("fs=" << fs << " type=" << (int) t << " f0=" << f0 << " q=" << q << " f=" << f);
                        REQUIRE (std::abs (magnitudeDb (up, f, fs) + magnitudeDb (down, f, fs)) < 1.0e-6);
                    }
                }
}

TEST_CASE ("Cuts are 3 dB down at the cutoff for every slope", "[filters][cuts]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (FilterType t : { FilterType::LowCut, FilterType::HighCut })
            for (int slope = 6; slope <= 96; slope += 6)
            {
                const auto s = designFilter (make (t, 1000.0, 0.0, 0.70710678, slope), fs);
                INFO ("fs=" << fs << " type=" << (int) t << " slope=" << slope);
                REQUIRE (s.count == (slope / 6 + 1) / 2);
                REQUIRE (std::abs (magnitudeDb (s, 1000.0, fs) + 3.0103) < 0.02);
            }
}

TEST_CASE ("Cuts follow the Butterworth curve", "[filters][cuts]")
{
    for (double fs : kSampleRates)
        for (FilterType t : { FilterType::LowCut, FilterType::HighCut })
            for (double f0 : { 100.0, 500.0, 1000.0 })
                for (int slope : { 6, 12, 18, 24, 48, 96 })
                    for (double q : { 0.70710678, 2.0 })
                    {
                        // On compare jusqu'à -20 dB : plus bas, l'écart n'a aucune importance musicale (un étage d'ordre 1
                        // ne peut pas suivre la courbe analogique mieux qu'environ 1 dB à -30 dB près de Nyquist).
                        const double err = maxErrorDb (make (t, f0, 0.0, q, slope), fs, -20.0);
                        INFO ("fs=" << fs << " type=" << (int) t << " f0=" << f0 << " slope=" << slope << " q=" << q << " err=" << err);
                        REQUIRE (err < (slope == 6 ? 0.6 : 0.3)); // l'ordre 1 est limité à ~0,5 dB (1 kHz, 44,1 kHz)
                    }
}

TEST_CASE ("Resonance only changes the last stage of a cut", "[filters][cuts]")
{
    const double fs = 48000.0;
    const auto flat = designFilter (make (FilterType::HighCut, 2000.0, 0.0, 0.70710678, 48), fs);
    const auto res  = designFilter (make (FilterType::HighCut, 2000.0, 0.0, 4.0, 48), fs);
    REQUIRE (flat.count == res.count);

    for (int i = 0; i + 1 < flat.count; ++i)
    {
        REQUIRE (flat.stage[(size_t) i].a1 == res.stage[(size_t) i].a1);
        REQUIRE (flat.stage[(size_t) i].a2 == res.stage[(size_t) i].a2);
    }

    const int last = flat.count - 1;
    REQUIRE (flat.stage[(size_t) last].a1 != res.stage[(size_t) last].a1);
    REQUIRE (magnitudeDb (res, 1900.0, fs) > magnitudeDb (flat, 1900.0, fs) + 3.0);
}

TEST_CASE ("Notch is silent at its center and band-pass peaks at 0 dB", "[filters]")
{
    for (double fs : kSampleRates)
        for (double f0 : { 100.0, 1000.0, 8000.0 })
            for (double q : { 0.5, 2.0, 10.0 })
            {
                const auto notch = designFilter (make (FilterType::Notch, f0, 0.0, q), fs);
                const auto bp    = designFilter (make (FilterType::BandPass, f0, 0.0, q), fs);
                INFO ("fs=" << fs << " f0=" << f0 << " q=" << q);
                REQUIRE (magnitudeDb (notch, f0, fs) < -60.0);
                REQUIRE (std::abs (magnitudeDb (notch, 10.0, fs)) < 0.5);
                REQUIRE (std::abs (magnitudeDb (bp, f0, fs)) < 0.02);
            }
}

TEST_CASE ("Every setting gives finite and stable filters", "[filters][stability]")
{
    const FilterType types[] = { FilterType::Bell, FilterType::LowShelf, FilterType::HighShelf, FilterType::LowCut,
                                 FilterType::HighCut, FilterType::Notch, FilterType::BandPass };

    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (FilterType t : types)
            for (double f0 : { 5.0, 10.0, 30.0, 100.0, 1000.0, 5000.0, 10000.0, 20000.0, 30000.0, 90000.0 })
                for (double q : { 0.01, 0.025, 0.1, 0.71, 1.0, 10.0, 40.0, 100.0 })
                    for (double g : { -40.0, -30.0, -12.0, 12.0, 30.0, 40.0 })
                        for (int slope : { 3, 6, 12, 18, 24, 96, 120 })
                        {
                            const auto s = designFilter (make (t, f0, g, q, slope), fs);
                            INFO ("fs=" << fs << " type=" << (int) t << " f0=" << f0 << " q=" << q << " g=" << g << " slope=" << slope);
                            REQUIRE (s.count >= 0);
                            REQUIRE (s.count <= kMaxStages);

                            for (int i = 0; i < s.count; ++i)
                            {
                                REQUIRE (s.stage[(size_t) i].isFinite());
                                REQUIRE (s.stage[(size_t) i].isStable());
                            }

                            for (double f : { 20.0, 1000.0, 20000.0 })
                                REQUIRE (std::isfinite (magnitudeDb (s, f, fs)));
                        }
}

TEST_CASE ("Biquad matches the difference equation", "[biquad]")
{
    BiquadCoeffs c;
    c.b0 = 0.2; c.b1 = 0.4; c.b2 = 0.2; c.a1 = -0.3; c.a2 = 0.1;

    Biquad bq;
    bq.setCoeffs (c);

    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    for (int n = 0; n < 200; ++n)
    {
        const double x = n == 0 ? 1.0 : std::sin (0.37 * n);
        const double expected = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        REQUIRE (std::abs (bq.process (x) - expected) < 1.0e-12);
        x2 = x1; x1 = x; y2 = y1; y1 = expected;
    }
}

TEST_CASE ("Identity biquad is bit exact", "[biquad]")
{
    Biquad bq;
    std::vector<float> in (64), out (64);

    for (size_t i = 0; i < in.size(); ++i)
        in[i] = std::sin (0.1f * (float) i) * 0.9f;

    bq.processBlock (in.data(), out.data(), (int) in.size());
    REQUIRE (in == out);
}

TEST_CASE ("Designed filter processes a sine at the expected level", "[biquad][filters]")
{
    const double fs = 48000.0;
    const auto s = designFilter (make (FilterType::Bell, 1000.0, 12.0, 2.0), fs);
    Biquad bq;
    bq.setCoeffs (s.stage[0]);

    double peak = 0.0;
    const double w = 2.0 * 3.14159265358979323846 * 1000.0 / fs;

    for (int n = 0; n < 48000; ++n)
    {
        const double y = bq.process (std::sin (w * n));

        if (n > 24000)
            peak = std::max (peak, std::abs (y));
    }

    REQUIRE (std::abs (20.0 * std::log10 (peak) - 12.0) < 0.05);
}
