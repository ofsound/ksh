#include <PluginProcessor.h>
#include <PluginEditor.h>
#include "../standalone/StandalonePreferences.h"

#include <engine/KshMidiPlayback.h>
#include <engine/KshPersistence.h>

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <exception>
#include <thread>
#include <vector>

using namespace ksh;

class PluginEditorTestPeer
{
public:
    static bool load (PluginEditor& editor, const juce::File& file, juce::String& error) { return editor.loadProjectFile (file, error); }
    static bool loadData (PluginEditor& editor, const juce::File& file, const juce::MemoryBlock& data, juce::String& error) { return editor.loadProjectData (file, data, error); }
    static void layoutMobileViewport (PluginEditor& editor, juce::Rectangle<float> available, bool phone) { editor.layoutNativeView (available, phone); }
    static ksh::ui::NativeEditor& view (PluginEditor& editor) { return *editor.nativeView; }
};

namespace
{
juce::MemoryBlock getPluginState (PluginProcessor& plugin)
{
    juce::MemoryBlock state;
    plugin.getStateInformation (state);
    return state;
}

void setPluginState (PluginProcessor& plugin, const juce::MemoryBlock& state)
{
    plugin.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
}

void setPluginStateText (PluginProcessor& plugin, std::string_view text)
{
    plugin.setStateInformation (text.data(), static_cast<int> (text.size()));
}

int countNoteOns (const juce::MidiBuffer& midi)
{
    int count = 0;

    for (const auto metadata : midi)
    {
        if (metadata.getMessage().isNoteOn())
            ++count;
    }

    return count;
}

bool containsNoteOn (const juce::MidiBuffer& midi, int pitch)
{
    for (const auto metadata : midi)
    {
        const auto& message = metadata.getMessage();

        if (message.isNoteOn() && message.getNoteNumber() == pitch)
            return true;
    }

    return false;
}

void configureSnarePattern (PluginProcessor& plugin)
{
    REQUIRE (plugin.dispatchUiEngineCommand ("steps", { 16 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("channels", { 2 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("channel_note", { 2, 38 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 2, 5, 1, 90, 100, 1 }));
}
} // namespace

TEST_CASE ("parsePersistencePayload accepts v1 JSON", "[plugin][persistence]")
{
    const std::string json = R"({"v":1,"stepCount":8,"channelCount":2})";
    const auto parsed = parsePersistencePayload (json);

    REQUIRE (parsed.has_value());
    REQUIRE ((*parsed)["v"] == 1);
    REQUIRE ((*parsed)["stepCount"] == 8);
}

TEST_CASE ("parsePersistencePayload rejects M4L pattern wrapper", "[plugin][persistence]")
{
    const std::string wrapped =
        R"({"ksh_pattern_data":[{"v":1,"stepCount":8,"channelCount":2,"refreshSteps":4,"generationMode":"static","staticSource":0,"rate":"16n","tempo":120,"swing":0,"velocityHumanize":0,"timingHumanize":0,"deviceActive":1,"channels":[["Kick",36,-1,16,"normal"],["Snare",38,-1,16,"normal"]],"sourceChannelMutes":[[0,0],[0,0],[0,0],[0,0]],"cells":[]}]})";

    const auto parsed = parsePersistencePayload (wrapped);

    REQUIRE_FALSE (parsed.has_value());
}

TEST_CASE ("parsePersistencePayload rejects chunked M4L atoms", "[plugin][persistence]")
{
    const std::string payload =
        R"({"v":1,"stepCount":8,"channelCount":2,"refreshSteps":4,"generationMode":"static","staticSource":0,"rate":"16n","tempo":120,"swing":0,"velocityHumanize":0,"timingHumanize":0,"deviceActive":1,"channels":[["Kick",36,-1,16,"normal"]],"sourceChannelMutes":[[0],[0],[0],[0]],"cells":[]})";
    const nlohmann::json atoms = nlohmann::json::array ({
        "ksh_json_chunks_v1",
        juce::URL::addEscapeChars (payload, true).toStdString()
    });
    const auto parsed = parsePersistencePayload (atoms.dump());

    REQUIRE_FALSE (parsed.has_value());
}

TEST_CASE ("plugin state roundtrips through getStateInformation", "[plugin][persistence]")
{
    PluginProcessor original;
    configureSnarePattern (original);

    const auto saved = getPluginState (original);

    PluginProcessor restored;
    setPluginState (restored, saved);

    const auto engine = restored.engineStateSnapshot();
    REQUIRE (engine.stepCount == 16);
    REQUIRE (engine.channelCount == Constants::maxChannels);
    REQUIRE (engine.channels[1].note == 38);
    REQUIRE (engine.sources[0][1][4].enabled);
    REQUIRE (engine.sources[0][1][4].velocity == 90);
    CHECK (engine.channels[15].note == Constants::defaultChannelNotes[15]);
    CHECK_FALSE (engine.sources[0][15][4].enabled);
}

TEST_CASE ("Replacing a clean DAW project leaves all patterns recoverable", "[plugin][persistence][project-recovery]")
{
    auto originalStorage = std::make_unique<PluginProcessor>();
    auto& original = *originalStorage;
    original.setProjectMetadata ("DAW project", "All patterns", {}, {});
    for (int pattern = 1; pattern <= Constants::sourceCount; ++pattern)
        REQUIRE (original.dispatchUiEngineCommand ("cell", { pattern, 1, pattern, 1, 60 + pattern }));

    const juce::TemporaryFile temporaryFolder ("-projects");
    const auto folder = temporaryFolder.getFile();
    REQUIRE (folder.createDirectory().wasOk());
    const auto firstFile = folder.getChildFile ("01.kshproject");
    const auto secondFile = folder.getChildFile ("02.kshproject");
    REQUIRE (firstFile.replaceWithText (original.enginePersistenceState().dump()));
    auto replacementStorage = std::make_unique<PluginProcessor>();
    auto& replacement = *replacementStorage;
    replacement.setProjectMetadata ("Replacement", {}, {}, {});
    REQUIRE (secondFile.replaceWithText (replacement.enginePersistenceState().dump()));

    PluginProcessor processor;
    setPluginState (processor, getPluginState (original));
    REQUIRE_FALSE (processor.isDocumentModified());
    REQUIRE_FALSE (processor.canRestorePreviousProject());
    {
        PluginEditor editor (processor);
        auto& view = PluginEditorTestPeer::view (editor);
        SECTION ("New") { view.projectAction (0); }
        SECTION ("Next project")
        {
            processor.setProjectFile (firstFile);
            view.projectAction (4);
        }
        SECTION ("File drop") { editor.filesDropped ({ secondFile.getFullPathName() }, 100, 100); }
    }
    CHECK (processor.getProjectName() != "DAW project");
    REQUIRE (processor.canRestorePreviousProject());

    // A failed open must leave the last successful replacement recoverable.
    const std::string badProject = R"({"v":1,"channelCount":0,"channels":[]})";
    REQUIRE_FALSE (processor.loadStateFromData (badProject.data(), static_cast<int> (badProject.size())));
    PluginEditor reopened (processor);
    auto& view = PluginEditorTestPeer::view (reopened);
    view.projectAction (5);
    juce::TextButton* restore = nullptr;
    for (auto* child : view.getChildren())
        if (auto* browser = dynamic_cast<ksh::ui::ProjectBrowser*> (child); browser && browser->isVisible())
            for (auto* component : browser->getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*> (component); button && button->getButtonText() == "RESTORE PREVIOUS")
                    restore = button;
    REQUIRE (restore != nullptr);
    REQUIRE (restore->isVisible());
    restore->onClick();
    REQUIRE_FALSE (processor.canRestorePreviousProject());
    CHECK (processor.getProjectName() == "DAW project");
    CHECK (processor.getProjectDescription() == "All patterns");
    CHECK (processor.getProjectFile() == juce::File());
    CHECK (processor.isDocumentModified());
    const auto restored = processor.engineStateSnapshot();
    for (size_t pattern = 0; pattern < Constants::sourceCount; ++pattern)
    {
        CHECK (restored.sources[pattern][0][pattern].enabled);
        CHECK (restored.sources[pattern][0][pattern].velocity == 61 + static_cast<int> (pattern));
    }
    CHECK (folder.deleteRecursively());
}

TEST_CASE ("Unsupported host project versions are saved byte for byte until replaced", "[plugin][persistence]")
{
    auto future = GENERATE (std::string (" \n{\"v\":2, \"future\":true}\n"), std::string (R"({"v":4294967297})"));
    PluginProcessor processor;
    configureSnarePattern (processor);
    const auto supported = getPluginState (processor);
    setPluginStateText (processor, future);
    REQUIRE (processor.hasUnsupportedProjectVersion());
    CHECK (getPluginState (processor) == juce::MemoryBlock (future.data(), future.size()));

    const std::string invalid = "truncated state";
    setPluginStateText (processor, invalid);
    REQUIRE_FALSE (processor.loadStateFromData (future.data(), static_cast<int> (future.size())));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 93 }));
    CHECK (getPluginState (processor) == juce::MemoryBlock (future.data(), future.size()));

    SECTION ("The user chooses New") { processor.resetProject(); }
    SECTION ("The user opens a supported file")
    {
        REQUIRE (processor.loadStateFromData (supported.getData(), static_cast<int> (supported.getSize())));
    }
    SECTION ("The host restores a supported project") { setPluginState (processor, supported); }
    CHECK_FALSE (processor.hasUnsupportedProjectVersion());
    const auto saved = getPluginState (processor);
    CHECK (parsePersistencePayload ({ static_cast<const char*> (saved.getData()), saved.getSize() }).has_value());
}

TEST_CASE ("restored plugin emits MIDI from saved pattern", "[plugin][persistence]")
{
    PluginProcessor original;
    configureSnarePattern (original);

    const auto saved = getPluginState (original);

    PluginProcessor restored;
    setPluginState (restored, saved);

    MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto snapshot = restored.enginePlaybackSnapshot();
    const auto blockStartBeat = 4.0 * snapshot.beatsPerStep;
    juce::MidiBuffer midi;
    [[maybe_unused]] const auto result = runner.processBlock (snapshot, blockStartBeat, 120.0, true, 512, midi);

    REQUIRE (countNoteOns (midi) == 1);
    REQUIRE (containsNoteOn (midi, 38));
}

TEST_CASE ("plugin state restore works off the message thread", "[plugin][persistence]")
{
    PluginProcessor original;
    configureSnarePattern (original);

    const auto saved = getPluginState (original);

    PluginProcessor restored;
    juce::MemoryBlock restoredState;
    std::exception_ptr error;

    std::thread worker ([&]
    {
        try
        {
            setPluginState (restored, saved);
            restored.getStateInformation (restoredState);
        }
        catch (...)
        {
            error = std::current_exception();
        }
    });

    worker.join();

    if (error != nullptr)
        std::rethrow_exception (error);

    REQUIRE (restoredState.getSize() > 0);
    const auto engine = restored.engineStateSnapshot();
    REQUIRE (engine.channelCount == Constants::maxChannels);
    REQUIRE (engine.sources[0][1][4].enabled);
}

TEST_CASE ("plugin state save flushes pending host macro parameter changes", "[plugin][persistence]")
{
    PluginProcessor plugin;
    auto* swing = plugin.getValueTreeState().getParameter ("swing");

    REQUIRE (swing != nullptr);

    swing->setValueNotifyingHost (swing->convertTo0to1 (64.0f));

    const auto saved = getPluginState (plugin);
    const auto parsed = nlohmann::json::parse (
        std::string_view (static_cast<const char*> (saved.getData()), saved.getSize()));

    REQUIRE (parsed["swing"] == 64);
}

TEST_CASE ("Pattern and row mute overrides roundtrip only in DAW state", "[plugin][persistence]")
{
    PluginProcessor plugin;
    REQUIRE (plugin.dispatchUiEngineCommand ("static_source", { 4 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("source_channel_mute", { 4, 2, 1 }));
    plugin.markDocumentClean();
    const auto project = plugin.enginePersistenceState();

    auto* pattern = plugin.getValueTreeState().getParameter ("pattern");
    auto* mute = plugin.getValueTreeState().getParameter ("row_8_mute");
    REQUIRE (pattern != nullptr);
    REQUIRE (mute != nullptr);
    pattern->setValueNotifyingHost (pattern->convertTo0to1 (16.0f));
    mute->setValueNotifyingHost (1.0f);
    CHECK (plugin.enginePersistenceState() == project);
    CHECK_FALSE (plugin.isDocumentModified());
    const auto saved = getPluginState (plugin);

    PluginProcessor restored;
    setPluginState (restored, saved);
    auto* restoredPattern = restored.getValueTreeState().getParameter ("pattern");
    auto* restoredMute = restored.getValueTreeState().getParameter ("row_8_mute");
    REQUIRE (restoredPattern != nullptr);
    REQUIRE (restoredMute != nullptr);
    CHECK (restoredPattern->getText (restoredPattern->getValue(), 16) == "16");
    CHECK (restoredMute->getValue() == 1.0f);
    CHECK (restored.engineStateSnapshot().staticSource == 3);
    CHECK (restored.engineStateSnapshot().sourceChannelMutes[3][1]);

    const auto projectData = project.dump();
    const auto size = static_cast<int> (projectData.size());
    REQUIRE (restored.loadStateFromData (projectData.data(), size));
    CHECK (restoredMute->getValue() == 1.0f);
    restored.setStateInformation (projectData.data(), size);
    CHECK (restoredPattern->getText (restoredPattern->getValue(), 16) == "Manual");
    CHECK (restoredMute->getValue() == 0.0f);
}

TEST_CASE ("project metadata and editor state roundtrip through plugin state", "[plugin][persistence]")
{
    PluginProcessor original;
    configureSnarePattern (original);
    original.setProjectMetadata ("Pseudoflute",
                                 "External file test",
                                 "2026-07-08T10:00:00Z",
                                 "2026-07-08T11:00:00Z", "ofsound");

    const auto saved = getPluginState (original);
    const auto parsed = nlohmann::json::parse (
        std::string_view (static_cast<const char*> (saved.getData()), saved.getSize()));

    REQUIRE (parsed["projectName"] == "Pseudoflute");
    REQUIRE (parsed["projectDescription"] == "External file test");
    REQUIRE (parsed["projectAuthor"] == "ofsound");
    CHECK_FALSE (parsed.contains ("projectThemeMode"));
    CHECK_FALSE (parsed.contains ("patternViewScale"));
    CHECK_FALSE (parsed.contains ("standaloneTempo"));

    PluginProcessor restored;
    setPluginState (restored, saved);

    REQUIRE (restored.getProjectName() == "Pseudoflute");
    REQUIRE (restored.getProjectDescription() == "External file test");
    REQUIRE (restored.getProjectAuthor() == "ofsound");
    REQUIRE (restored.getProjectCreatedAt() == "2026-07-08T10:00:00Z");
    REQUIRE (restored.getProjectModifiedAt() == "2026-07-08T11:00:00Z");
    REQUIRE (restored.engineStateSnapshot().sources[0][1][4].enabled);
}

TEST_CASE ("Save updates the current project file and keeps author and created date", "[plugin][persistence]")
{
    PluginProcessor processor;
    processor.setProjectMetadata ("Night drive", "Notes", "2026-07-08T10:00:00Z", "2026-07-09T11:00:00Z", "ofsound");
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("ksh-save-current", ".kshproject");
    const auto data = getPluginState (processor);
    REQUIRE (file.replaceWithData (data.getData(), data.getSize()));
    auto editor = std::make_unique<PluginEditor> (processor);
    juce::String error;
    REQUIRE (PluginEditorTestPeer::load (*editor, file, error));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 77 }));
    bool completed = false;
    editor->saveProject ([&] (juce::var result)
    {
        completed = true;
        CHECK (static_cast<int> (result["success"]) == 1);
        CHECK (result["error"].toString().isEmpty());
    });
    CHECK (completed);
    editor.reset();
    PluginEditor reopened (processor);
    CHECK (reopened.getCurrentProjectFileName() == file.getFileName());
    const auto saved = nlohmann::json::parse (file.loadFileAsString().toStdString());
    CHECK (saved["projectAuthor"] == "ofsound");
    CHECK (saved["projectCreatedAt"] == "2026-07-08T10:00:00Z");
    CHECK (saved["projectModifiedAt"] != "2026-07-09T11:00:00Z");
    CHECK_FALSE (processor.isDocumentModified());
    PluginProcessor restored;
    setPluginStateText (restored, saved.dump());
    CHECK (restored.engineStateSnapshot().sources[0][0][0].velocity == 77);
    CHECK (restored.getProjectFile() == juce::File());
    processor.resetProject();
    CHECK (processor.getProjectFile() == juce::File());
    CHECK (file.deleteFile());
}

