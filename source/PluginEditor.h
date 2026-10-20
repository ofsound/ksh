#pragma once

#include "PluginProcessor.h"

#include "ui/KshNativeEditor.h"

//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor&, bool fitStandaloneToViewport =
                         #if JUCE_IOS
                             true
                         #else
                             false
                         #endif
    );
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showSaveProjectDialog (const juce::Array<juce::var>& args,
        std::function<void (juce::var)> complete);
    void saveProject (std::function<void (juce::var)> complete);
    void showLoadProjectDialog (std::function<void (juce::var)> complete);
    void createNewProject (std::function<void (juce::var)> complete);
    void cycleProject (int direction,
        std::function<void (juce::var)> complete);
    bool hasPreviousProject() const;
    bool hasNextProject() const;
    juce::String getCurrentProjectFileName() const;

private:
    friend class PluginEditorTestPeer;
    PluginProcessor& processorRef;
    const bool fitViewport;

    void confirmDiscardingChanges (std::function<void()> proceed);
    juce::File getDefaultProjectsDirectory() const;
    juce::Array<juce::File> getSiblingProjectFiles() const;
    bool loadProjectFile (const juce::File& file, juce::String& errorMessage);
    bool saveProjectFile (const juce::File& file, juce::String& errorMessage);
    juce::var projectOperationResult (bool success,
        const juce::String& errorMessage = {}) const;

    std::unique_ptr<ksh::ui::NativeEditor> nativeView;
    void updateNativeSize();
    std::unique_ptr<juce::FileChooser> projectFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
