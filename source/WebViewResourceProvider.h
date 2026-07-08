#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

class PluginProcessor;
class PluginEditor;

namespace WebViewResources
{
#if JUCE_WEB_BROWSER
std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

juce::WebBrowserComponent::Options makeBrowserOptions (PluginProcessor& processor,
                                                        PluginEditor& editor);
#endif
}
