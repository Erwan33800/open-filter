#pragma once

#include <string>
#include <vector>

// Définition des paramètres exposés à l'hôte (Live) : identifiants, plages, valeurs par défaut, formats.
// C++ pur, sans JUCE : testable seul. La couche JUCE (ParameterLayout.cpp) en construit l'APVTS.
//
// RÈGLE : ne jamais renommer, supprimer ni réordonner un identifiant ou un choix publié. L'automation
// enregistrée dans les projets Live en dépend (un choix est automatisé par son index normalisé : en ajouter
// un au milieu décalerait toutes les valeurs). Tout nouveau paramètre prend un nouvel identifiant ; tout
// nouveau choix s'ajoute à la fin. C'est pourquoi les types et placements prévus en v1 / v2 sont déjà listés.
namespace openfilter::params
{
    enum class Kind { Float, Bool, Choice };
    enum class Unit { None, Hz, Db, Q, Ms, Ratio, Percent };

    struct ParamSpec
    {
        std::string id;                  // « b3_freq »
        std::string name;                // « B3 Freq » (nom affiché dans Live)
        Kind kind = Kind::Float;
        Unit unit = Unit::None;
        double min = 0.0, max = 1.0, def = 0.0;
        double centre = 0.0;             // valeur placée au milieu de la course (échelle log) ; 0 = linéaire
        std::vector<std::string> choices;
        bool automatable = true;
    };

    inline constexpr int kNumBands = 24;
    inline constexpr int kNumMacros = 8;

    // Indices des choix de « type » (publiés : ne pas réordonner). Tilt et AllPass sont réservés (v1 / v2).
    enum class BandType { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, Tilt, AllPass };
    // Placement stéréo. Left et Right sont réservés (v1).
    enum class Placement { Stereo = 0, Left, Right, Mid, Side };

    // Tous les paramètres : 24 bandes, puis les globaux, puis les 8 macros. L'ordre est stable.
    const std::vector<ParamSpec>& allParams();

    // Identifiant d'un réglage de bande : bandParamId (3, "freq") -> « b3_freq » (bande de 1 à 24).
    std::string bandParamId (int band, const std::string& param);

    // Recherche par identifiant (nullptr si inconnu).
    const ParamSpec* findParam (const std::string& id);

    // Exposant de courbe qui place `centre` au milieu de la course (même formule que juce::NormalisableRange).
    double skewForCentre (double min, double max, double centre);
    double toNormalised (const ParamSpec& spec, double value);
    double fromNormalised (const ParamSpec& spec, double normalised);

    // Texte affiché dans Live (« 80.0 Hz », « 2.50 kHz », « +3.0 dB », « 12.0 ms », « 4.0:1 »).
    std::string formatValue (const ParamSpec& spec, double value);

    // Lecture d'une saisie (« 2k », « 2.5 kHz », « 440 », « -6 dB », « 2x » = +6 dB). false si illisible.
    bool parseValue (const ParamSpec& spec, const std::string& text, double& value);
}
