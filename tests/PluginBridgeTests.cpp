#include <PluginProcessor.h>

#include <KshUiBridge.h>

#include <catch2/catch_test_macros.hpp>

namespace
{
class FixedPlayHead final : public juce::AudioPlayHead
{
public:
    FixedPlayHead()
    {
        position.setBpm (120.0);
        position.setPpqPosition (0.0);
        position.setIsPlaying (true);
    }

    juce::Optional<PositionInfo> getPosition() const override
    {
        return position;
    }

private:
    PositionInfo position;
};

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

bool containsNoteOnWithVelocity (const juce::MidiBuffer& midi, int pitch, int velocity)
{
    for (const auto metadata : midi)
    {
        const auto& message = metadata.getMessage();

        if (message.isNoteOn() && message.getNoteNumber() == pitch && message.getVelocity() == velocity)
            return true;
    }

    return false;
}

bool containsNoteOff (const juce::MidiBuffer& midi, int pitch)
{
    for (const auto metadata : midi)
    {
        const auto& message = metadata.getMessage();

        if (message.isNoteOff() && message.getNoteNumber() == pitch)
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

TEST_CASE ("ui playback edits do not retrigger the current step", "[plugin][bridge][transport]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);
    REQUIRE (countNoteOns (midi) == 1);

    REQUIRE (plugin.getUiBridge().handleCommand (R"({"selector":"swing","args":[37]})"));

    midi.clear();
    plugin.processBlock (buffer, midi);
    REQUIRE (countNoteOns (midi) == 0);

    REQUIRE (plugin.getUiBridge().handleCommand (R"({"selector":"cell","args":[1,1,2,1,100,100,1]})"));

    midi.clear();
    plugin.processBlock (buffer, midi);
    REQUIRE (countNoteOns (midi) == 0);
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

TEST_CASE ("incoming MIDI notes 0-7 select source patterns", "[plugin][bridge][midi-input]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("static_source", { 5 }));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 0, static_cast<juce::uint8> (100)), 0);

    plugin.processBlock (buffer, midi);

    REQUIRE_FALSE (containsNoteOn (midi, 0));

    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    REQUIRE (plugin.engineStateSnapshot().staticSource == 0);

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 7, static_cast<juce::uint8> (100)), 0);

    plugin.processBlock (buffer, midi);

    REQUIRE_FALSE (containsNoteOn (midi, 7));

    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    REQUIRE (plugin.engineStateSnapshot().staticSource == 7);
}

TEST_CASE ("incoming MIDI source selector affects beat one playback immediately", "[plugin][bridge][midi-input]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);

    REQUIRE (plugin.dispatchUiEngineCommand ("static_source", { 5 }));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 0, static_cast<juce::uint8> (100)), 0);

    plugin.processBlock (buffer, midi);

    REQUIRE_FALSE (containsNoteOn (midi, 0));
    REQUIRE (containsNoteOn (midi, 36));

    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    REQUIRE (plugin.engineStateSnapshot().staticSource == 0);
}

TEST_CASE ("incoming MIDI source selector notes filter note-offs and preserve other MIDI", "[plugin][bridge][midi-input]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("static_source", { 4 }));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOff (1, 0), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (100)), 8);

    plugin.processBlock (buffer, midi);

    REQUIRE_FALSE (containsNoteOff (midi, 0));
    REQUIRE (containsNoteOn (midi, 64));

    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    REQUIRE (plugin.engineStateSnapshot().staticSource == 3);
}

TEST_CASE ("incoming row MIDI notes monitor when pattern recording is off", "[plugin][bridge][midi-input]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 38, static_cast<juce::uint8> (91)), 0);

    plugin.processBlock (buffer, midi);

    REQUIRE (containsNoteOnWithVelocity (midi, 38, 91));

    const auto engine = plugin.engineStateSnapshot();
    REQUIRE_FALSE (engine.sources[0][2][0].enabled);
}

TEST_CASE ("armed pattern recording captures incoming row MIDI notes", "[plugin][bridge][midi-input][record]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);

    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_enabled", { 1, 2 }));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 38, static_cast<juce::uint8> (91)), 0);

    plugin.processBlock (buffer, midi);

    REQUIRE (containsNoteOnWithVelocity (midi, 38, 91));

    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    const auto engine = plugin.engineStateSnapshot();
    REQUIRE (engine.sources[1][2][0].enabled);
    REQUIRE (engine.sources[1][2][0].velocity == 91);
}

TEST_CASE ("armed pattern recording captures qwerty row command at current step", "[plugin][bridge][record]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);

    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_enabled", { 1, 3 }));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);

    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_row", { 4, 77 }));

    midi.clear();
    plugin.processBlock (buffer, midi);

    const auto engine = plugin.engineStateSnapshot();
    REQUIRE (engine.sources[2][3][0].enabled);
    REQUIRE (engine.sources[2][3][0].velocity == 77);
    REQUIRE (containsNoteOnWithVelocity (midi, 39, 77));
}
