#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

class PluginProcessor;

namespace WebViewResources
{
#if JUCE_WEB_BROWSER
std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

juce::WebBrowserComponent::Options makeBrowserOptions (PluginProcessor& processor);
#endif
}
