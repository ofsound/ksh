#pragma once

#include "PluginProcessor.h"

#if JUCE_WEB_BROWSER
 #include "WebViewResourceProvider.h"

class KshWebBrowserComponent : public juce::WebBrowserComponent
{
public:
    KshWebBrowserComponent (const Options& options, std::function<void()> onPageLoadedIn);

    void pageFinishedLoading (const juce::String& url) override;

private:
    std::function<void()> onPageLoaded;
};
#endif

//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    PluginProcessor& processorRef;

#if JUCE_WEB_BROWSER
    std::unique_ptr<KshWebBrowserComponent> webView;
#else
    juce::Label fallbackLabel;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
