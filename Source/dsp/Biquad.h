#pragma once

#include <cmath>

// Biquad en double, forme transposée directe II (TDF-II). C++ pur : aucune dépendance à JUCE.
namespace openfilter::dsp
{
    // Coefficients normalisés (a0 = 1) : H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)
    struct BiquadCoeffs
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

        bool isFinite() const noexcept
        {
            return std::isfinite (b0) && std::isfinite (b1) && std::isfinite (b2)
                && std::isfinite (a1) && std::isfinite (a2);
        }

        // Pôles strictement à l'intérieur du cercle unité (triangle de stabilité).
        bool isStable() const noexcept
        {
            return std::abs (a2) < 1.0 && std::abs (a1) < 1.0 + a2;
        }
    };

    class Biquad
    {
    public:
        void setCoeffs (const BiquadCoeffs& c) noexcept { coeffs = c; }
        const BiquadCoeffs& getCoeffs() const noexcept  { return coeffs; }
        void reset() noexcept                           { s1 = s2 = 0.0; }

        double process (double x) noexcept
        {
            const double y = coeffs.b0 * x + s1;
            s1 = coeffs.b1 * x - coeffs.a1 * y + s2;
            s2 = coeffs.b2 * x - coeffs.a2 * y;
            return y;
        }

        // Traite un bloc float en place ou non (in et out peuvent être identiques). Calcul interne en double.
        void processBlock (const float* in, float* out, int numSamples) noexcept
        {
            for (int i = 0; i < numSamples; ++i)
                out[i] = static_cast<float> (process (static_cast<double> (in[i])));
        }

    private:
        BiquadCoeffs coeffs;
        double s1 = 0.0, s2 = 0.0;
    };
}
