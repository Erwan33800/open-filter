#pragma once

#include <atomic>
#include <set>
#include <unordered_map>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// Fenêtre du plugin : l'interface Open Filter (HTML/JS) dans une WebView2, reliée aux paramètres.
//
// Pont : événement JUCE « openfilter », tableau [commande, ...arguments] (le détail est dans ui/src/bridge.js).
//   Page -> plugin : ready ; begin <id> ; set <id> <valeur> ; end <id> ; ui <json>
//   Plugin -> page : snapshot {id: valeur} <json> ; params [[id, valeur], ...]
// Les valeurs échangées sont les vraies valeurs des paramètres (Hz, dB, index de choix), jamais normalisées.
class OpenFilterEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit OpenFilterEditor (OpenFilterProcessor&);
    ~OpenFilterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void handleMessage (const juce::var& message);
    void sendSnapshot();
    void send (const juce::Array<juce::var>& message);
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    OpenFilterProcessor& processor;
    juce::WebBrowserComponent browser;

    // Surveillance des paramètres (automation de l'hôte, chargement d'un preset de Live...) : on compare 30 fois par
    // seconde les valeurs atomiques à celles déjà connues de la page. Aucun écouteur : rien à faire dans le thread audio.
    std::vector<juce::String> paramIds;
    std::vector<std::atomic<float>*> paramValues;
    std::vector<float> lastSent;
    std::unordered_map<std::string, size_t> paramIndex;

    std::set<juce::String> activeGestures; // gestes d'automation ouverts par la page, refermés si la fenêtre se ferme
    bool pageReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenFilterEditor)
};
