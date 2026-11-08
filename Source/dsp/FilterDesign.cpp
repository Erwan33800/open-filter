#include "FilterDesign.h"

#include <algorithm>
#include <cmath>

namespace openfilter::dsp
{
namespace
{
    constexpr double kPi       = 3.14159265358979323846;
    constexpr double kInvSqrt2 = 0.70710678118654752440;

    double sq (double v) noexcept { return v * v; }

    // ------------------------------------------------------------------------------------------
    // Référence analogique : |H(jx)|^2 avec x = f / f0 (prototypes du RBJ Audio EQ Cookbook).
    // A = 10^(gain dB / 40). Le gain linéaire au centre d'une cloche vaut A^2.
    // ------------------------------------------------------------------------------------------
    double analogPower (FilterType type, double x, double A, double q) noexcept
    {
        const double x2 = x * x;
        const double d  = sq (1.0 - x2) + x2 / (q * q);

        switch (type)
        {
            case FilterType::Bell:
                return (sq (1.0 - x2) + sq (x * A / q)) / (sq (1.0 - x2) + sq (x / (A * q)));
            case FilterType::LowShelf:
                return A * A * (sq (A - x2) + x2 * A / (q * q)) / (sq (1.0 - A * x2) + x2 * A / (q * q));
            case FilterType::HighShelf:
                return A * A * (sq (1.0 - A * x2) + x2 * A / (q * q)) / (sq (A - x2) + x2 * A / (q * q));
            case FilterType::LowCut:   return x2 * x2 / d;
            case FilterType::HighCut:  return 1.0 / d;
            case FilterType::Notch:    return sq (1.0 - x2) / d;
            case FilterType::BandPass: return (x2 / (q * q)) / d;
        }
        return 1.0;
    }

    double firstOrderPower (FilterType type, double x) noexcept
    {
        const double x2 = x * x;
        return type == FilterType::LowCut ? x2 / (1.0 + x2) : 1.0 / (1.0 + x2);
    }

