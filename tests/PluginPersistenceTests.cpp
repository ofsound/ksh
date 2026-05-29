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
    auto& engine = plugin.getEngine();
    engine.setStepCount (16);
    engine.setChannelCount (2);
    engine.setChannelNote (1, 38);
    engine.setCell (0, 1, 4, true, 90, 100, 1);
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

TEST_CASE ("parsePersistencePayload unwraps M4L pattern wrapper", "[plugin][persistence]")
{
    const std::string wrapped =
        R"({"ksh_pattern_data":[{"v":1,"stepCount":8,"channelCount":2,"refreshSteps":4,"generationMode":"static","staticSource":0,"rate":"16n","tempo":120,"swing":0,"velocityHumanize":0,"timingHumanize":0,"deviceActive":1,"phaseOffsetBeats":0,"channels":[["Kick",36,-1,16,"normal"],["Snare",38,-1,16,"normal"]],"sourceChannelMutes":[[0,0],[0,0],[0,0],[0,0]],"cells":[]}]})";

    const auto parsed = parsePersistencePayload (wrapped);

    REQUIRE (parsed.has_value());
    REQUIRE ((*parsed)["v"] == 1);
    REQUIRE ((*parsed)["stepCount"] == 8);
}

TEST_CASE ("parsePersistencePayload accepts chunked M4L atoms", "[plugin][persistence]")
{
    const std::string payload =
        R"({"v":1,"stepCount":8,"channelCount":2,"refreshSteps":4,"generationMode":"static","staticSource":0,"rate":"16n","tempo":120,"swing":0,"velocityHumanize":0,"timingHumanize":0,"deviceActive":1,"phaseOffsetBeats":0,"channels":[["Kick",36,-1,16,"normal"]],"sourceChannelMutes":[[0],[0],[0],[0]],"cells":[]})";
    const nlohmann::json atoms = nlohmann::json::array ({
        "ksh_json_chunks_v1",
        juce::URL::addEscapeChars (payload, true).toStdString()
    });
    const auto parsed = parsePersistencePayload (atoms.dump());

    REQUIRE (parsed.has_value());
    REQUIRE ((*parsed)["v"] == 1);
    REQUIRE ((*parsed)["stepCount"] == 8);
}

TEST_CASE ("plugin state roundtrips through getStateInformation", "[plugin][persistence]")
{
    PluginProcessor original;
    configureSnarePattern (original);

    const auto saved = getPluginState (original);

    PluginProcessor restored;
    setPluginState (restored, saved);

    const auto& engine = restored.getEngine();
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

    const auto blockStartBeat = 4.0 * restored.getEngine().beatsPerStep();
    juce::MidiBuffer midi;
    [[maybe_unused]] const auto result =
        runner.processBlock (restored.getEngine().makePlaybackSnapshot(), blockStartBeat, 120.0, true, 512, midi);

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
    REQUIRE (restored.getEngine().channelCount == 2);
    REQUIRE (restored.getEngine().sources[0][1][4].enabled);
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

TEST_CASE ("invalid plugin state is ignored", "[plugin][persistence]")
{
    PluginProcessor plugin;
    configureSnarePattern (plugin);

    setPluginStateText (plugin, "not json");
    REQUIRE (plugin.getEngine().sources[0][1][4].enabled);

    setPluginStateText (plugin, R"({"v":1,"stepCount":8,"channelCount":2,"cells":})");
    REQUIRE (plugin.getEngine().sources[0][1][4].enabled);
}
