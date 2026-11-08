#include "PluginEditor.h"
#include "BinaryData.h"
#include "params/ParamSpec.h"

namespace
{
    const juce::Colour backgroundColour { 0xff151516 }; // --bg de l'interface
    const juce::Identifier eventId { "openfilter" };

    juce::WebBrowserComponent::Options makeOptions()
    {
        return juce::WebBrowserComponent::Options {}
            .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
                                         .withUserDataFolder (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                                                  .getChildFile ("OpenFilter")
                                                                  .getChildFile ("WebView"))
                                         .withBackgroundColour (backgroundColour))
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden();
    }
}

OpenFilterEditor::OpenFilterEditor (OpenFilterProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      browser (makeOptions()
                   .withEventListener (eventId, [this] (const juce::var& message) { handleMessage (message); })
                   .withResourceProvider ([this] (const juce::String& url) { return getResource (url); }))
{
    const auto& specs = openfilter::params::allParams();
    paramIds.reserve (specs.size());
    paramValues.reserve (specs.size());
    lastSent.assign (specs.size(), std::nanf (""));

    for (size_t i = 0; i < specs.size(); ++i)
    {
        paramIds.push_back (juce::String (specs[i].id));
        paramValues.push_back (processor.apvts.getRawParameterValue (specs[i].id.c_str()));
        paramIndex[specs[i].id] = i;
    }

    addAndMakeVisible (browser);
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    // Mêmes limites que le minimum CSS de la page (980 x 620).
    setResizable (true, true);
    setResizeLimits (980, 620, 1800, 1000);
    setSize (1120, 740);

    startTimerHz (30);
}

OpenFilterEditor::~OpenFilterEditor()
{
    stopTimer();

    // Si la fenêtre se ferme en plein geste, on referme les gestes ouverts : Live ne doit pas en garder d'ouverts.
    for (const auto& id : activeGestures)
        if (auto* param = processor.apvts.getParameter (id))
            param->endChangeGesture();
}

void OpenFilterEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);
}

void OpenFilterEditor::resized()
{
    browser.setBounds (getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> OpenFilterEditor::getResource (const juce::String& url)
{
    const auto path = url.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);

    if (path.isEmpty() || path == "index.html")
    {
        const auto* data = reinterpret_cast<const std::byte*> (BinaryData::openfilter_ui_html);
        return juce::WebBrowserComponent::Resource { std::vector<std::byte> (data, data + BinaryData::openfilter_ui_htmlSize),
                                                     "text/html" };
    }

    return std::nullopt;
}

void OpenFilterEditor::send (const juce::Array<juce::var>& message)
{
    browser.emitEventIfBrowserIsVisible (eventId, juce::var (message));
}

// État complet envoyé à la page quand elle est prête : toutes les valeurs de paramètres, puis les réglages d'interface.
void OpenFilterEditor::sendSnapshot()
{
    auto* values = new juce::DynamicObject();

    for (size_t i = 0; i < paramIds.size(); ++i)
    {
        const float v = paramValues[i]->load();
        values->setProperty (juce::Identifier (paramIds[i]), (double) v);
        lastSent[i] = v;
    }

    juce::Array<juce::var> message;
    message.add ("snapshot");
    message.add (juce::var (values));
    message.add (processor.getUiState());
    send (message);
}

void OpenFilterEditor::timerCallback()
{
    if (! pageReady)
        return;

    juce::Array<juce::var> changes;

    for (size_t i = 0; i < paramIds.size(); ++i)
    {
        const float v = paramValues[i]->load();

        if (v == lastSent[i])
            continue;

        lastSent[i] = v;

        juce::Array<juce::var> pair;
        pair.add (paramIds[i]);
        pair.add ((double) v);
        changes.add (juce::var (pair));
    }

    if (changes.isEmpty())
        return;

    juce::Array<juce::var> message;
    message.add ("params");
    message.add (juce::var (changes));
    send (message);
}

void OpenFilterEditor::handleMessage (const juce::var& message)
{
    const auto* args = message.getArray();

    if (args == nullptr || args->isEmpty())
        return;

    const auto command = (*args)[0].toString();

    if (command == "ready")
    {
        pageReady = true;
        sendSnapshot();
        return;
    }

    if (command == "ui")
    {
        if (args->size() > 1)
            processor.setUiState ((*args)[1].toString());
        return;
    }

    if ((command != "begin" && command != "end" && command != "set") || args->size() < 2)
        return;

    const auto id = (*args)[1].toString();
    auto* param = processor.apvts.getParameter (id);

    if (param == nullptr)
        return; // identifiant inconnu : ignoré

    if (command == "begin")
    {
        if (activeGestures.insert (id).second)
            param->beginChangeGesture();
    }
    else if (command == "end")
    {
        if (activeGestures.erase (id) > 0)
            param->endChangeGesture();
    }
    else if (args->size() > 2)
    {
        const float value = (float) (double) (*args)[2];
        param->setValueNotifyingHost (param->convertTo0to1 (value));

        // La page connaît déjà cette valeur : pas d'écho dans le prochain passage du minuteur.
        const auto it = paramIndex.find (id.toStdString());

        if (it != paramIndex.end())
            lastSent[it->second] = paramValues[it->second]->load();
    }
}