    // ------------------------------------------------------------------------------------------
    // Bilinéaire avec pré-déformation à f0 (RBJ). Toujours stable, exact à f0, trop « serré » près de Nyquist.
    // ------------------------------------------------------------------------------------------
    BiquadCoeffs rbj (FilterType type, double f0, double gainDb, double q, double fs) noexcept
    {
        const double w0 = 2.0 * kPi * f0 / fs, cw = std::cos (w0), sw = std::sin (w0);
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double al = sw / (2.0 * q);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

        switch (type)
        {
            case FilterType::Bell:
                b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A;
                a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A;
                break;
            case FilterType::LowShelf:
            {
                const double s = 2.0 * std::sqrt (A) * al;
                b0 = A * ((A + 1) - (A - 1) * cw + s);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - s);
                a0 = (A + 1) + (A - 1) * cw + s;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case FilterType::HighShelf:
            {
                const double s = 2.0 * std::sqrt (A) * al;
                b0 = A * ((A + 1) + (A - 1) * cw + s);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - s);
                a0 = (A + 1) - (A - 1) * cw + s;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
            case FilterType::LowCut:
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
                a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al;
                break;
            case FilterType::HighCut:
                b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
                a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al;
                break;
            case FilterType::Notch:
                b0 = 1; b1 = -2 * cw; b2 = 1;
                a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al;
                break;
            case FilterType::BandPass: // gain de crête 0 dB
                b0 = al; b1 = 0; b2 = -al;
                a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al;
                break;
        }

        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    // Étage d'ordre 1 (bilinéaire pré-déformé), pour les pentes de 6, 18, 30... dB/oct.
    BiquadCoeffs firstOrder (FilterType type, double f0, double fs) noexcept
    {
        const double K = std::tan (kPi * f0 / fs);
        const double n = 1.0 / (1.0 + K);
        BiquadCoeffs c;
        c.a1 = (K - 1.0) * n;
        c.a2 = 0.0;
        c.b2 = 0.0;

        if (type == FilterType::LowCut)
        {
            c.b0 = n;
            c.b1 = -n;
        }
        else
        {
            // Coupe-haut : le bilinéaire écrase la pente près de Nyquist (1,6 dB d'écart pour f0 = 100 Hz). On impose
            // donc le gain analogique à 0 Hz (1), à f0 (-3,01 dB) et à Nyquist. Avec S = 1 + a1 et D = hNy (1 - a1)
            // (gains à 0 Hz et à Nyquist), la condition à f0 donne P a1^2 + Q a1 + P = 0 : deux racines inverses l'une
            // de l'autre, on garde celle de module < 1. En cas d'échec, on reste sur le bilinéaire ci-dessus.
            const double w0 = 2.0 * kPi * f0 / fs, cw = std::cos (w0);
            const double h2 = firstOrderPower (type, fs / (2.0 * f0));  // |H|^2 à Nyquist
            const double hm2 = 0.5;                                      // |H|^2 à f0
            const double P = (1.0 + cw) / 2.0 + (1.0 - cw) / 2.0 * h2 - hm2;
            const double Q = (1.0 + cw) - (1.0 - cw) * h2 - 2.0 * hm2 * cw;

            double a = c.a1;
            bool ok = false;

            if (std::abs (P) > 1.0e-12)
            {
                const double disc = Q * Q - 4.0 * P * P;

                if (disc >= 0.0)
                {
                    const double r1 = (-Q + std::sqrt (disc)) / (2.0 * P), r2 = (-Q - std::sqrt (disc)) / (2.0 * P);
                    a = std::abs (r1) < std::abs (r2) ? r1 : r2;
                    ok = std::abs (a) < 1.0;
                }
            }

            if (ok)
            {
                const double S = 1.0 + a, D = std::sqrt (h2) * (1.0 - a);
                c.a1 = a;
                c.b0 = (S + D) / 2.0;
                c.b1 = (S - D) / 2.0;
            }
            else
            {
                const double hNy = std::sqrt (h2);
                c.b0 = ((1.0 + c.a1) + hNy * (1.0 - c.a1)) / 2.0;
                c.b1 = ((1.0 + c.a1) - hNy * (1.0 - c.a1)) / 2.0;
            }
        }
        return c;
    }

    // ------------------------------------------------------------------------------------------
    // Méthode « matched » (Vicanek). Les pôles du prototype analogique (pulsation wpT en rad/échantillon,
    // amortissement zeta) sont ramenés par z = exp(sT). Le numérateur est ajusté pour que |H|^2 numérique
    // égale |H|^2 analogique à 0 Hz (hDc2), à Nyquist (hNy2) et à la pulsation wmT (hm2).
    // Renvoie false si l'ajustement est impossible ou instable : l'appelant se replie alors sur le RBJ.
    // ------------------------------------------------------------------------------------------
    bool matched (double wpT, double zeta, double hDc2, double hNy2, double wmT, double hm2, BiquadCoeffs& out) noexcept
    {
        const double e = std::exp (-zeta * wpT);
        double a1;

        if (zeta < 1.0)       a1 = -2.0 * e * std::cos (std::sqrt (1.0 - zeta * zeta) * wpT);
        else if (zeta == 1.0) a1 = -2.0 * e;
        else                  a1 = -2.0 * e * std::cosh (std::sqrt (zeta * zeta - 1.0) * wpT);

        const double a2 = e * e;

        const double A0 = sq (1.0 + a1 + a2), A1 = sq (1.0 - a1 + a2), A2 = -4.0 * a2;
        const double s2   = sq (std::sin (wmT / 2.0));
        const double phi0 = 1.0 - s2, phi1 = s2, phi2 = 4.0 * phi0 * phi1;

        if (! (phi2 > 1.0e-12))
            return false;

        const double Am = A0 * phi0 + A1 * phi1 + A2 * phi2;
        const double B0 = A0 * hDc2;
        const double B1 = A1 * hNy2;
        const double B2 = (hm2 * Am - B0 * phi0 - B1 * phi1) / phi2;

        if (! (B0 >= 0.0 && B1 >= 0.0))
            return false;

        const double W    = (std::sqrt (B0) + std::sqrt (B1)) / 2.0;
        const double disc = W * W + B2;

        if (disc < -1.0e-12)
            return false;

        const double b0 = (W + std::sqrt (std::max (0.0, disc))) / 2.0;

        if (! (b0 > 1.0e-12))
            return false;

        BiquadCoeffs c;
        c.b0 = b0;
        c.b1 = (std::sqrt (B0) - std::sqrt (B1)) / 2.0;
        c.b2 = -B2 / (4.0 * b0);
        c.a1 = a1;
        c.a2 = a2;

        if (! c.isFinite() || ! c.isStable())
            return false;

        out = c;
        return true;
    }

    // Inverse d'un filtre : échange numérateur et dénominateur. Exige un numérateur à phase minimale.
    bool invert (const BiquadCoeffs& c, BiquadCoeffs& out) noexcept
    {
        if (! (std::abs (c.b0) > 1.0e-12))
            return false;

        BiquadCoeffs r;
        r.b0 = 1.0 / c.b0;
        r.b1 = c.a1 / c.b0;
        r.b2 = c.a2 / c.b0;
        r.a1 = c.b1 / c.b0;
        r.a2 = c.b2 / c.b0;

        if (! r.isFinite() || ! r.isStable())
            return false;

        out = r;
        return true;
    }

    BiquadCoeffs scaled (BiquadCoeffs c, double g) noexcept
    {
        c.b0 *= g; c.b1 *= g; c.b2 *= g;
        return c;
    }

    // Amplification (gainDb >= 0) matched, ou RBJ en repli.
    BiquadCoeffs bellBoost (double f0, double gainDb, double q, double fs) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w0T = 2.0 * kPi * f0 / fs;
        const double hNy = analogPower (FilterType::Bell, fs / (2.0 * f0), A, q);
        BiquadCoeffs c;
        if (matched (w0T, 1.0 / (2.0 * A * q), 1.0, hNy, w0T, sq (sq (A)), c))
            return c;
        return rbj (FilterType::Bell, f0, gainDb, q, fs);
    }

