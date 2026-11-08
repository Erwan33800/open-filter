#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp/EqEngine.h"
#include "params/ParamSpec.h"

// Noms des TEST_CASE en ASCII : voir CLAUDE.md.

using namespace openfilter::dsp;

// Les valeurs de placement du moteur et celles des paramètres doivent rester identiques.
static_assert ((int) Placement::Stereo == (int) openfilter::params::Placement::Stereo);
static_assert ((int) Placement::Left   == (int) openfilter::params::Placement::Left);
static_assert ((int) Placement::Right  == (int) openfilter::params::Placement::Right);
static_assert ((int) Placement::Mid    == (int) openfilter::params::Placement::Mid);
static_assert ((int) Placement::Side   == (int) openfilter::params::Placement::Side);

namespace
{
    constexpr double kFs = 48000.0;
    constexpr double kPi = 3.14159265358979323846;

    struct Rng
    {
        std::uint64_t s = 0x9E3779B97F4A7C15ull;
        double next() noexcept // [-1 ; 1[
        {
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            return (double) (std::int64_t) (s >> 11) / (double) (1ull << 52) - 1.0;
        }
        double unit() noexcept { return 0.5 * (next() + 1.0); }
    };

    BandSettings band (FilterType t, double f, double g, double q, Placement p = Placement::Stereo, int slope = 12)
    {
        BandSettings b;
        b.used = true;
        b.on = true;
        b.filter.type = t; b.filter.freq = f; b.filter.gainDb = g; b.filter.q = q; b.filter.slopeDb = slope;
        b.placement = p;
        return b;
    }

    struct Stereo
    {
        std::vector<float> l, r;
        float* ptrs[2];
        Stereo (size_t n) : l (n), r (n) { ptrs[0] = l.data(); ptrs[1] = r.data(); }
    };

    void fillSine (Stereo& s, double f, double amp, bool rightInverted = false)
    {
        for (size_t i = 0; i < s.l.size(); ++i)
        {
            const float v = (float) (amp * std::sin (2.0 * kPi * f * (double) i / kFs));
            s.l[i] = v;
            s.r[i] = rightInverted ? -v : v;
        }
    }

    void run (EqEngine& e, Stereo& s, int block)
    {
        const int n = (int) s.l.size();

        for (int pos = 0; pos < n; pos += block)
        {
            float* p[2] = { s.l.data() + pos, s.r.data() + pos };
            e.process (p, 2, std::min (block, n - pos));
        }
    }

    // Amplitude crête sur la dernière partie du signal (régime établi).
    double peak (const std::vector<float>& v, size_t from)
    {
        double m = 0.0;

        for (size_t i = from; i < v.size(); ++i)
            m = std::max (m, (double) std::abs (v[i]));

        return m;
    }

    double db (double ratio) { return 20.0 * std::log10 (ratio); }
}

TEST_CASE ("Idle engine leaves the audio bit exact", "[engine]")
{
    Rng rng;
    Stereo in (8192), out (8192);

    for (size_t i = 0; i < in.l.size(); ++i)
    {
        in.l[i] = out.l[i] = (float) (0.8 * rng.next());
        in.r[i] = out.r[i] = (float) (0.8 * rng.next());
    }

    EqEngine e;
    e.prepare (kFs, 512);

    SECTION ("no band at all")
    {
    }
    SECTION ("a band at 0 dB")
    {
        e.setBand (0, band (FilterType::Bell, 1000.0, 0.0, 1.0));
        e.setBand (1, band (FilterType::HighShelf, 8000.0, 0.0, 0.71, Placement::Mid));
    }
    SECTION ("a band that is bypassed")
    {
        auto b = band (FilterType::Bell, 1000.0, 12.0, 1.0);
        b.on = false;
        e.setBand (0, b);
    }
    SECTION ("a band that is not used")
    {
        auto b = band (FilterType::LowCut, 500.0, 0.0, 0.71, Placement::Side, 48);
        b.used = false;
        e.setBand (0, b);
    }

    run (e, out, 256);
    REQUIRE (in.l == out.l);
    REQUIRE (in.r == out.r);
}

