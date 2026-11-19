#pragma once

#include "PluginProcessor.h"

#include "ui/KshNativeEditor.h"
#include "ui/KshSwingImport.h"

#if JUCE_IOS
#include "platform/KshIOSFileDrop.h"
#endif

//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor, private juce::Timer, public juce::FileDragAndDropTarget
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
   #if JUCE_IOS
    std::shared_ptr<ksh::IOSProjectLibrary> iosLibrary;
    std::unique_ptr<ksh::IOSFileDrop> iosFileDrop;
    uint64_t iosOperationEpoch = 0;
    void iosProjectAction (int action, std::function<void (juce::var)>, juce::File target = {},
                           juce::String name = {}, juce::String description = {});
   #endif
    PluginProcessor& processorRef;
    const bool fitViewport;
    bool zenMode = false, zenFullscreenSeen = false, zenExitRequested = false;
    juce::Component::SafePointer<juce::DocumentWindow> zenWindow;
    juce::Rectangle<int> windowBoundsBeforeZen;
    int titleBarHeightBeforeZen = 0;
    bool nativeTitleBarBeforeZen = false;
    void timerCallback() override;
    void finishZenExit();

    void confirmDiscardingChanges (std::function<void()> proceed);
    juce::File getDefaultProjectsDirectory() const;
    juce::Array<juce::File> getSiblingProjectFiles() const;
    bool loadProjectData (const juce::File&, const juce::MemoryBlock&, juce::String& errorMessage);
    bool loadProjectFile (const juce::File& file, juce::String& errorMessage);
    bool saveProjectFile (const juce::File& file, juce::String& errorMessage);
    juce::var projectOperationResult (bool success,
        const juce::String& errorMessage = {}) const;

    std::unique_ptr<ksh::ui::NativeEditor> nativeView;
    void updateNativeSize();
    std::unique_ptr<juce::FileChooser> projectFileChooser;

    struct SwingDrop
    {
        uint64_t request, document;
        double beatsPerStep, bpm;
        int swing, target;
    };
    SwingDrop beginSwingDrop();
    void analyseSwingDrop (SwingDrop, const juce::File&, std::shared_ptr<juce::TemporaryFile> stagedFile = {});
    void completeSwingDrop (SwingDrop, const ksh::ui::SwingImportResult&);
    void showSwingDropStatus (const juce::String&, bool pending = false);
    uint64_t swingDropRequest = 0;
    bool swingDragOver = false;
    juce::String swingDropStatus;
    juce::TimedCallback swingDropStatusTimer { [this] { showSwingDropStatus ({}); } };
    std::unique_ptr<juce::ThreadPool> swingDropWorker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