    BiquadCoeffs lowShelfBoost (double f0, double gainDb, double q, double fs) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w0T = 2.0 * kPi * f0 / fs;
        const double hNy = analogPower (FilterType::LowShelf, fs / (2.0 * f0), A, q);
        BiquadCoeffs c;
        if (matched (w0T / std::sqrt (A), 1.0 / (2.0 * q), sq (sq (A)), hNy, w0T, A * A, c))
            return c;
        return rbj (FilterType::LowShelf, f0, gainDb, q, fs);
    }

    // Une atténuation est l'inverse exact de l'amplification de même valeur absolue (symétrie du RBJ).
    template <typename BoostFn>
    BiquadCoeffs signedGain (BoostFn boost, FilterType type, double f0, double gainDb, double q, double fs) noexcept
    {
        BiquadCoeffs c = boost (f0, std::abs (gainDb), q, fs);

        if (gainDb < 0.0)
        {
            BiquadCoeffs inv;
            if (invert (c, inv))
                return inv;
            return rbj (type, f0, gainDb, q, fs);
        }
        return c;
    }

    BiquadCoeffs bell (double f0, double gainDb, double q, double fs) noexcept
    {
        return signedGain (bellBoost, FilterType::Bell, f0, gainDb, q, fs);
    }

    BiquadCoeffs lowShelf (double f0, double gainDb, double q, double fs) noexcept
    {
        return signedGain (lowShelfBoost, FilterType::LowShelf, f0, gainDb, q, fs);
    }

    // Identité exacte du RBJ : plateau aigu (g) = gain linéaire 10^(g/20) x plateau grave (-g).
    BiquadCoeffs highShelf (double f0, double gainDb, double q, double fs) noexcept
    {
        const BiquadCoeffs c = scaled (lowShelf (f0, -gainDb, q, fs), std::pow (10.0, gainDb / 20.0));
        return c.isFinite() && c.isStable() ? c : rbj (FilterType::HighShelf, f0, gainDb, q, fs);
    }

    BiquadCoeffs highCutBiquad (double f0, double q, double fs) noexcept
    {
        const double w0T = 2.0 * kPi * f0 / fs;
        const double hNy = analogPower (FilterType::HighCut, fs / (2.0 * f0), 1.0, q);
        BiquadCoeffs c;
        if (matched (w0T, 1.0 / (2.0 * q), 1.0, hNy, w0T, q * q, c))
            return c;
        return rbj (FilterType::HighCut, f0, 0.0, q, fs);
    }

    BiquadCoeffs bandPass (double f0, double q, double fs) noexcept
    {
        const double w0T = 2.0 * kPi * f0 / fs;
        const double hNy = analogPower (FilterType::BandPass, fs / (2.0 * f0), 1.0, q);
        BiquadCoeffs c;
        if (matched (w0T, 1.0 / (2.0 * q), 0.0, hNy, w0T, 1.0, c))
            return c;
        return rbj (FilterType::BandPass, f0, 0.0, q, fs);
    }

    // Butterworth d'ordre n : Q_k = 1 / (2 sin((2k-1) pi / (2n))), k = 1 (le plus résonant) à n/2.
    // La résonance demandée ne s'applique qu'au dernier étage (k = 1) : Q demandé 0,7071 = Butterworth pur.
    double cutStageQ (int k, int n, double userQ) noexcept
    {
        double qk = 1.0 / (2.0 * std::sin ((2.0 * k - 1.0) * kPi / (2.0 * n)));

        if (k == 1)
            qk *= userQ / kInvSqrt2;

        return std::clamp (qk, 0.025, 100.0);
    }