TEST_CASE ("A bell applies its gain to a sine at the center frequency", "[engine]")
{
    for (double gain : { -12.0, -3.0, 6.0, 12.0 })
    {
        EqEngine e;
        e.prepare (kFs, 512);
        e.setBand (0, band (FilterType::Bell, 1000.0, gain, 2.0));

        Stereo s ((size_t) kFs);
        fillSine (s, 1000.0, 0.25);
        run (e, s, 128);

        INFO ("gain=" << gain);
        REQUIRE (std::abs (db (peak (s.l, 24000) / 0.25) - gain) < 0.02);
        REQUIRE (std::abs (db (peak (s.r, 24000) / 0.25) - gain) < 0.02);
    }
}

TEST_CASE ("Static processing does not depend on the block size", "[engine]")
{
    Rng rng;
    Stereo ref (6000);

    for (size_t i = 0; i < ref.l.size(); ++i)
    {
        ref.l[i] = (float) (0.3 * rng.next());
        ref.r[i] = (float) (0.3 * rng.next());
    }

    const auto render = [&] (int block)
    {
        Stereo s = ref;
        s.ptrs[0] = s.l.data(); s.ptrs[1] = s.r.data();
        EqEngine e;
        e.prepare (kFs, 512);
        e.setBand (0, band (FilterType::Bell, 800.0, 9.0, 3.0));
        e.setBand (1, band (FilterType::LowCut, 90.0, 0.0, 0.71, Placement::Side, 24));
        e.setBand (2, band (FilterType::HighShelf, 6000.0, -5.0, 0.71, Placement::Mid));
        e.setBand (3, band (FilterType::Notch, 3000.0, 0.0, 4.0, Placement::Left));
        e.setOutput (-3.0, false);
        run (e, s, block);
        return s;
    };

    const Stereo a = render (512);

    for (int block : { 1, 7, 32, 33, 100, 480 })
    {
        const Stereo b = render (block);
        INFO ("block=" << block);
        REQUIRE (a.l == b.l);
        REQUIRE (a.r == b.r);
    }
}

TEST_CASE ("Mid and Side bands only touch their own part of the signal", "[engine][stereo]")
{
    const double g = 12.0;
    const auto render = [&] (Placement p, bool rightInverted)
    {
        EqEngine e;
        e.prepare (kFs, 512);
        e.setBand (0, band (FilterType::Bell, 1000.0, g, 2.0, p));
        Stereo s ((size_t) kFs);
        fillSine (s, 1000.0, 0.25, rightInverted);
        run (e, s, 128);
        return db (peak (s.l, 24000) / 0.25);
    };

    // L = R : tout le signal est dans le Mid.
    REQUIRE (std::abs (render (Placement::Mid, false) - g) < 0.05);
    REQUIRE (std::abs (render (Placement::Side, false)) < 0.05);

    // L = -R : tout le signal est dans le Side.
    REQUIRE (std::abs (render (Placement::Side, true) - g) < 0.05);
    REQUIRE (std::abs (render (Placement::Mid, true)) < 0.05);

    // Stéréo : les deux.
    REQUIRE (std::abs (render (Placement::Stereo, false) - g) < 0.05);
    REQUIRE (std::abs (render (Placement::Stereo, true) - g) < 0.05);
}

TEST_CASE ("Left and Right bands only touch their own channel", "[engine][stereo]")
{
    for (Placement p : { Placement::Left, Placement::Right })
    {
        EqEngine e;
        e.prepare (kFs, 512);
        e.setBand (0, band (FilterType::Bell, 1000.0, 12.0, 2.0, p));
        Stereo s ((size_t) kFs);
        fillSine (s, 1000.0, 0.25);
        run (e, s, 128);

        const double l = db (peak (s.l, 24000) / 0.25), r = db (peak (s.r, 24000) / 0.25);
        INFO ("placement=" << (int) p);
        REQUIRE (std::abs ((p == Placement::Left ? l : r) - 12.0) < 0.05);
        REQUIRE (std::abs (p == Placement::Left ? r : l) < 0.001);
    }
}

TEST_CASE ("A mono track ignores the placement", "[engine][stereo]")
{
    for (Placement p : { Placement::Stereo, Placement::Left, Placement::Right, Placement::Mid, Placement::Side })
    {
        EqEngine e;
        e.prepare (kFs, 512);
        e.setBand (0, band (FilterType::Bell, 1000.0, 12.0, 2.0, p));

        std::vector<float> mono ((size_t) kFs);

        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = (float) (0.25 * std::sin (2.0 * kPi * 1000.0 * (double) i / kFs));

        for (size_t pos = 0; pos < mono.size(); pos += 128)
        {
            float* ptr[1] = { mono.data() + pos };
            e.process (ptr, 1, (int) std::min<size_t> (128, mono.size() - pos));
        }

        INFO ("placement=" << (int) p);
        REQUIRE (std::abs (db (peak (mono, 24000) / 0.25) - 12.0) < 0.05);
    }
}