TEST_CASE ("Standalone keeps the save destination across restarts without adding it to project state", "[plugin][persistence]")
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("ksh-save-restart", ".kshproject");
    auto data = getPluginState (processor);
    REQUIRE (file.replaceWithData (data.getData(), data.getSize()));
    {
        PluginEditor editor (processor);
        juce::String error;
        REQUIRE (PluginEditorTestPeer::load (editor, file, error));
    }
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 63 }));
    juce::PropertySet settings;
    ksh::saveStandaloneProjectFile (processor, settings);
    data = getPluginState (processor);
    CHECK_FALSE (nlohmann::json::parse (std::string_view (static_cast<const char*> (data.getData()), data.getSize())).contains ("projectFile"));
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor restarted;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    setPluginState (restarted, data);
    ksh::restoreStandaloneProjectFile (restarted, settings);
    const auto restoredFile = restarted.getProjectFile();
    INFO ("Original: " << file.getFullPathName() << ", restored: " << restoredFile.getFullPathName());
    REQUIRE (restoredFile.existsAsFile());
    // Resolving a macOS bookmark can canonicalize an alias such as /var.
    const auto originalFileId = file.getFileIdentifier();
    REQUIRE (originalFileId != 0);
    REQUIRE (restoredFile.getFileIdentifier() == originalFileId);
    CHECK (restarted.isDocumentModified());
    {
        PluginEditor editor (restarted);
        bool completed = false;
        editor.saveProject ([&] (juce::var result)
        {
            completed = true;
            CHECK (static_cast<int> (result["success"]) == 1);
        });
        REQUIRE (completed);
    }
    CHECK_FALSE (restarted.isDocumentModified());
    const auto saved = nlohmann::json::parse (file.loadFileAsString().toStdString());
    CHECK_FALSE (saved.contains ("standaloneDocumentModified"));
    PluginProcessor loaded;
    setPluginState (loaded, data);
    CHECK_FALSE (loaded.isDocumentModified());
    setPluginStateText (loaded, saved.dump());
    CHECK (loaded.engineStateSnapshot().sources[0][0][0].velocity == 63);
    CHECK (loaded.getProjectFile() == juce::File());
    REQUIRE (file.deleteFile());
    ksh::restoreStandaloneProjectFile (restarted, settings);
    CHECK (restarted.getProjectFile() == juce::File());
    processor.resetProject();
    ksh::saveStandaloneProjectFile (processor, settings);
    CHECK (settings.getValue ("projectFile").isEmpty());
}

