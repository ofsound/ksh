#include <PluginProcessor.h>

#include <KshUiBridge.h>

#include <catch2/catch_test_macros.hpp>

class PluginProcessorTestPeer
{
public:
    static bool messageThreadWorkPending (const PluginProcessor& processor) { return processor.messageThreadWorkPending.load(); }
    static void tick (PluginProcessor& processor) { processor.timerCallback(); }
    static bool hostTempoPending (const PluginProcessor& processor) { return processor.hostBpmChangePending.load(); }
};

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

    void setPpqPosition (double ppqPosition)
    {
        position.setPpqPosition (ppqPosition);
    }

    void setBpm (double bpm)
    {
        position.setBpm (bpm);
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

TEST_CASE ("audio note hits reach the UI through the message-thread timer", "[plugin][bridge][realtime]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));
    PluginProcessorTestPeer::tick (plugin);
    int hits = 0;
    plugin.getUiBridge().onNoteHit = [&] (const ksh::NativeHit&) { ++hits; };
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);
    REQUIRE (containsNoteOn (midi, 36));
    REQUIRE (PluginProcessorTestPeer::messageThreadWorkPending (plugin));
    REQUIRE (hits == 0);
    PluginProcessorTestPeer::tick (plugin);
    REQUIRE (hits == 1);
    REQUIRE (plugin.emitPendingNoteHitsForUi() == 0);
    plugin.getUiBridge().onNoteHit = {};
    plugin.setPlayHead (nullptr);
}

TEST_CASE ("MIDI uses the final edit after a burst while audio processing is suspended", "[plugin][bridge][realtime]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);
    REQUIRE (containsNoteOn (midi, 36));
    for (int note = 60; note <= 80; ++note)
        REQUIRE (plugin.dispatchUiEngineCommand ("channel_note", { 1, note }));
    // No further edit or message-thread retry is needed for the latest state.
    plugin.releaseResources();
    midi.clear();
    plugin.processBlock (buffer, midi);
    REQUIRE (containsNoteOn (midi, 80));
    REQUIRE_FALSE (containsNoteOn (midi, 60));
    plugin.setPlayHead (nullptr);
}

TEST_CASE ("host macro edits after a MIDI pattern switch keep the new pattern's rate", "[plugin][bridge][midi-input]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("rate", { "8n" }));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 1, static_cast<juce::uint8> (100)), 0);
    plugin.processBlock (buffer, midi);
    PluginProcessorTestPeer::tick (plugin);

    auto& parameters = plugin.getValueTreeState();
    REQUIRE (plugin.engineStateSnapshot().staticSource == 1);
    REQUIRE (plugin.engineStateSnapshot().rate == "16n");
    REQUIRE (static_cast<int> (parameters.getRawParameterValue ("rate")->load()) == 4);

    auto* swing = parameters.getParameter ("swing");
    swing->setValueNotifyingHost (swing->convertTo0to1 (40.0f));
    PluginProcessorTestPeer::tick (plugin);

    const auto engine = plugin.engineStateSnapshot();
    REQUIRE (engine.swing == 40);
    REQUIRE (engine.rate == "16n");
    REQUIRE (engine.sourceSettings[0].rate == "8n");
    REQUIRE (engine.sourceSettings[1].rate == "16n");
}

TEST_CASE ("bypass and state restore release notes that are still sounding", "[plugin][bridge][realtime]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 256);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);
    REQUIRE (containsNoteOn (midi, 36));
    REQUIRE_FALSE (containsNoteOff (midi, 36));

    SECTION ("bypass")
    {
        midi.clear();
        plugin.processBlockBypassed (buffer, midi);
        REQUIRE (containsNoteOff (midi, 36));
    }

    SECTION ("state restore")
    {
        juce::MemoryBlock state;
        plugin.getStateInformation (state);
        plugin.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        midi.clear();
        plugin.processBlock (buffer, midi);
        REQUIRE (containsNoteOff (midi, 36));
    }

    plugin.setPlayHead (nullptr);
}

TEST_CASE ("in-app edits mark the project modified until it is saved or loaded", "[plugin][bridge][persistence]")
{
    PluginProcessor plugin;
    REQUIRE_FALSE (plugin.isDocumentModified());

    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));
    REQUIRE (plugin.isDocumentModified());

    juce::MemoryBlock state;
    plugin.getStateInformation (state);
    REQUIRE (plugin.loadStateFromData (state.getData(), static_cast<int> (state.getSize())));
    REQUIRE_FALSE (plugin.isDocumentModified());

    plugin.setProjectMetadata ("Renamed", {}, {}, {});
    REQUIRE (plugin.isDocumentModified());
    plugin.markDocumentClean();

    const std::string_view incomplete = R"({"v":1,"channels":[],"sourceChannelMutes":[],"cells":[]})";
    REQUIRE_FALSE (plugin.loadStateFromData (incomplete.data(), static_cast<int> (incomplete.size())));
    REQUIRE (plugin.engineStateSnapshot().sources[0][0][0].enabled);
}