TEST_CASE ("Output gain and phase invert are applied smoothly", "[engine][output]")
{
    EqEngine e;
    e.prepare (kFs, 512);
    e.setOutput (6.0, false);

    Stereo s ((size_t) kFs);
    fillSine (s, 440.0, 0.25);
    Stereo ref = s;
    run (e, s, 100);

    // Rampe, pas de saut : le premier échantillon n'est pas encore à +6 dB.
    REQUIRE (std::abs (s.l[1]) < std::abs (ref.l[1]) * 1.05 + 1.0e-6);
    REQUIRE (std::abs (db (peak (s.l, 24000) / 0.25) - 6.0) < 0.02);

    e.setOutput (6.0, true);
    Stereo t ((size_t) kFs);
    fillSine (t, 440.0, 0.25);
    run (e, t, 100);

    // Après la rampe, le signal est inversé : il est négativement corrélé à l'entrée d'origine.
    REQUIRE (std::abs (db (peak (t.l, 24000) / 0.25) - 6.0) < 0.02);
    double corr = 0.0;

    for (size_t k = 24000; k < t.l.size(); ++k)
        corr += (double) t.l[k] * (double) ref.l[k];

    REQUIRE (corr < 0.0);
}

TEST_CASE ("Bypass fades out to a bit exact copy and back", "[engine][bypass]")
{
    EqEngine e;
    e.prepare (kFs, 512);
    e.setBand (0, band (FilterType::Bell, 1000.0, 12.0, 2.0));
    e.setOutput (3.0, false);

    const size_t n = (size_t) kFs * 2;
    Stereo s (n);
    fillSine (s, 1000.0, 0.2);
    const Stereo ref = s;

    // 0,5 s traitées, bypass de 0,5 s à 1,5 s, puis reprise.
    const auto blockRun = [&] (size_t from, size_t to)
    {
        for (size_t pos = from; pos < to; pos += 64)
        {
            float* p[2] = { s.l.data() + pos, s.r.data() + pos };
            e.process (p, 2, (int) std::min<size_t> (64, to - pos));
        }
    };

    blockRun (0, 24000);
    REQUIRE (peak (s.l, 12000) > 0.2 * 3.0); // traité

    e.setBypass (true);
    blockRun (24000, 72000);

    // Après le fondu (20 ms = 960 échantillons), la sortie est exactement l'entrée.
    for (size_t i = 24000 + 1200; i < 72000; ++i)
        REQUIRE (s.l[i] == ref.l[i]);

    e.setBypass (false);
    blockRun (72000, n);
    REQUIRE (peak (s.l, 72000 + 2000) > 0.2 * 3.0); // retraité

    // Pas de saut brutal à l'entrée ni à la sortie du bypass.
    double maxJump = 0.0;

    for (size_t i = 1; i < n; ++i)
        maxJump = std::max (maxJump, (double) std::abs (s.l[i] - s.l[i - 1]));

    // Pente maximale d'un sinus de 1 kHz d'amplitude 0,2 amplifié de 12 dB (cloche) + 3 dB (sortie).
    REQUIRE (maxJump < 0.2 * std::pow (10.0, 15.0 / 20.0) * 2.0 * kPi * 1000.0 / kFs * 1.3);
}

TEST_CASE ("A frequency sweep from 20 Hz to 20 kHz in one second does not click", "[engine][clicks]")
{
    EqEngine e;
    e.prepare (kFs, 64);

    Stereo s ((size_t) kFs);
    fillSine (s, 440.0, 0.5);

    const int block = 64;
    const int n = (int) s.l.size();

    for (int pos = 0; pos < n; pos += block)
    {
        const double t = (double) pos / n;
        e.setBand (0, band (FilterType::Bell, 20.0 * std::pow (1000.0, t), 12.0, 4.0));
        float* p[2] = { s.l.data() + pos, s.r.data() + pos };
        e.process (p, 2, std::min (block, n - pos));
    }

    // Pente maximale d'un sinus de 440 Hz amplifié de 12 dB : au-delà, c'est un clic.
    double maxJump = 0.0;

    for (int i = 1; i < n; ++i)
        maxJump = std::max (maxJump, (double) std::abs (s.l[(size_t) i] - s.l[(size_t) i - 1]));

    const double bound = 0.5 * std::pow (10.0, 12.0 / 20.0) * 2.0 * kPi * 440.0 / kFs * 1.3;
    INFO ("maxJump=" << maxJump << " bound=" << bound);
    REQUIRE (maxJump < bound);
}