TEST_CASE ("Save As writes project details to nested folders from the project browser", "[plugin][persistence][project-browser]")
{
    using namespace ksh::ui;
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("ksh-browser", {});
    const juce::ScopeGuard cleanup { [&] { root.deleteRecursively(); } };
    REQUIRE (root.createDirectory().wasOk());
    const auto original = root.getChildFile ("Original.kshproject");
    PluginProcessor processor;
    REQUIRE (original.replaceWithText (processor.enginePersistenceState().dump()));
    processor.setProjectFile (original);
    const auto previous = original.loadFileAsString();
    PluginEditor editor (processor);
    bool completed = false;
    editor.showSaveProjectDialog ({ "New title", "Original notes" }, [&] (juce::var result)
    {
        completed = static_cast<bool> (result["success"]);
    });
    auto& view = PluginEditorTestPeer::view (editor);
    ProjectBrowser* browser = nullptr;
    for (auto* child : view.getChildren())
        if (auto* projects = dynamic_cast<ProjectBrowser*> (child)) browser = projects;
    REQUIRE (browser != nullptr);
    REQUIRE (browser->isVisible());
    const auto button = [&] (const juce::String& label)
    {
        for (auto* child : browser->getChildren())
            if (auto* control = dynamic_cast<juce::TextButton*> (child); control != nullptr && (control->getButtonText() == label || control->getTitle() == label))
                return control;
        return static_cast<juce::TextButton*> (nullptr);
    };
    const auto field = [&] (const juce::String& title)
    {
        for (auto* child : browser->getChildren())
            if (auto* control = dynamic_cast<juce::TextEditor*> (child); control != nullptr && control->getTitle() == title)
                return control;
        return static_cast<juce::TextEditor*> (nullptr);
    };
    REQUIRE (button ("New Folder") != nullptr);
    REQUIRE (button ("CREATE") != nullptr);
    REQUIRE (field ("Folder name") != nullptr);
    for (const auto* folder : { "Sessions", "Live" })
    {
        button ("New Folder")->onClick();
        REQUIRE (field ("Folder name")->isVisible());
        field ("Folder name")->setText (folder);
        button ("CREATE")->onClick();
    }
    REQUIRE (field ("Project name") != nullptr);
    REQUIRE (field ("Project author") != nullptr);
    REQUIRE (field ("Project description") != nullptr);
    REQUIRE (button ("SAVE") != nullptr);
    field ("Project author")->setText ("ofsound");
    field ("Project description")->setText ("Saved from KSH");
    button ("SAVE")->onClick();
    CHECK (completed);
    CHECK_FALSE (browser->isVisible());
    const auto saved = root.getChildFile ("Sessions/Live/New title.kshproject");
    REQUIRE (saved.existsAsFile());
    CHECK (original.loadFileAsString() == previous);
    CHECK (processor.getProjectFile() == saved);
    CHECK (processor.getProjectName() == "New title");
    CHECK (processor.getProjectAuthor() == "ofsound");
    CHECK (processor.getProjectDescription() == "Saved from KSH");
    const auto payload = nlohmann::json::parse (saved.loadFileAsString().toStdString());
    CHECK (payload["projectName"] == "New title");
    CHECK (payload["projectAuthor"] == "ofsound");
    CHECK (payload["projectDescription"] == "Saved from KSH");

    auto other = payload;
    other["projectDescription"] = "Preview another project";
    REQUIRE (saved.getSiblingFile ("Other.kshproject").replaceWithText (other.dump()));
    view.projectAction (1);
    REQUIRE (browser->isVisible());
    view.controller.selectLatched = true;
    view.sync();
    for (auto* child : view.getChildren())
        if (child->getProperties().contains ("selectMode"))
        {
            const auto* front = view.getComponentAt (child->getBounds().getCentre());
            CHECK ((front == browser || browser->isParentOf (front)));
        }
    juce::ListBox* list = nullptr;
    for (auto* child : browser->getChildren())
    {
        if (auto* projectList = dynamic_cast<juce::ListBox*> (child)) list = projectList;
        if (auto* control = dynamic_cast<juce::TextEditor*> (child); control && control->getTitle().startsWith ("Project"))
            CHECK_FALSE (control->isVisible());
    }
    REQUIRE (list != nullptr);
    CHECK (list->getSelectedRow() == -1);
    list->selectRow (1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    const juce::TextEditor* preview = nullptr;
    for (auto* child : browser->getChildren())
    {
        if (auto* control = dynamic_cast<juce::TextEditor*> (child); control && control->isVisible() && control->isReadOnly())
            preview = control;
    }
    REQUIRE (preview != nullptr);
    CHECK (preview->getText() == "Preview another project");
    CHECK (preview->getX() > list->getRight());
    CHECK (processor.getProjectFile() == saved);
    CHECK (button ("Current Project") == nullptr);
    CHECK (button ("SHOW IN FINDER") == nullptr);
    const auto* browseFiles = button ("Browse Files");
    REQUIRE (browseFiles != nullptr);
    CHECK (browseFiles->isVisible());
    CHECK (browseFiles->getBottom() <= list->getY());
    for (const auto* label : { "CANCEL", "OPEN", "DISCARD AND OPEN" })
        if (const auto* action = button (label); action && action->isVisible())
        {
            CHECK (static_cast<bool> (action->getProperties()["insetProjectArtwork"]));
            CHECK (action->getButtonText() == action->getButtonText().toUpperCase());
            const auto labelFont = font (projectActionLabelSize (action->getHeight()), true);
            CHECK (juce::GlyphArrangement::getStringWidth (labelFont, action->getButtonText())
                   <= footerArtworkBounds (action->getLocalBounds().toFloat()).getWidth());
        }
    const auto captureDir = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (captureDir.isNotEmpty())
    {
        const juce::File directory (captureDir);
        REQUIRE (directory.createDirectory().wasOk());
        auto imageFile = directory.getChildFile ("project-browser.png").createOutputStream();
        REQUIRE (imageFile != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (view.createComponentSnapshot (view.getLocalBounds()), *imageFile));
    }
}

TEST_CASE ("A dropped KSH project opens in place and protects unsaved edits", "[plugin][persistence][file-drop]")
{
    const juce::TemporaryFile temp (".kshproject");
    const auto file = temp.getFile();
    PluginProcessor processor;
    processor.setProjectMetadata ("Dropped project", "Notes from Finder", {}, {});
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 100 }));
    REQUIRE (file.replaceWithText (processor.enginePersistenceState().dump()));
    processor.resetProject();

    PluginEditor editor (processor);
    const juce::StringArray files { file.getFullPathName() };
    CHECK (editor.isInterestedInFileDrag (files));
    CHECK_FALSE (editor.isInterestedInFileDrag ({ files[0], files[0] }));
    editor.filesDropped (files, 100, 100);
    CHECK (processor.getProjectFile() == file);
    CHECK (processor.getProjectName() == "Dropped project");
    CHECK_FALSE (processor.isDocumentModified());

    auto& view = PluginEditorTestPeer::view (editor);
    const ksh::ui::Location savedStep { 0, 2 };
    CHECK (view.controller.cell (savedStep).enabled);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 0 }));
    view.controller.refresh();
    REQUIRE (processor.isDocumentModified());
    editor.filesDropped (files, 100, 100);
    CHECK_FALSE (view.controller.cell (savedStep).enabled);
    REQUIRE (view.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (processor.isDocumentModified());

    editor.filesDropped (files, 100, 100);
    juce::TextButton* discard = nullptr;
    for (auto* child : view.getChildren())
        if (auto* dialog = dynamic_cast<ksh::ui::DiscardChanges*> (child); dialog && dialog->isVisible())
            for (auto* control : dialog->getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*> (control); button && button->getButtonText() == "DISCARD CHANGES")
                    discard = button;
    REQUIRE (discard != nullptr);
    discard->onClick();
    CHECK (view.controller.cell (savedStep).enabled);
    CHECK (processor.getProjectFile() == file);
    CHECK_FALSE (processor.isDocumentModified());
}

