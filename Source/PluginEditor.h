#pragma once

#include "open/ParamBridgeEditor.h"
#include "PluginProcessor.h"

// Fenêtre du plugin : l'interface Open Filter (HTML/JS) dans une WebView2, reliée aux 419 paramètres.
// Tout le pont (gestes d'automation, surveillance des valeurs, snapshot) est dans open::ParamBridgeEditor (open-core) ;
// le protocole est décrit dans open-core/ui/bridge-core.js, la partie propre à Open Filter dans ui/src/bridge.js.
class OpenFilterEditor final : public open::ParamBridgeEditor
{
public:
    explicit OpenFilterEditor (OpenFilterProcessor&);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenFilterEditor)
};
