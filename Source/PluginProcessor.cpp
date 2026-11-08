#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/Info.h"

OpenFilterProcessor::OpenFilterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false))
{
}

const juce::String OpenFilterProcessor::getName() const { return juce::String (openfilter::kName.data(), openfilter::kName.size()); }
bool OpenFilterProcessor::acceptsMidi() const           { return false; }
bool OpenFilterProcessor::producesMidi() const          { return false; }
double OpenFilterProcessor::getTailLengthSeconds() const { return 0.0; }

int OpenFilterProcessor::getNumPrograms()                              { return 1; }
int OpenFilterProcessor::getCurrentProgram()                           { return 0; }
void OpenFilterProcessor::setCurrentProgram (int)                      {}
const juce::String OpenFilterProcessor::getProgramName (int)           { return {}; }
void OpenFilterProcessor::changeProgramName (int, const juce::String&) {}

bool OpenFilterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    // Entrée et sortie principales : mono ou stéréo, identiques.
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn != mainOut)
        return false;

    // Sidechain : absent ou stéréo.
    const auto& sidechain = layouts.getChannelSet (true, 1);
    return sidechain.isDisabled() || sidechain == juce::AudioChannelSet::stereo();
}

void OpenFilterProcessor::prepareToPlay (double, int) {}
void OpenFilterProcessor::releaseResources() {}

void OpenFilterProcessor::processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Passe-plat : le traitement se fait en place sur le bus principal, rien à faire.
    // Le bus sidechain (s'il est actif) se trouve dans les canaux suivants du tampon.
}

bool OpenFilterProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* OpenFilterProcessor::createEditor() { return new OpenFilterEditor (*this); }

void OpenFilterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // État minimal versionné. Les paramètres (APVTS) arrivent à l'étape 3.
    juce::XmlElement xml ("OpenFilterState");
    xml.setAttribute ("version", 1);
    copyXmlToBinary (xml, destData);
}

void OpenFilterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ignoreUnused (data, sizeInBytes);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OpenFilterProcessor();
}
