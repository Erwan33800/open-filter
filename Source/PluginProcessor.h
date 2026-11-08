#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Effet audio Open Filter. Étape 1 : passe-plat. Entrée et sortie mono ou stéréo,
// plus un bus d'entrée « Sidechain » stéréo optionnel (menu Sidechain du device dans Live).
class OpenFilterProcessor final : public juce::AudioProcessor
{
public:
    OpenFilterProcessor();
    ~OpenFilterProcessor() override = default;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool hasEditor() const override;
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Tous les paramètres (bandes, globaux, macros). Les valeurs lues par le thread audio passent par
    // getRawParameterValue(), atomique : aucun verrou.
    juce::AudioProcessorValueTreeState apvts;

    // Réglages d'interface sauvegardés avec le set (taille de fenêtre, analyseur, assignations des macros...),
    // sous forme de JSON fourni par la page. Thread message uniquement.
    juce::String getUiState() const        { return uiState; }
    void setUiState (const juce::String& json) { uiState = json; }

private:
    juce::String uiState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenFilterProcessor)
};
