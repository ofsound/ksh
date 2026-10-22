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
    REQUIRE (engine.channelCount == 2);
    REQUIRE (engine.channels[1].note == 38);
    REQUIRE (engine.sources[0][1][4].enabled);
    REQUIRE (engine.sources[0][1][4].velocity == 90);
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
    REQUIRE (engine.channelCount == 2);
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
    PluginProcessor processor;
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
    PluginProcessor restarted;
    setPluginState (restarted, data);
    ksh::restoreStandaloneProjectFile (restarted, settings);
    REQUIRE (restarted.getProjectFile() == file);
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
    const auto saved = nlohmann::json::parse (file.loadFileAsString().toStdString());
    PluginProcessor loaded;
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

TEST_CASE ("UI scale is independent of saved and legacy projects", "[plugin][persistence]")
{
    PluginProcessor plugin;
    plugin.setProjectUiScalePercent (75);
    const auto saved = getPluginState (plugin);
    auto payload = nlohmann::json::parse (
        std::string_view (static_cast<const char*> (saved.getData()), saved.getSize()));
    REQUIRE_FALSE (payload.contains ("projectUiScalePercent"));

    plugin.setProjectUiScalePercent (110);
    setPluginState (plugin, saved);
    CHECK (plugin.getProjectUiScalePercent() == 110);

    payload["projectUiScalePercent"] = 50;
    setPluginStateText (plugin, payload.dump());
    CHECK (plugin.getProjectUiScalePercent() == 110);

    plugin.resetProject();
    CHECK (plugin.getProjectUiScalePercent() == 110);
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