TEST_CASE ("ui revision bumps without an editor and leaves the default project intact", "[plugin][bridge]")
{
    PluginProcessor plugin;
    const auto before = plugin.getUiBridge().getRevision();

    REQUIRE (plugin.dispatchUiEngineCommand ("swing", { 12 }));
    REQUIRE_FALSE (plugin.dispatchUiEngineCommand ("not_a_command", {}));

    CHECK (plugin.getUiBridge().getRevision() > before);
    const auto engine = plugin.engineStateSnapshot();
    REQUIRE (engine.stepCount == 16);
    REQUIRE_FALSE (engine.sources[0][0][0].enabled);
}

TEST_CASE ("ui command applies cell edit", "[plugin][bridge]")
{
    PluginProcessor plugin;

    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1,1,5,1,90,100,1 }));

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

TEST_CASE ("ui humanize commands republish playback safely", "[plugin][bridge]")
{
    PluginProcessor plugin;
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));

    REQUIRE (plugin.dispatchUiEngineCommand ("velocity_humanize", { 1 }));
    REQUIRE (plugin.engineStateSnapshot().deviceActive);

    REQUIRE (plugin.dispatchUiEngineCommand ("timing_humanize", { 1 }));
    REQUIRE (plugin.engineStateSnapshot().deviceActive);
}

TEST_CASE ("ui humanize commands survive processBlock", "[plugin][bridge]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));

    REQUIRE (plugin.dispatchUiEngineCommand ("velocity_humanize", { 1 }));

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

    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);
    REQUIRE (countNoteOns (midi) == 1);

    REQUIRE (plugin.dispatchUiEngineCommand ("swing", { 37 }));

    midi.clear();
    plugin.processBlock (buffer, midi);
    REQUIRE (countNoteOns (midi) == 0);

    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1,1,2,1,100,100,1 }));

    midi.clear();
    plugin.processBlock (buffer, midi);
    REQUIRE (countNoteOns (midi) == 0);
}

TEST_CASE ("ui macro commands update host parameters", "[plugin][bridge]")
{
    PluginProcessor plugin;

    REQUIRE (plugin.dispatchUiEngineCommand ("swing", { 37 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("rate", { "8n" }));

    REQUIRE (plugin.getValueTreeState().getRawParameterValue ("swing")->load() == 37.0f);
    REQUIRE (plugin.getValueTreeState().getRawParameterValue ("rate")->load() == 2.0f);
}

TEST_CASE ("ui swing subdivision command updates engine state", "[plugin][bridge]")
{
    PluginProcessor plugin;

    REQUIRE (plugin.dispatchUiEngineCommand ("swing_subdivision", { 2 }));
    REQUIRE (plugin.engineStateSnapshot().swingSubdivisionIndex == 2);
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

TEST_CASE ("ui channel audition emits midi note", "[plugin][bridge]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("channel_note", { 2, 42 }));

    REQUIRE (plugin.dispatchUiEngineCommand ("channel_audition", { 2 }));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);

    REQUIRE (containsNoteOn (midi, 42));
}

TEST_CASE ("triggerRowFromUi queues audition without engine lock", "[plugin][bridge][latency]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("channel_note", { 1, 55 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("channel_note", { 3, 60 }));

    REQUIRE (plugin.triggerRowFromUi (0, 100));
    REQUIRE (plugin.triggerRowFromUi (2, 110));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);

    REQUIRE (containsNoteOnWithVelocity (midi, 55, 100));
    REQUIRE (containsNoteOnWithVelocity (midi, 60, 110));
}

TEST_CASE ("triggerRowFromUi records while auditioning when armed", "[plugin][bridge][latency][record]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);

    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_enabled", { 1, 2 }));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);

    REQUIRE (plugin.triggerRowFromUi (2, 91));

    midi.clear();
    plugin.processBlock (buffer, midi);
    REQUIRE (containsNoteOnWithVelocity (midi, 38, 91));

    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    const auto engine = plugin.engineStateSnapshot();
    REQUIRE (engine.sources[1][2][0].enabled);
    REQUIRE (engine.sources[1][2][0].velocity == 91);
}

TEST_CASE ("incoming MIDI notes 0-16 select source patterns and M", "[plugin][bridge][midi-input]")
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

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 15, static_cast<juce::uint8> (100)), 0);

    plugin.processBlock (buffer, midi);

    REQUIRE_FALSE (containsNoteOn (midi, 15));

    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    REQUIRE (plugin.engineStateSnapshot().staticSource == 15);

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 16, static_cast<juce::uint8> (100)), 0);

    plugin.processBlock (buffer, midi);

    REQUIRE_FALSE (containsNoteOn (midi, 16));

    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    REQUIRE (plugin.engineStateSnapshot().staticSource == ksh::Constants::mutedStaticSource);
}

