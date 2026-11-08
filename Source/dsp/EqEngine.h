#pragma once

#include <array>
#include <vector>

#include "Biquad.h"
#include "FilterDesign.h"
#include "Info.h"

// Moteur de l'égaliseur : 24 bandes en série, stéréo ou mono, Mid/Side, lissage, bypass, gain de sortie.
// C++ pur, sans JUCE. Aucune allocation ni verrou dans process() : tout est préparé dans prepare().
namespace openfilter::dsp
{
    // Placement stéréo d'une bande (mêmes valeurs que params::Placement). Left et Right sont prévus en v1 mais
    // déjà gérés ici ; sur une piste mono, toutes les bandes traitent l'unique canal.
    enum class Placement { Stereo = 0, Left, Right, Mid, Side };

    struct BandSettings
    {
        bool used = false;     // la bande existe dans l'interface
        bool on = true;        // active (sinon bypassée)
        FilterParams filter;
        Placement placement = Placement::Stereo;
    };

    class EqEngine
    {
    public:
        static constexpr int kNumBands = openfilter::kMaxBands;
        static constexpr int kControlBlock = 32;     // les coefficients se recalculent tous les 32 échantillons
        static constexpr double kRampSeconds = 0.020; // rampe de lissage d'environ 20 ms

        // Thread de préparation (jamais dans le thread audio). Réinitialise tous les états.
        void prepare (double sampleRate, int maxBlockSize);
        void reset() noexcept;

        // Réglages : appelables à chaque bloc depuis le thread audio. Un changement de fréquence, de gain ou de Q
        // est lissé ; un changement de type, de pente ou de placement s'applique d'un coup (états remis à zéro).
        void setBand (int index, const BandSettings& settings) noexcept;
        void setOutput (double gainDb, bool invertPhase) noexcept;
        void setBypass (bool bypass) noexcept;

        // Traite en place. numChannels : 1 (mono) ou 2 (stéréo) ; au-delà, les canaux en trop sont laissés tels quels.
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

    private:
        struct Band
        {
            BandSettings target;
            bool initialised = false;

            double curFreq = 1000.0, curGain = 0.0, curQ = 1.0; // valeurs lissées
            double stepsLeft = 0.0;                              // blocs de contrôle restants dans la rampe
            double mix = 0.0, mixTarget = 0.0;                   // fondu actif / bypass de la bande

            FilterStages stages;
            std::array<std::array<Biquad, 2>, kMaxStages> biquads {}; // [étage][canal 0 ou 1]

            // Dernier calcul de coefficients, pour ne recalculer que si quelque chose a bougé.
            bool designed = false;
            double dFreq = 0.0, dGain = 0.0, dQ = 0.0;
        };

        void snap (Band& b) noexcept;
        void controlStep (Band& b, double steps) noexcept;
        void redesign (Band& b) noexcept;
        void resetStates() noexcept;
        void processChunk (float* const* channels, int numChannels, int offset, int length) noexcept;

        double sampleRate = 48000.0;
        int maxBlock = 512;
        double rampSteps = 30.0;     // kRampSeconds en blocs de contrôle
        double rampSamples = 960.0;  // kRampSeconds en échantillons

        std::array<Band, kNumBands> bands;

        // Sortie : gain linéaire (avec le signe de l'inversion de phase) lissé, et bypass global (fondu).
        double gainCur = 1.0, gainTarget = 1.0, gainSamplesLeft = 0.0;
        double bypassMix = 1.0, bypassTarget = 1.0; // 1 = traité, 0 = bypass
        bool statesClear = true;

        std::vector<double> work[2];
    };
}
