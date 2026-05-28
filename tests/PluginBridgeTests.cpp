#include <PluginProcessor.h>

#include <KshUiBridge.h>

#include <catch2/catch_test_macros.hpp>

namespace
{
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
} // namespace

TEST_CASE ("ui bridge sync_all does not crash without webview", "[plugin][bridge]")
{
    PluginProcessor plugin;

    plugin.getUiBridge().syncAll();

    REQUIRE (plugin.getEngine().stepCount == 16);
    REQUIRE (plugin.getEngine().sources[0][0][0].enabled);
}

TEST_CASE ("ui bridge handleCommand parses sync_all json", "[plugin][bridge]")
{
    PluginProcessor plugin;

    REQUIRE (plugin.getUiBridge().handleCommand (R"({"selector":"sync_all","args":[]})"));
    REQUIRE_FALSE (plugin.getUiBridge().handleCommand ("not json"));
    REQUIRE_FALSE (plugin.getUiBridge().handleCommand (R"({"args":[]})"));
}

TEST_CASE ("ui bridge handleCommand applies cell edit", "[plugin][bridge]")
{
    PluginProcessor plugin;

    REQUIRE (plugin.getUiBridge().handleCommand (
        R"({"selector":"cell","args":[1,1,5,1,90,100,1]})"));

    REQUIRE (plugin.getEngine().sources[0][0][4].enabled);
    REQUIRE (plugin.getEngine().sources[0][0][4].velocity == 90);
}

TEST_CASE ("ui bridge channel audition emits midi note", "[plugin][bridge]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);
    plugin.getEngine().setChannelNote (1, 42);

    REQUIRE (plugin.getUiBridge().handleCommand (R"({"selector":"channel_audition","args":[2]})"));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);

    REQUIRE (containsNoteOn (midi, 42));
}