TEST_CASE ("Project browser breadcrumbs and history navigate folders and restore selection", "[plugin][persistence][project-browser]")
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("ksh-browser-navigation", {});
    const juce::ScopeGuard cleanup { [&] { root.deleteRecursively(); } };
    const auto sessions = root.getChildFile ("Sessions");
    const auto nested = sessions.getChildFile ("Live");
    REQUIRE (nested.createDirectory().wasOk());
    REQUIRE (sessions.getChildFile ("Studio").createDirectory().wasOk());
    for (const auto* name : { "Take 2.kshproject", "Take 10.kshproject" })
        REQUIRE (nested.getChildFile (name).create().wasOk());
    const auto deep = nested.getChildFile ("A long folder name/Another long folder name/Even deeper/Final folder");
    REQUIRE (deep.createDirectory().wasOk());

    juce::PropertiesFile preferences (root.getChildFile ("settings.xml"), {});
    PluginProcessor processor;
    ksh::ui::ProjectBrowser browser (preferences, processor);
    browser.setSize (814, 409);
    juce::File browsed;
    auto enumerate = [&] (const juce::File& folder)
    {
        browsed = folder;
        return folder.findChildFiles (juce::File::findFiles, false, "*.kshproject");
    };
    browser.open (root, {}, std::nullopt, enumerate, {}, {}, false, {}, nested);
    const auto button = [&] (const juce::String& title) -> juce::Button*
    {
        for (auto* child : browser.getChildren())
            if (auto* control = dynamic_cast<juce::Button*> (child); control != nullptr && control->getTitle() == title)
                return control;
        return nullptr;
    };
    juce::ListBox* list = nullptr;
    for (auto* child : browser.getChildren())
    {
        if (auto* candidate = dynamic_cast<juce::ListBox*> (child)) list = candidate;
    }
    REQUIRE (list != nullptr);
    REQUIRE (button ("Back") != nullptr);
    REQUIRE (button ("Forward") != nullptr);
    CHECK_FALSE (button ("Back")->isEnabled());
    CHECK_FALSE (button ("Forward")->isEnabled());

    list->selectRow (2);
    REQUIRE (button ("Sessions") != nullptr);
    REQUIRE (button ("Sessions")->isVisible());
    auto navigate = button ("Sessions")->onClick;
    navigate();
    CHECK (browsed == sessions);
    CHECK (list->getSelectedRow() == 0);
    CHECK (button ("Back")->isEnabled());
    button ("Back")->onClick();
    CHECK (browsed == nested);
    CHECK (list->getSelectedRow() == 2);
    CHECK (button ("Forward")->isEnabled());
    button ("Forward")->onClick();
    CHECK (browsed == sessions);
    REQUIRE (button ("Projects") != nullptr);
    REQUIRE (button ("Projects")->isVisible());
    CHECK (list->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (browsed == nested);
    CHECK_FALSE (button ("Forward")->isEnabled());

    CHECK (list->getListBoxModel()->getNumRows() == 3);
    CHECK (list->getListBoxModel()->getNameForRow (1) == "Take 2");
    CHECK (list->getListBoxModel()->getNameForRow (2) == "Take 10");
    browser.dismiss();

    browser.open (root, {}, std::nullopt, enumerate, {}, {}, false, {}, deep);
    REQUIRE (button ("Parent folders") != nullptr);
    CHECK (button ("Parent folders")->isVisible());
    REQUIRE (button ("Final folder (current location)") != nullptr);
    CHECK (button ("Final folder (current location)")->isVisible());
}

