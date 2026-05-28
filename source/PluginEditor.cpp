#include "PluginEditor.h"

#if JUCE_WEB_BROWSER

KshWebBrowserComponent::KshWebBrowserComponent (const Options& options,
                                                std::function<void()> onPageLoadedIn)
    : juce::WebBrowserComponent (options),
      onPageLoaded (std::move (onPageLoadedIn))
{
}

void KshWebBrowserComponent::pageFinishedLoading (const juce::String& url)
{
    juce::WebBrowserComponent::pageFinishedLoading (url);

    if (onPageLoaded != nullptr)
        onPageLoaded();
}

#endif

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p)
{
#if JUCE_WEB_BROWSER
    processorRef.setEditorResizeCallback ([this] (int width, int height)
    {
        setSize (width, height);
    });

    auto onPageLoaded = [this]
    {
        processorRef.getUiBridge().syncAll();
    };

    webView = std::make_unique<KshWebBrowserComponent> (WebViewResources::makeBrowserOptions (processorRef),
                                                        onPageLoaded);
    addAndMakeVisible (*webView);
    processorRef.getUiBridge().attachWebView (webView.get());

   #if JUCE_DEBUG
    webView->goToURL ("http://localhost:5173");
   #else
    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
   #endif
#else
    fallbackLabel.setText ("WebView is not available on this platform.\nEnable JUCE_WEB_BROWSER.",
                           juce::dontSendNotification);
    fallbackLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (fallbackLabel);
#endif

    setResizeLimits (736, 176, 2400, 1200);
    setResizable (true, true);
    setSize (736, 176);
    startTimerHz (30);
}

PluginEditor::~PluginEditor()
{
#if JUCE_WEB_BROWSER
    processorRef.getUiBridge().detachWebView();
#endif
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
}

void PluginEditor::resized()
{
#if JUCE_WEB_BROWSER
    if (webView != nullptr)
        webView->setBounds (getLocalBounds());
#else
    fallbackLabel.setBounds (getLocalBounds());
#endif
}

void PluginEditor::timerCallback()
{
    processorRef.getUiBridge().pollTransportUi();
}
