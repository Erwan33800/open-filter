#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace openfilter
{
    // Construit tous les paramètres du plugin (24 bandes, globaux, 8 macros) à partir de params::allParams().
    // Chaque ParameterID porte la version 1 : voir la règle de stabilité dans params/ParamSpec.h.
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