TEST_CASE ("The project browser remembers favorites and protects unsaved changes", "[plugin][persistence][project-browser]")
{
    using ksh::ui::ProjectBrowser;
    const bool compact = GENERATE (false, true);
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("ksh-favorites", {});
    const juce::ScopeGuard cleanup { [&] { root.deleteRecursively(); } };
    REQUIRE (root.getChildFile ("Sessions").createDirectory().wasOk());
    const auto project = root.getChildFile ("Sessions/Favorite.kshproject");
    REQUIRE (project.create().wasOk());
    const auto enumerate = [] (const juce::File& directory)
    {
        return directory.findChildFiles (juce::File::findFiles, false, "*.kshproject");
    };
    const auto findButton = [] (ProjectBrowser& browser, const juce::String& label) -> juce::Button*
    {
        for (auto* child : browser.getChildren())
            if (auto* button = dynamic_cast<juce::Button*> (child); button != nullptr && button->getButtonText() == label)
                return button;
        return nullptr;
    };
    const auto settings = root.getChildFile ("settings.xml");
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    PluginProcessor processor;
    {
        juce::PropertiesFile preferences (settings, options);
        ProjectBrowser browser (preferences, processor);
        browser.setSize (814, 409);
        browser.open (root, {}, std::nullopt, enumerate, {});
        auto* enter = findButton (browser, "OPEN");
        REQUIRE (enter != nullptr);
        enter->onClick();
        auto* favorite = findButton (browser, "Favorite project");
        REQUIRE (favorite != nullptr);
        REQUIRE (favorite->isVisible());
        REQUIRE (favorite->isEnabled());
        CHECK_FALSE (favorite->getToggleState());
        favorite->onClick();
        CHECK (favorite->getToggleState());
        favorite->onClick();
        CHECK_FALSE (favorite->getToggleState());
        favorite->onClick();
        CHECK (favorite->getToggleState());
    }
    juce::PropertiesFile preferences (settings, options);
    ProjectBrowser browser (preferences, processor);
    browser.setSize (compact ? 814 : 1200, compact ? 409 : 800);
    juce::File opened;
    browser.open (root, {}, std::nullopt, enumerate, [&] (const juce::File& file, const auto&, auto done)
    {
        opened = file;
        done ({});
    }, {}, true, [&] (const juce::File&, auto done)
    {
        done (ProjectBrowser::Details { "Favorite", {}, {}, {}, {} });
    });
    auto* favorites = findButton (browser, "Favorites");
    REQUIRE (favorites != nullptr);
    favorites->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (160);
    juce::TextEditor* description = nullptr;
    juce::ListBox* list = nullptr;
    for (auto* child : browser.getChildren())
    {
        if (child->getTitle() == "Project description" && child->isVisible())
            description = dynamic_cast<juce::TextEditor*> (child);
        if (auto* candidate = dynamic_cast<juce::ListBox*> (child))
            list = candidate;
    }
    REQUIRE (description != nullptr);
    REQUIRE (list != nullptr);
    CHECK (description->getText() == "No description");
    CHECK (list->getBottom() - description->getBottom() >= 42);
    auto* open = findButton (browser, "OPEN");
    REQUIRE (open != nullptr);
    REQUIRE (open->isEnabled());
    open->onClick();
    CHECK (opened == juce::File());
    CHECK (open->getButtonText() == "DISCARD AND OPEN");
    open->onClick();
    CHECK (opened == project);
    CHECK_FALSE (browser.isVisible());
}

