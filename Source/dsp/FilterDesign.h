#pragma once

#include <array>
#include "Biquad.h"

// Calcul des coefficients des filtres de la bande. C++ pur, sans allocation : utilisable dans le thread audio.
namespace openfilter::dsp
{
    enum class FilterType
    {
        Bell,       // cloche
        LowShelf,   // plateau grave
        HighShelf,  // plateau aigu
        LowCut,     // coupe-bas (passe-haut), pente 6 à 96 dB/oct
        HighCut,    // coupe-haut (passe-bas), pente 6 à 96 dB/oct
        Notch,
        BandPass
    };

    // 96 dB/oct = ordre 16 = 8 biquads au maximum (un ordre impair ajoute un étage d'ordre 1, soit encore 8 au plus).
    inline constexpr int kMaxStages = 8;

    struct FilterParams
    {
        FilterType type = FilterType::Bell;
        double freq    = 1000.0;  // Hz, ramené dans [10 Hz ; 0,49 x fe]
        double gainDb  = 0.0;     // cloche et plateaux seulement
        double q       = 1.0;     // [0,025 ; 40]. Pour les coupes : résonance, appliquée au dernier étage seulement
        int slopeDb    = 12;      // coupes seulement : 6 à 96 par pas de 6
    };

    struct FilterStages
    {
        std::array<BiquadCoeffs, kMaxStages> stage {};
        int count = 0; // 0 = bande neutre : le signal passe inchangé, au bit près
    };

    // Calcule la cascade d'étages d'une bande pour la fréquence d'échantillonnage donnée.
    //
    // Cloche et plateaux : réponse « matched » (poles de l'analogique ramenés par exp(sT), numérateur ajusté pour
    // retrouver le gain analogique à 0 Hz, à la fréquence centrale et à Nyquist, d'après Vicanek, « Matched Second
    // Order Digital Filters »). Une atténuation est l'inverse exact de l'amplification de même réglage.
    // Coupe-haut et passe-bande : même méthode. Coupe-bas et notch : bilinéaire (RBJ) avec pré-déformation.
    // Si la méthode « matched » échoue (instable, impossible), repli sur le RBJ, toujours stable.
    FilterStages designFilter (const FilterParams& params, double sampleRate) noexcept;

    // Réponse en amplitude (dB) d'un étage, d'une cascade, et de la référence analogique idéale.
    double magnitudeDb (const BiquadCoeffs& c, double freq, double sampleRate) noexcept;
    double magnitudeDb (const FilterStages& s, double freq, double sampleRate) noexcept;
    double analogMagnitudeDb (const FilterParams& params, double freq) noexcept;

    // Bornes appliquées aux paramètres avant le calcul.
    double clampFreq (double freq, double sampleRate) noexcept;
    double clampQ (double q) noexcept;
    int clampSlope (int slopeDb) noexcept;
}
