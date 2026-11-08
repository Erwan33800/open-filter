#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    open::ParamBridgeEditor::Config makeConfig (OpenFilterProcessor& p)
    {
        open::ParamBridgeEditor::Config config;
        config.eventId        = "openfilter"; // identique à BRIDGE_EVENT dans ui/src/bridge.js
        config.dataFolderName = "OpenFilter";
        config.pageHtml       = BinaryData::openfilter_ui_html;
        config.pageSize       = (size_t) BinaryData::openfilter_ui_htmlSize;
        config.getUiState     = [&p] { return p.getUiState(); };
        config.setUiState     = [&p] (const juce::String& json) { p.setUiState (json); };
        return config;
    }
}

OpenFilterEditor::OpenFilterEditor (OpenFilterProcessor& p)
    : open::ParamBridgeEditor (p, p.apvts, makeConfig (p))
{
}