TEST_CASE ("The project browser supports Save As and Open through a cloud folder picker", "[plugin][persistence][project-browser]")
{
    using ksh::ui::ProjectBrowser;
    juce::TemporaryFile temporary (".settings");
    juce::PropertiesFile::Options options;
    juce::PropertiesFile preferences (temporary.getFile(), options);
    PluginProcessor processor;
    ProjectBrowser browser (preferences, processor);
    browser.setSize (814, 409);
    const auto root = temporary.getFile().getParentDirectory();
    const auto local = root.getNonexistentChildFile ("KSH Local", {});
    const auto cloud = root.getNonexistentChildFile ("KSH Cloud", {});

    std::function<void (juce::File)> chooseFolder;
    juce::File destination;
    ProjectBrowser::Details saved;
    browser.open (local, {}, ProjectBrowser::Details { "Sequence", "Author", "Notes" }, {},
        [&] (const juce::File& file, const ProjectBrowser::Details& details, auto done)
        {
            destination = file;
            saved = details;
            done ({});
        }, {}, false, {}, {}, { "Local projects", cloud, [&] (auto done) { chooseFolder = std::move (done); } });

    const auto button = [&] (const juce::String& label) -> juce::Button*
    {
        for (auto* child : browser.getChildren())
            if (auto* control = dynamic_cast<juce::TextButton*> (child); control != nullptr && control->getButtonText() == label)
                return control;
        return nullptr;
    };
    auto* cloudButton = button ("iCloud Drive");
    auto* save = button ("SAVE");
    REQUIRE (cloudButton != nullptr);
    REQUIRE (cloudButton->isVisible());
    REQUIRE (save != nullptr);
    cloudButton->onClick();
    REQUIRE (chooseFolder);
    CHECK_FALSE (save->isEnabled());
    chooseFolder ({});
    CHECK (save->isEnabled());
    CHECK (destination == juce::File());

    cloudButton->onClick();
    chooseFolder (cloud);
    REQUIRE (save->isEnabled());
    save->onClick();
    CHECK (destination == cloud.getChildFile ("Sequence.kshproject"));
    CHECK (saved.name == "Sequence");
    CHECK (saved.author == "Author");
    CHECK (saved.description == "Notes");
    CHECK_FALSE (browser.isVisible());

    const auto cloudProject = cloud.getChildFile ("Sequence.kshproject");
    juce::File opened;
    bool browsedExternal = false;
    browser.open (local, {}, std::nullopt, [&] (const juce::File& folder)
    {
        return folder == cloud ? juce::Array<juce::File> { cloudProject } : juce::Array<juce::File> {};
    }, [&] (const juce::File& file, const auto&, auto done)
    {
        opened = file;
        done ({});
    }, [&] { browsedExternal = true; }, false, {}, {}, { "Local projects", cloud, [&] (auto done) { chooseFolder = std::move (done); } });
    REQUIRE (cloudButton->isVisible());
    const auto* browseFiles = button ("Browse Files");
    const auto* favorites = button ("Favorites");
    REQUIRE (browseFiles != nullptr);
    REQUIRE (favorites != nullptr);
    CHECK (browseFiles->isVisible());
    CHECK (browseFiles->getY() == cloudButton->getY());
    CHECK (browseFiles->getX() == cloudButton->getRight());
    CHECK (favorites->getX() == browseFiles->getRight());
    SECTION ("Browse Files opens the external picker")
    {
        browseFiles->onClick();
        CHECK (browsedExternal);
        CHECK_FALSE (browser.isVisible());
    }
    SECTION ("iCloud Drive opens the in-app project list")
    {
        cloudButton->onClick();
        chooseFolder (cloud);
        auto* open = button ("OPEN");
        REQUIRE (open != nullptr);
        REQUIRE (open->isEnabled());
        open->onClick();
        CHECK (opened == cloudProject);
        CHECK_FALSE (browsedExternal);
    }
}

TEST_CASE ("Project arrows wrap within the current folder", "[plugin][persistence][project-browser]")
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("ksh-cycle-projects", {});
    const juce::ScopeGuard cleanup { [&] { root.deleteRecursively(); } };
    const auto folder = root.getChildFile ("Sessions");
    const auto nested = folder.getChildFile ("Archive");
    REQUIRE (nested.createDirectory().wasOk());
    const auto first = folder.getChildFile ("Take 2.kshproject");
    const auto second = folder.getChildFile ("Take 10.kshproject");
    PluginProcessor processor;
    const auto contents = processor.enginePersistenceState().dump();
    for (const auto& file : { first, second, nested.getChildFile ("Archived.kshproject"), root.getChildFile ("Other.kshproject") })
        REQUIRE (file.replaceWithText (contents));

    PluginEditor editor (processor);
    auto& view = PluginEditorTestPeer::view (editor);
    view.showProjectBrowser (root, {}, std::nullopt, {}, {});
    ksh::ui::ProjectBrowser* browser = nullptr;
    for (auto* child : view.getChildren())
        if (auto* candidate = dynamic_cast<ksh::ui::ProjectBrowser*> (child))
            browser = candidate;
    REQUIRE (browser != nullptr);
    juce::Button* browserOpen = nullptr;
    for (auto* child : browser->getChildren())
        if (auto* button = dynamic_cast<juce::Button*> (child); button != nullptr && button->getButtonText() == "OPEN")
            browserOpen = button;
    REQUIRE (browserOpen != nullptr);
    browserOpen->onClick();
    browser->dismiss();

    const auto clickArrow = [&] (const juce::String& tooltip)
    {
        for (auto* child : view.getChildren())
            if (auto* button = dynamic_cast<juce::Button*> (child); button != nullptr && button->getTooltip() == tooltip)
            {
                button->onClick();
                return;
            }
        FAIL ("Missing project navigation button: " << tooltip);
    };
    const juce::String next ("Next project in this folder"), previous ("Previous project in this folder");
    clickArrow (next);
    REQUIRE (processor.getProjectFile() == first);
    clickArrow (next);
    CHECK (processor.getProjectFile() == second);
    clickArrow (next);
    CHECK (processor.getProjectFile() == first);
    clickArrow (previous);
    CHECK (processor.getProjectFile() == second);
    REQUIRE (second.deleteFile());
    clickArrow (next);
    CHECK (processor.getProjectFile() == first);

    view.projectAction (0);
    REQUIRE (processor.getProjectFile() == juce::File());
    clickArrow (previous);
    CHECK (processor.getProjectFile() == first);
}

TEST_CASE ("project author survives metadata saves and resets for old or new projects", "[plugin][persistence]")
{
    PluginProcessor processor;
    processor.setProjectMetadata ("Title", "Notes", "created", "modified", "  ofsound  ");
    processor.setProjectMetadata ("Renamed", "Notes", "created", "saved");
    CHECK (processor.getProjectAuthor() == "ofsound");
    auto legacy = processor.enginePersistenceState();
    legacy.erase ("projectAuthor");
    setPluginStateText (processor, legacy.dump());
    CHECK (processor.getProjectAuthor().isEmpty());
    processor.setProjectMetadata ("Title", "Notes", "created", "modified", "ofsound");
    processor.resetProject();
    CHECK (processor.getProjectAuthor().isEmpty());
}

TEST_CASE ("projects saved with retired editor keys still load", "[plugin][persistence]")
{
    PluginProcessor source;
    configureSnarePattern (source);
    const auto saved = getPluginState (source);
    auto payload = nlohmann::json::parse (
        std::string_view (static_cast<const char*> (saved.getData()), saved.getSize()));
    payload["projectThemeMode"] = "sepia";
    payload["patternViewScale"] = 1.5;
    payload["standaloneTempo"] = 140.0;

    PluginProcessor restored;
    setPluginStateText (restored, payload.dump());
    CHECK (restored.engineStateSnapshot().sources[0][1][4].enabled);
    CHECK (std::abs (restored.engineStateSnapshot().tempo - source.engineStateSnapshot().tempo) < 0.001);
}

TEST_CASE ("resetProject restores default project-owned state", "[plugin][persistence]")
{
    PluginProcessor plugin;
    configureSnarePattern (plugin);
    plugin.setProjectMetadata ("Old Project", "Old description", "created", "modified");

    plugin.resetProject();

    const auto state = plugin.engineStateSnapshot();
    REQUIRE (plugin.getProjectName() == "Untitled Project");
    REQUIRE (plugin.getProjectDescription().isEmpty());
    REQUIRE (state.channelCount == ksh::Constants::defaultChannelCount);
    REQUIRE_FALSE (state.sources[0][0][0].enabled);
    REQUIRE_FALSE (state.sources[0][1][4].enabled);
}

