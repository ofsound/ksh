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

#if JUCE_WEB_BROWSER
    void showSaveProjectDialog (const juce::Array<juce::var>& args,
                                juce::WebBrowserComponent::NativeFunctionCompletion complete);
    void showLoadProjectDialog (juce::WebBrowserComponent::NativeFunctionCompletion complete);
    void createNewProject (juce::WebBrowserComponent::NativeFunctionCompletion complete);
    void cycleProject (int direction,
                       juce::WebBrowserComponent::NativeFunctionCompletion complete);
    juce::var handleEditorScaleMinimumRequest (int minWidth, int minHeight);
    bool hasPreviousProject() const;
    bool hasNextProject() const;
    juce::String getCurrentProjectFileName() const;
#endif

private:
    void timerCallback() override;

    PluginProcessor& processorRef;

#if JUCE_WEB_BROWSER
    void applyNormalResizeLimits();
    juce::File getDefaultProjectsDirectory() const;
    juce::Array<juce::File> getSiblingProjectFiles() const;
    bool loadProjectFile (const juce::File& file, juce::String& errorMessage);
    bool saveProjectFile (const juce::File& file, juce::String& errorMessage);
    juce::var projectOperationResult (bool success,
                                      const juce::String& errorMessage = {}) const;

    std::unique_ptr<KshWebBrowserComponent> webView;
    std::unique_ptr<juce::FileChooser> projectFileChooser;
    juce::File currentProjectFile;
    int scaleMinimumWidth = 1328;
    int scaleMinimumHeight = 828;
#else
    juce::Label fallbackLabel;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