TEST_CASE ("Wild automation never produces NaN or runaway levels", "[engine][stability]")
{
    Rng rng;
    const FilterType types[] = { FilterType::Bell, FilterType::LowShelf, FilterType::HighShelf, FilterType::LowCut,
                                 FilterType::HighCut, FilterType::Notch, FilterType::BandPass };

    for (double fs : { 44100.0, 96000.0 })
    {
        EqEngine e;
        e.prepare (fs, 256);
        Stereo s ((size_t) (fs * 3.0));

        for (size_t i = 0; i < s.l.size(); ++i)
        {
            s.l[i] = (float) (0.2 * rng.next());
            s.r[i] = (float) (0.2 * rng.next());
        }

        const int n = (int) s.l.size();

        for (int pos = 0; pos < n; pos += 128)
        {
            for (int b = 0; b < EqEngine::kNumBands; ++b)
            {
                BandSettings bs;
                bs.used = rng.unit() > 0.2;
                bs.on = rng.unit() > 0.2;
                bs.filter.type = types[(size_t) (rng.unit() * 6.999)];
                bs.filter.freq = 10.0 * std::pow (3000.0, rng.unit());
                bs.filter.gainDb = 60.0 * (rng.unit() - 0.5);
                bs.filter.q = 0.025 * std::pow (1600.0, rng.unit());
                bs.filter.slopeDb = 6 * (1 + (int) (rng.unit() * 15.99));
                bs.placement = (Placement) (int) (rng.unit() * 4.99);
                e.setBand (b, bs);
            }

            e.setOutput (36.0 * (rng.unit() - 0.5), rng.unit() > 0.8);
            e.setBypass (rng.unit() > 0.9);
            float* p[2] = { s.l.data() + pos, s.r.data() + pos };
            e.process (p, 2, std::min (128, n - pos));
        }

        for (size_t i = 0; i < s.l.size(); ++i)
        {
            REQUIRE (std::isfinite (s.l[i]));
            REQUIRE (std::isfinite (s.r[i]));
        }
    }
}

TEST_CASE ("A gain change is ramped over about 20 ms", "[engine][clicks]")
{
    EqEngine e;
    e.prepare (kFs, 128);
    e.setBand (0, band (FilterType::Bell, 1000.0, 0.0, 2.0));

    const size_t n = (size_t) kFs;
    Stereo s (n);
    fillSine (s, 1000.0, 0.25);

    const size_t change = 24000; // à 0,5 s
    const auto runBlocks = [&] (size_t from, size_t to)
    {
        for (size_t pos = from; pos < to; pos += 64)
        {
            float* p[2] = { s.l.data() + pos, s.r.data() + pos };
            e.process (p, 2, (int) std::min<size_t> (64, to - pos));
        }
    };

    runBlocks (0, change);
    e.setBand (0, band (FilterType::Bell, 1000.0, 12.0, 2.0));
    runBlocks (change, n);

    const auto windowDb = [&] (size_t from, size_t to)
    {
        double m = 0.0;

        for (size_t i = from; i < to; ++i)
            m = std::max (m, (double) std::abs (s.l[i]));

        return db (m / 0.25);
    };

    const double first = windowDb (change, change + 48);            // la première milliseconde
    const double middle = windowDb (change + 480, change + 528);    // vers 10 ms
    const double last = windowDb (change + 1500, change + 1700);    // après 30 ms

    INFO ("1 ms: " << first << " dB, 10 ms: " << middle << " dB, 30 ms: " << last << " dB");
    REQUIRE (first < 3.0);                          // à peine commencé : pas de saut à +12 dB
    REQUIRE (middle > 3.0);
    REQUIRE (middle < 10.0);                        // en cours de rampe
    REQUIRE (std::abs (last - 12.0) < 0.1);         // arrivé
}