TEST_CASE ("invalid plugin state is ignored", "[plugin][persistence]")
{
    PluginProcessor plugin;
    configureSnarePattern (plugin);

    setPluginStateText (plugin, "not json");
    REQUIRE (plugin.engineStateSnapshot().sources[0][1][4].enabled);

    setPluginStateText (plugin, R"({"v":1,"stepCount":8,"channelCount":2,"cells":})");
    REQUIRE (plugin.engineStateSnapshot().sources[0][1][4].enabled);
}

TEST_CASE ("structurally invalid cells cannot wipe the current pattern", "[plugin][persistence]")
{
    PluginProcessor plugin;
    configureSnarePattern (plugin);

    const auto saved = getPluginState (plugin);
    auto payload = nlohmann::json::parse (
        std::string_view (static_cast<const char*> (saved.getData()), saved.getSize()));

    payload["projectName"] = "Metadata that survived";
    payload["cells"] = nlohmann::json::object();
    setPluginStateText (plugin, payload.dump());

    const auto state = plugin.engineStateSnapshot();
    REQUIRE (state.sources[0][1][4].enabled);
    REQUIRE (state.sources[0][1][4].velocity == 90);
}

TEST_CASE ("iPhone balances its vertical margins inside the safe area", "[native-ui][iphone]")
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    PluginEditor editor (processor, true);
    editor.setSize (932, 430);
    auto* view = dynamic_cast<ksh::ui::NativeEditor*> (editor.getChildComponent (0));
    REQUIRE (view != nullptr);
    for (const auto available : { juce::Rectangle<float> (59.0f, 0.0f, 814.0f, 430.0f),
                                  juce::Rectangle<float> (59.0f, 0.0f, 814.0f, 409.0f),
                                  juce::Rectangle<float> (59.0f, 24.0f, 814.0f, 406.0f),
                                  juce::Rectangle<float> (101.0f, 0.0f, 730.0f, 430.0f) })
    {
        PluginEditorTestPeer::layoutMobileViewport (editor, available, true);
        REQUIRE (view->layout.compactOnPhone);
        const auto content = view->getLocalBounds().toFloat().transformedBy (view->getTransform());
        CHECK (available.withTrimmedTop (10.0f).expanded (0.01f).contains (content));
        const float bottomMargin = static_cast<float> (editor.getHeight()) - content.getBottom();
        CHECK (std::abs (content.getY() - bottomMargin) < 0.01f);
    }
}

TEST_CASE ("iPad standalone and AUv3 fit the safe area as their windows narrow", "[native-ui][ipad]")
{
    const bool standalone = GENERATE (true, false);
    juce::AudioProcessor::setTypeOfNextNewPlugin (standalone ? juce::AudioProcessor::wrapperType_Standalone
                                                            : juce::AudioProcessor::wrapperType_AudioUnitv3);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    PluginEditor editor (processor, standalone);
    auto& view = PluginEditorTestPeer::view (editor);
    view.layout.touchRows = true;
    if (!standalone)
        editor.setResizable (true, false);
    processor.setProjectUiScalePercent (85);

    const auto checkViewport = [&] (juce::Rectangle<int> window, juce::BorderSize<int> insets, bool compact, const char* capture = nullptr)
    {
        editor.setBounds (window);
        const auto safe = insets.subtractedFrom (editor.getLocalBounds()).toFloat();
        PluginEditorTestPeer::layoutMobileViewport (editor, safe, false);
        CHECK (view.layout.compact == compact);
        CHECK (view.layout.phoneRowControls == (compact && !standalone));
        const auto content = view.getLocalBounds().toFloat().transformedBy (view.getTransform());
        CHECK (safe.expanded (0.01f).contains (content));
        CHECK (std::abs (content.getCentreX() - safe.getCentreX()) < 0.01f);
        CHECK (std::abs (content.getCentreY() - safe.getCentreY()) < 0.01f);
        CHECK (processor.getProjectUiScalePercent() == 85);

        const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
        if (capture != nullptr && directory.isNotEmpty())
        {
            const juce::File root (directory);
            REQUIRE (root.createDirectory().wasOk());
            juce::TemporaryFile image (root.getChildFile (capture));
            {
                auto stream = image.getFile().createOutputStream();
                REQUIRE (stream != nullptr);
                REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f), *stream));
            }
            REQUIRE (image.overwriteTargetFileWithTemporary());
        }
    };

    checkViewport ({ 0, 0, 1194, 834 }, { 24, 0, 20, 0 }, false, standalone ? "ipad-standalone-safe-area.png" : nullptr);
    checkViewport ({ 596, 0, 598, 834 }, { 24, 8, 20, 6 }, !standalone, standalone ? nullptr : "ipad-auv3-narrow-safe-area.png");
    checkViewport ({ 720, 25, 320, 690 }, { 30, 0, 34, 0 }, !standalone);
    checkViewport ({ 720, 25, 320, 690 }, { 45, 14, 20, 0 }, !standalone);
    checkViewport ({ 0, 0, 900, 450 }, { 0, 0, 0, 0 }, true);
    checkViewport ({ 0, 0, 900, 720 }, { 0, 0, 0, 0 }, false);
}

TEST_CASE ("DAW sessions remember editor zoom independently of project files", "[plugin][persistence]")
{
    PluginProcessor plugin;
    plugin.setProjectUiScalePercent (175);
    const auto saved = getPluginState (plugin);
    auto payload = nlohmann::json::parse (
        std::string_view (static_cast<const char*> (saved.getData()), saved.getSize()));
    REQUIRE (payload["editorZoomPercent"] == 175);
    REQUIRE_FALSE (payload.contains ("projectUiScalePercent"));
    CHECK_FALSE (plugin.enginePersistenceState().contains ("editorZoomPercent"));

    plugin.setProjectUiScalePercent (110);
    setPluginState (plugin, saved);
    CHECK (plugin.getProjectUiScalePercent() == 175);

    plugin.setProjectUiScalePercent (110);
    payload["projectUiScalePercent"] = 50;
    const auto project = payload.dump();
    REQUIRE (plugin.loadStateFromData (project.data(), static_cast<int> (project.size())));
    CHECK (plugin.getProjectUiScalePercent() == 110);

    plugin.resetProject();
    CHECK (plugin.getProjectUiScalePercent() == 110);

    payload.erase ("editorZoomPercent");
    setPluginStateText (plugin, payload.dump());
    CHECK (plugin.getUserUiScalePercent() == 0);
    CHECK (plugin.getProjectUiScalePercent() == 100);
}

TEST_CASE ("persistence keeps hidden channels and steps beyond the current lengths", "[plugin][persistence]")
{
    KickSnareHatEngine original;
    original.setStepCount (32);
    original.setChannelCount (4);
    original.setChannelLabel (3, "Hidden Perc");
    original.setChannelNote (3, 50);
    original.setCell (0, 3, 20, true, 77, 100, 1);
    original.setCell (0, 0, 24, true, 66, 100, 1);
    original.setSourceChannelMute (2, 3, true);
    original.setChannelCount (2);
    original.setStepCount (16);

    KickSnareHatEngine restored;
    REQUIRE (restored.deserializeForPersistence (original.serializeForPersistence()));

    REQUIRE (restored.getChannelCount() == 2);
    REQUIRE (restored.getStepCount() == 16);
    REQUIRE (restored.channelAt (3).label == "Hidden Perc");
    REQUIRE (restored.channelAt (3).note == 50);
    REQUIRE (restored.sourceCellAt (0, 3, 20).enabled);
    REQUIRE (restored.sourceCellAt (0, 3, 20).velocity == 77);
    REQUIRE (restored.sourceCellAt (0, 0, 24).velocity == 66);
    REQUIRE (restored.sourceChannelMutedAt (2, 3));
}

