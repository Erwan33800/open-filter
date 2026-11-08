#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    const juce::Colour backgroundColour { 0xff151516 }; // --bg de l'interface

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
                   .withResourceProvider ([this] (const juce::String& url) { return getResource (url); }))
{
    addAndMakeVisible (browser);
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    // Mêmes limites que le minimum CSS de la page (980 x 620).
    setResizable (true, true);
    setResizeLimits (980, 620, 1800, 1000);
    setSize (1120, 740);
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
