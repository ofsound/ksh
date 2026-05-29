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

    const auto engine = plugin.engineStateSnapshot();
    REQUIRE (engine.stepCount == 16);
    REQUIRE (engine.sources[0][0][0].enabled);
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

    const auto engine = plugin.engineStateSnapshot();
    REQUIRE (engine.sources[0][0][4].enabled);
    REQUIRE (engine.sources[0][0][4].velocity == 90);
}

TEST_CASE ("processor command setVelocityHumanize does not crash", "[plugin][bridge]")
{
    PluginProcessor plugin;
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));

    REQUIRE (plugin.dispatchUiEngineCommand ("velocity_humanize", { 1 }));

    REQUIRE (plugin.engineStateSnapshot().deviceActive);
}

TEST_CASE ("ui bridge humanize commands rebuild native playback safely", "[plugin][bridge]")
{
    PluginProcessor plugin;
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));

    REQUIRE (plugin.getUiBridge().handleCommand (R"({"selector":"velocity_humanize","args":[1]})"));
    REQUIRE (plugin.engineStateSnapshot().deviceActive);

    REQUIRE (plugin.getUiBridge().handleCommand (R"({"selector":"timing_humanize","args":[1]})"));
    REQUIRE (plugin.engineStateSnapshot().deviceActive);
}

TEST_CASE ("ui bridge humanize commands survive processBlock", "[plugin][bridge]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));

    REQUIRE (plugin.getUiBridge().handleCommand (R"({"selector":"velocity_humanize","args":[1]})"));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);

    REQUIRE (plugin.engineStateSnapshot().deviceActive);
}

TEST_CASE ("ui bridge macro commands update host parameters", "[plugin][bridge]")
{
    PluginProcessor plugin;

    REQUIRE (plugin.getUiBridge().handleCommand (R"({"selector":"swing","args":[37]})"));
    REQUIRE (plugin.getUiBridge().handleCommand (R"({"selector":"rate","args":["8n"]})"));

    REQUIRE (plugin.getValueTreeState().getRawParameterValue ("swing")->load() == 37.0f);
    REQUIRE (plugin.getValueTreeState().getRawParameterValue ("rate")->load() == 2.0f);
}

TEST_CASE ("host macro parameter changes update engine", "[plugin][bridge]")
{
    PluginProcessor plugin;
    auto* swing = plugin.getValueTreeState().getParameter ("swing");

    REQUIRE (swing != nullptr);

    swing->setValueNotifyingHost (swing->convertTo0to1 (41.0f));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    REQUIRE (plugin.engineStateSnapshot().swing == 41);
}

TEST_CASE ("ui bridge channel audition emits midi note", "[plugin][bridge]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("channel_note", { 2, 42 }));

    REQUIRE (plugin.getUiBridge().handleCommand (R"({"selector":"channel_audition","args":[2]})"));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);

    REQUIRE (containsNoteOn (midi, 42));
}