TEST_CASE ("persistence rejects v1 projects with missing or mistyped keys without changing anything", "[plugin][persistence]")
{
    KickSnareHatEngine engine;
    engine.setCell (0, 0, 0, true, 90, 100, 1);
    const auto before = engine.serializeForPersistence();

    for (const auto* key : { "stepCount", "channelCount", "refreshSteps", "staticSource", "rate", "swing",
                             "velocityHumanize", "timingHumanize", "deviceActive" })
    {
        CAPTURE (key);
        auto broken = before;
        broken.erase (key);
        REQUIRE_FALSE (engine.deserializeForPersistence (broken));
        REQUIRE (engine.serializeForPersistence() == before);
    }

    auto mistypedChannel = before;
    mistypedChannel["channels"][0][1] = "loud";
    REQUIRE_FALSE (engine.deserializeForPersistence (mistypedChannel));

    auto mistypedCell = before;
    mistypedCell["cells"][0][4] = "ninety";
    REQUIRE_FALSE (engine.deserializeForPersistence (mistypedCell));

    auto mistypedSettings = before;
    mistypedSettings["sourceSettings"][0][1] = 16;
    REQUIRE_FALSE (engine.deserializeForPersistence (mistypedSettings));

    REQUIRE (engine.serializeForPersistence() == before);
}

TEST_CASE ("loading a project resets channel data the file does not mention", "[plugin][persistence]")
{
    KickSnareHatEngine engine;
    engine.setChannelCount (8);
    engine.setChannelLabel (6, "Old Tom");
    engine.setChannelNote (6, 70);
    engine.setChannelLock (6, 3);

    // Older files only stored the visible channel rows.
    auto legacy = KickSnareHatEngine {}.serializeForPersistence();
    legacy["channelCount"] = 2;
    legacy["channels"] = nlohmann::json::array ({ legacy["channels"][0], legacy["channels"][1] });
    REQUIRE (engine.deserializeForPersistence (legacy));

    const KickSnareHatEngine defaults;
    REQUIRE (engine.channelAt (6).label == defaults.channelAt (6).label);
    REQUIRE (engine.channelAt (6).note == defaults.channelAt (6).note);
    REQUIRE (engine.channelAt (6).lock == defaults.channelAt (6).lock);
}

TEST_CASE ("undo-style restores keep the generated pattern instead of re-rolling it", "[plugin][persistence][generation]")
{
    double randomValue = 0.0;
    EngineCallbacks callbacks;
    callbacks.rng = [&randomValue]
    {
        randomValue = std::fmod (randomValue + 0.37, 1.0);
        return randomValue;
    };

    KickSnareHatEngine engine { callbacks };
    engine.setStepCount (8);
    engine.setChannelCount (2);
    engine.setGenerationMode (GenerationMode::perChannel);

    for (int source = 0; source < 4; ++source)
        for (int channel = 0; channel < 2; ++channel)
            engine.setCell (source, channel, 0, true, 50 + source, 100, 1);

    engine.generateWindow (0, 8, false);

    const auto generatedSources = [&engine]
    {
        std::vector<int> sources;
        for (int channel = 0; channel < 2; ++channel)
            for (int step = 0; step < 8; ++step)
                sources.push_back (engine.generatedCellAt (channel, step).source);
        return sources;
    };

    const auto before = generatedSources();
    const auto version = engine.generationVersion();
    const auto state = engine.serializeForPersistence();

    REQUIRE (engine.deserializeForPersistence (state, true));
    REQUIRE (generatedSources() == before);
    REQUIRE (engine.generationVersion() == version);

    REQUIRE (engine.deserializeForPersistence (state));
    REQUIRE (engine.generationVersion() != version);
}

TEST_CASE ("Recording quantization and calibration persist with strict optional defaults", "[plugin][persistence][record-timing]")
{
    PluginProcessor plugin;
    CHECK (plugin.isPatternRecordingQuantized());
    CHECK (plugin.getPatternRecordingOffsetMs() == 0);
    plugin.setPatternRecordingQuantizeStrength (50);
    plugin.setPatternRecordingOffsetMs (-15);
    const auto state = plugin.enginePersistenceState();
    auto payload = state.dump();
    PluginProcessor restored;
    REQUIRE (restored.loadStateFromData (payload.data(), static_cast<int> (payload.size())));
    CHECK (restored.getPatternRecordingQuantizeStrength() == 50);
    CHECK (restored.getPatternRecordingOffsetMs() == -15);
    CHECK_FALSE (restored.isPatternRecordingQuantized());
    for (const auto& bad : { nlohmann::json ("off"), nlohmann::json (50.5), nlohmann::json (-1), nlohmann::json (101) })
    {
        auto invalid = state;
        invalid["patternRecordQuantizeStrength"] = bad;
        payload = invalid.dump();
        CHECK_FALSE (restored.loadStateFromData (payload.data(), static_cast<int> (payload.size())));
        CHECK (restored.getPatternRecordingQuantizeStrength() == 50);
    }
    auto old = state;
    old.erase ("patternRecordQuantizeStrength");
    old.erase ("patternRecordOffsetMs");
    payload = old.dump();
    REQUIRE (restored.loadStateFromData (payload.data(), static_cast<int> (payload.size())));
    CHECK (restored.isPatternRecordingQuantized());
    CHECK (restored.getPatternRecordingOffsetMs() == 0);
    restored.setPatternRecordingQuantized (false);
    restored.setPatternRecordingOffsetMs (25);
    restored.resetProject();
    CHECK (restored.isPatternRecordingQuantized());
    CHECK (restored.getPatternRecordingOffsetMs() == 0);
}

TEST_CASE ("Rejected downloaded projects preserve the current save destination and edits", "[plugin][persistence]")
{
    PluginProcessor processor;
    const auto original = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("original.kshproject");
    const auto incoming = original.getSiblingFile ("downloaded.kshproject");
    processor.setProjectFile (original);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 77 }));
    PluginEditor editor (processor);
    const auto before = getPluginState (processor);
    const auto epoch = processor.getUiBridge().getDocumentEpoch();
    const auto invalid = GENERATE (std::string ("not a project"), std::string (R"({"v":1,"channelCount":0,"channels":[]})"));
    juce::String error;
    CHECK_FALSE (PluginEditorTestPeer::loadData (editor, incoming, juce::MemoryBlock (invalid.data(), invalid.size()), error));
    CHECK (error.isNotEmpty());
    CHECK (processor.getProjectFile() == original);
    CHECK (processor.getUiBridge().getDocumentEpoch() == epoch);
    CHECK (processor.isDocumentModified());
    CHECK (getPluginState (processor) == before);
}
