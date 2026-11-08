#pragma once

#include <string_view>

// Constantes d'identité du plugin, sans dépendance à JUCE (utilisables dans les tests du DSP).
namespace openfilter
{
    inline constexpr std::string_view kName    = "Open Filter";
    inline constexpr std::string_view kVersion = "0.1.0";
    inline constexpr int kMaxBands = 24;
}
