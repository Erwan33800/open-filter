#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include "PluginEditor.h"
#include "params/ParameterLayout.h"
#include "dsp/Info.h"
#include "params/ParamSpec.h"

OpenFilterProcessor::OpenFilterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "OpenFilterParams", openfilter::createParameterLayout())
{
    for (int n = 1; n <= openfilter::dsp::EqEngine::kNumBands; ++n)
    {
        const auto raw = [this, n] (const char* name)
        {
            return apvts.getRawParameterValue (openfilter::params::bandParamId (n, name).c_str());
        };
        auto& bp = bandParams[(size_t) (n - 1)];
        bp.used = raw ("used"); bp.on = raw ("on");
        bp.freq = raw ("freq"); bp.gain = raw ("gain"); bp.q = raw ("q");
        bp.type = raw ("type"); bp.slope = raw ("slope"); bp.place = raw ("place");
    }

    outGainParam     = apvts.getRawParameterValue ("out_gain");
    bypassParam      = apvts.getRawParameterValue ("bypass");
    phaseInvertParam = apvts.getRawParameterValue ("phase_invert");
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

void OpenFilterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Toute la mémoire du moteur est allouée ici, jamais dans processBlock.
    engine.prepare (sampleRate, samplesPerBlock);
    pushParametersToEngine(); // les bandes démarrent directement à leurs valeurs, sans rampe
}

void OpenFilterProcessor::releaseResources() {}

// Traduit les valeurs des paramètres en réglages du moteur (appelé à chaque bloc : atomiques, sans allocation).
void OpenFilterProcessor::pushParametersToEngine() noexcept
{
    using namespace openfilter;

    // Index du paramètre « type » -> type de filtre du moteur. Tilt et All Pass (réservés) ne sont pas encore gérés.
    constexpr dsp::FilterType kTypes[] = { dsp::FilterType::Bell, dsp::FilterType::LowShelf, dsp::FilterType::HighShelf,
                                           dsp::FilterType::LowCut, dsp::FilterType::HighCut, dsp::FilterType::Notch,
                                           dsp::FilterType::BandPass };

    for (int i = 0; i < dsp::EqEngine::kNumBands; ++i)
    {
        const auto& bp = bandParams[(size_t) i];
        dsp::BandSettings s;

        const int type = (int) std::lround (bp.type->load());
        s.used = bp.used->load() >= 0.5f && type >= 0 && type < 7;
        s.on = bp.on->load() >= 0.5f;
        s.filter.type = kTypes[std::clamp (type, 0, 6)];
        s.filter.freq = (double) bp.freq->load();
        s.filter.gainDb = (double) bp.gain->load();
        s.filter.q = (double) bp.q->load();

        const int slopeIndex = (int) std::lround (bp.slope->load());
        s.filter.slopeDb = slopeIndex >= 15 ? 96 : (slopeIndex + 1) * 6; // 16e choix = Brickwall (réservé) : 96 dB/oct
        s.placement = (dsp::Placement) std::clamp ((int) std::lround (bp.place->load()), 0, 4);

        engine.setBand (i, s);
    }

    engine.setOutput ((double) outGainParam->load(), phaseInvertParam->load() >= 0.5f);
    engine.setBypass (bypassParam->load() >= 0.5f);
}

void OpenFilterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Bus principal (entrée et sortie partagent les mêmes canaux, traitement en place). Le bus sidechain,
    // s'il est actif, vient après dans le tampon : il servira à la détection de la dynamique (étape 5).
    auto main = getBusBuffer (buffer, true, 0);

    if (main.getNumChannels() < 1 || main.getNumSamples() < 1)
        return;

    pushParametersToEngine();
    engine.process (main.getArrayOfWritePointers(), main.getNumChannels(), main.getNumSamples());
}

bool OpenFilterProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* OpenFilterProcessor::createEditor() { return new OpenFilterEditor (*this); }

// État sauvegardé avec le set : <OpenFilterState version="1"> contient l'APVTS (<OpenFilterParams>)
// et, dans un nœud séparé, les réglages d'interface (<UI json="...">). Le numéro de version permettra
// de migrer d'anciens états si le format change.
void OpenFilterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("OpenFilterState");
    xml.setAttribute ("version", 1);

    if (auto params = apvts.copyState().createXml())
        xml.addChildElement (params.release());

    xml.createNewChildElement ("UI")->setAttribute ("json", uiState);
    copyXmlToBinary (xml, destData);
}

void OpenFilterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName ("OpenFilterState"))
        return; // état inconnu ou corrompu : on garde les réglages courants

    if (const auto* params = xml->getChildByName (apvts.state.getType().toString()))
        apvts.replaceState (juce::ValueTree::fromXml (*params));

    if (const auto* ui = xml->getChildByName ("UI"))
        uiState = ui->getStringAttribute ("json");
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OpenFilterProcessor();
}