    FilterStages cutCascade (FilterType type, double f0, double q, int slopeDb, double fs) noexcept
    {
        const int n = slopeDb / 6;
        FilterStages s;

        if (n % 2 == 1)
            s.stage[(size_t) s.count++] = firstOrder (type, f0, fs);

        // Étages du moins résonant au plus résonant : le dernier porte la résonance.
        for (int k = n / 2; k >= 1; --k)
        {
            const double qk = cutStageQ (k, n, q);
            s.stage[(size_t) s.count++] = type == FilterType::LowCut ? rbj (FilterType::LowCut, f0, 0.0, qk, fs)
                                                                      : highCutBiquad (f0, qk, fs);
        }
        return s;
    }

    bool hasGain (FilterType type) noexcept
    {
        return type == FilterType::Bell || type == FilterType::LowShelf || type == FilterType::HighShelf;
    }
}

double clampFreq (double freq, double sampleRate) noexcept
{
    return std::clamp (freq, 10.0, 0.49 * sampleRate);
}

double clampQ (double q) noexcept
{
    return std::clamp (q, 0.025, 40.0);
}

int clampSlope (int slopeDb) noexcept
{
    return std::clamp (((slopeDb + 3) / 6) * 6, 6, 96);
}

FilterStages designFilter (const FilterParams& params, double sampleRate) noexcept
{
    const double f0 = clampFreq (params.freq, sampleRate);
    const double q  = clampQ (params.q);
    FilterStages s;

    if (hasGain (params.type) && std::abs (params.gainDb) < 1.0e-6)
        return s; // bande à 0 dB : retirée du traitement

    switch (params.type)
    {
        case FilterType::Bell:      s.stage[0] = bell (f0, params.gainDb, q, sampleRate);      s.count = 1; break;
        case FilterType::LowShelf:  s.stage[0] = lowShelf (f0, params.gainDb, q, sampleRate);  s.count = 1; break;
        case FilterType::HighShelf: s.stage[0] = highShelf (f0, params.gainDb, q, sampleRate); s.count = 1; break;
        case FilterType::LowCut:
        case FilterType::HighCut:   return cutCascade (params.type, f0, q, clampSlope (params.slopeDb), sampleRate);
        case FilterType::Notch:     s.stage[0] = rbj (FilterType::Notch, f0, 0.0, q, sampleRate); s.count = 1; break;
        case FilterType::BandPass:  s.stage[0] = bandPass (f0, q, sampleRate);                  s.count = 1; break;
    }
    return s;
}

double magnitudeDb (const BiquadCoeffs& c, double freq, double sampleRate) noexcept
{
    const double w  = 2.0 * kPi * freq / sampleRate;
    const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
    const double nr = c.b0 + c.b1 * c1 + c.b2 * c2, ni = -(c.b1 * s1 + c.b2 * s2);
    const double dr = 1.0 + c.a1 * c1 + c.a2 * c2,  di = -(c.a1 * s1 + c.a2 * s2);
    return 10.0 * std::log10 ((nr * nr + ni * ni) / (dr * dr + di * di) + 1.0e-300);
}

double magnitudeDb (const FilterStages& s, double freq, double sampleRate) noexcept
{
    double db = 0.0;

    for (int i = 0; i < s.count; ++i)
        db += magnitudeDb (s.stage[(size_t) i], freq, sampleRate);

    return db;
}

double analogMagnitudeDb (const FilterParams& params, double freq) noexcept
{
    const double f0 = std::max (params.freq, 1.0e-3);
    const double q  = clampQ (params.q);
    const double x  = freq / f0;

    if (params.type == FilterType::LowCut || params.type == FilterType::HighCut)
    {
        const int n = clampSlope (params.slopeDb) / 6;
        double p = 1.0;

        if (n % 2 == 1)
            p *= firstOrderPower (params.type, x);

        for (int k = n / 2; k >= 1; --k)
            p *= analogPower (params.type, x, 1.0, cutStageQ (k, n, q));

        return 10.0 * std::log10 (p + 1.0e-300);
    }

    const double A = std::pow (10.0, (hasGain (params.type) ? params.gainDb : 0.0) / 40.0);
    return 10.0 * std::log10 (analogPower (params.type, x, A, q) + 1.0e-300);
}

}