TEST_CASE ("incoming MIDI source selector affects beat one playback immediately", "[plugin][bridge][midi-input]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);

    REQUIRE (plugin.dispatchUiEngineCommand ("static_source", { 5 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));

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
    midi.addEvent (juce::MidiMessage::noteOff (1, 16), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (100)), 8);

    plugin.processBlock (buffer, midi);

    REQUIRE_FALSE (containsNoteOff (midi, 16));
    REQUIRE (containsNoteOn (midi, 64));

    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    REQUIRE (plugin.engineStateSnapshot().staticSource == 3);
}

TEST_CASE ("filtered MIDI keeps SysEx and controllers intact beside generated notes", "[plugin][bridge][midi-input][realtime]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));

    std::array<juce::uint8, 300> sysex {};
    for (size_t i = 0; i < sysex.size(); ++i)
        sysex[i] = static_cast<juce::uint8> (i & 0x7f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 0, static_cast<juce::uint8> (100)), 0);
    midi.addEvent (juce::MidiMessage::createSysExMessage (sysex.data(), static_cast<int> (sysex.size())), 3);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 20), 5);

    plugin.processBlock (buffer, midi);

    CHECK_FALSE (containsNoteOn (midi, 0));
    CHECK (containsNoteOn (midi, 36));
    bool sysexIntact = false, controllerIntact = false;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isSysEx() && metadata.samplePosition == 3 && message.getSysExDataSize() == static_cast<int> (sysex.size()))
            sysexIntact = std::equal (sysex.begin(), sysex.end(), message.getSysExData());
        if (message.isController() && metadata.samplePosition == 5)
            controllerIntact = message.getControllerNumber() == 74 && message.getControllerValue() == 20;
    }
    CHECK (sysexIntact);
    CHECK (controllerIntact);
    plugin.setPlayHead (nullptr);
}

TEST_CASE ("host tempo outside the engine range is applied once instead of every block", "[plugin][bridge][realtime]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    playHead.setBpm (400.0);
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    plugin.processBlock (buffer, midi);
    REQUIRE (PluginProcessorTestPeer::hostTempoPending (plugin));
    PluginProcessorTestPeer::tick (plugin);
    CHECK (std::abs (plugin.engineStateSnapshot().tempo - 300.0) < 0.001);

    for (int block = 1; block <= 4; ++block)
    {
        midi.clear();
        playHead.setPpqPosition (block * 0.02);
        plugin.processBlock (buffer, midi);
        CHECK_FALSE (PluginProcessorTestPeer::hostTempoPending (plugin));
    }
    plugin.setPlayHead (nullptr);
}

TEST_CASE ("host tempo ramps are applied at a bounded rate and settle on the latest tempo", "[plugin][bridge][realtime]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    playHead.setBpm (130.0);
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    plugin.processBlock (buffer, midi);
    PluginProcessorTestPeer::tick (plugin);
    REQUIRE (std::abs (plugin.engineStateSnapshot().tempo - 130.0) < 0.001);

    for (const double bpm : { 131.0, 132.0, 133.0 })
    {
        playHead.setBpm (bpm);
        midi.clear();
        plugin.processBlock (buffer, midi);
        PluginProcessorTestPeer::tick (plugin);
    }
    CHECK (std::abs (plugin.engineStateSnapshot().tempo - 130.0) < 0.001);
    CHECK (PluginProcessorTestPeer::hostTempoPending (plugin));

    juce::Thread::sleep (120);
    PluginProcessorTestPeer::tick (plugin);
    CHECK (std::abs (plugin.engineStateSnapshot().tempo - 133.0) < 0.001);
    CHECK_FALSE (PluginProcessorTestPeer::hostTempoPending (plugin));
    plugin.setPlayHead (nullptr);
}

TEST_CASE ("incoming row MIDI notes monitor when pattern recording is off", "[plugin][bridge][midi-input]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);

    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1,1,1,1,100,100,1 }));

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

TEST_CASE ("armed qwerty recording quantizes late first step to next step", "[plugin][bridge][record]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    playHead.setPpqPosition (0.13);
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);

    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 4, 4, 1, 0, 100, 100, 1 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 4, 4, 2, 0, 100, 100, 1 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_enabled", { 1, 4 }));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);

    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_row", { 4, 73 }));

    const auto engine = plugin.engineStateSnapshot();
    REQUIRE_FALSE (engine.sources[3][3][0].enabled);
    REQUIRE (engine.sources[3][3][1].enabled);
    REQUIRE (engine.sources[3][3][1].velocity == 73);
}

TEST_CASE ("armed qwerty recording quantizes late last step back to first step", "[plugin][bridge][record]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    playHead.setPpqPosition (3.89);
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);

    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 5, 4, 1, 0, 100, 100, 1 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 5, 4, 16, 0, 100, 100, 1 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_enabled", { 1, 5 }));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);

    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_row", { 4, 74 }));

    const auto engine = plugin.engineStateSnapshot();
    REQUIRE (engine.sources[4][3][0].enabled);
    REQUIRE (engine.sources[4][3][0].velocity == 74);
    REQUIRE_FALSE (engine.sources[4][3][15].enabled);
}
