#include <PluginProcessor.h>

#include <engine/KshMidiPlayback.h>
#include <engine/KshPersistence.h>

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <exception>
#include <thread>

using namespace ksh;

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
                                 "2026-07-08T11:00:00Z");
    original.setPatternViewScale (1.5);

    const auto saved = getPluginState (original);
    const auto parsed = nlohmann::json::parse (
        std::string_view (static_cast<const char*> (saved.getData()), saved.getSize()));

    REQUIRE (parsed["projectName"] == "Pseudoflute");
    REQUIRE (parsed["projectDescription"] == "External file test");
    REQUIRE (parsed["patternViewScale"] == 1.5);

    PluginProcessor restored;
    setPluginState (restored, saved);

    REQUIRE (restored.getProjectName() == "Pseudoflute");
    REQUIRE (restored.getProjectDescription() == "External file test");
    REQUIRE (restored.getProjectCreatedAt() == "2026-07-08T10:00:00Z");
    REQUIRE (restored.getProjectModifiedAt() == "2026-07-08T11:00:00Z");
    REQUIRE (restored.getPatternViewScale() == 1.5);
    REQUIRE (restored.engineStateSnapshot().sources[0][1][4].enabled);
}

TEST_CASE ("resetProject restores default project-owned state", "[plugin][persistence]")
{
    PluginProcessor plugin;
    configureSnarePattern (plugin);
    plugin.setProjectMetadata ("Old Project", "Old description", "created", "modified");
    plugin.setPatternViewScale (1.5);

    plugin.resetProject();

    const auto state = plugin.engineStateSnapshot();
    REQUIRE (plugin.getProjectName() == "Untitled Project");
    REQUIRE (plugin.getProjectDescription().isEmpty());
    REQUIRE (plugin.getPatternViewScale() == 1.0);
    REQUIRE (state.channelCount == ksh::Constants::defaultChannelCount);
    REQUIRE (state.sources[0][0][0].enabled);
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
