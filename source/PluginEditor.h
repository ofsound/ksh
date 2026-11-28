#pragma once

#include "PluginProcessor.h"

#include "ui/KshNativeEditor.h"
#include "ui/KshSwingImport.h"

#if JUCE_IOS
#include "platform/KshIOSFileDrop.h"
#endif

//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener, public juce::FileDragAndDropTarget
{
   #if JUCE_IOS
    static constexpr bool standaloneUsesFixedViewport = true;
   #else
    static constexpr bool standaloneUsesFixedViewport = false;
   #endif

public:
    explicit PluginEditor (PluginProcessor&, bool fitStandaloneToViewport = standaloneUsesFixedViewport,
                           std::shared_ptr<juce::PropertiesFile> preferences = {});
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void setZenMode (bool);
    bool isZenMode() const { return zenMode; }
    bool keyPressed (const juce::KeyPress&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    void showSaveProjectDialog (const juce::Array<juce::var>& args,
        std::function<void (juce::var)> complete);
    void saveCurrentProject (bool saveAs = false);
    void saveProject (std::function<void (juce::var)> complete);
    void showLoadProjectDialog (std::function<void (juce::var)> complete);
    void createNewProject (std::function<void (juce::var)> complete);
    void cycleProject (int direction,
        std::function<void (juce::var)> complete);
    juce::String getCurrentProjectFileName() const;

private:
    friend class PluginEditorTestPeer;
   #if JUCE_IOS
    std::shared_ptr<ksh::IOSProjectLibrary> iosLibrary;
    std::unique_ptr<ksh::IOSFileDrop> iosFileDrop;
    juce::Rectangle<float> lastSafeArea;
    const bool isIPhone = juce::SystemStats::getDeviceDescription().startsWithIgnoreCase ("iPhone");
    uint64_t iosOperationEpoch = 0;
    void iosProjectAction (int action, std::function<void (juce::var)>, juce::File target = {});
   #endif
    PluginProcessor& processorRef;
    const bool fitViewport;
    bool updatingNativeSize = false;
    bool windowTallMode = false;
    bool zenMode = false, zenFullscreenSeen = false, zenExitRequested = false;
    juce::Component::SafePointer<juce::DocumentWindow> zenWindow;
    juce::Rectangle<int> windowBoundsBeforeZen;
    int titleBarHeightBeforeZen = 0;
    bool nativeTitleBarBeforeZen = false;
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void finishZenExit();

    void confirmDiscardingChanges (std::function<void()> proceed);
    void restorePreviousProject (std::function<void (juce::var)> complete);
    juce::File getDefaultProjectsDirectory() const;
    juce::File getLocalProjectsDirectory() const;
    ksh::ui::ProjectBrowser::StorageLocations projectStorageLocations();
    juce::Array<juce::File> getProjectFiles (const juce::File&) const;
    juce::Array<juce::File> getSiblingProjectFiles (const juce::File& currentProjectFile) const;
    void showExternalProjectDialog (std::function<void (juce::var)>);
    void saveProjectTo (const juce::File&, const ksh::ui::ProjectBrowser::Details&, bool createOnly,
                        std::function<void (juce::var)>);
    bool loadProjectData (const juce::File&, const juce::MemoryBlock&, juce::String& errorMessage);
    bool loadProjectFile (const juce::File& file, juce::String& errorMessage);
    bool writeProjectFile (const juce::File&, const juce::MemoryBlock&, bool createOnly, juce::String& errorMessage);
    juce::var projectOperationResult (bool success,
        const juce::String& errorMessage = {}) const;

    std::unique_ptr<ksh::ui::NativeEditor> nativeView;
    bool usesHostedTabletViewport() const;
    void updateViewport (juce::Rectangle<float>);
    void layoutNativeView (juce::Rectangle<float>, bool onPhone);
    void updateNativeSize();
    std::unique_ptr<juce::FileChooser> projectFileChooser;
    juce::File projectBrowseFolder;

    struct FileDrop
    {
        uint64_t request, document;
        double beatsPerStep, bpm;
        int swing, target;
    };
    struct ProjectDrop
    {
        std::string data;
        juce::String error, summary;
        std::optional<double> bpm;
    };
    FileDrop beginFileDrop();
    void openDroppedProject (const juce::File&);
    void analyseFileDrop (FileDrop, const juce::File&, const juce::String& originalName,
                          std::shared_ptr<juce::TemporaryFile> stagedFile = {});
    void completeProjectDrop (FileDrop, ProjectDrop);
    void completeSwingDrop (FileDrop, const ksh::ui::SwingImportResult&);
    void showFileDropStatus (const juce::String&, bool pending = false);
    uint64_t fileDropRequest = 0;
    bool fileDragOver = false;
    juce::String fileDropStatus;
    juce::TimedCallback fileDropStatusTimer { [this] { showFileDropStatus ({}); } };
    std::shared_ptr<std::atomic<bool>> fileDropCancellation;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
