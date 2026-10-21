#include <PluginProcessor.h>

#include <KshUiBridge.h>
#include <ui/KshEditorController.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <thread>

class PluginProcessorTestPeer
{
public:
    static void setRecordingClock (PluginProcessor& processor, double ppq, double timestampMs,
                                   ksh::HostLoopRange loop = {}, bool playing = true)
    {
        PluginProcessor::PatternRecordTransport anchor { ppq, 120.0, timestampMs, playing, loop, processor.getUiBridge().getDocumentEpoch() };
        processor.recordingClockHistory[processor.recordingClockHistoryIndex++ % processor.recordingClockHistory.size()] = anchor;
        processor.patternRecordTransport.publish (anchor);
    }
    static auto uiPositionAt (const PluginProcessor& processor, int source, int channel, double timestampMs)
    {
        return processor.positionForUiTrigger (source, channel, timestampMs);
    }
    static void queueRecordingMonitor (PluginProcessor& processor, double onsetPpq)
    {
        [[maybe_unused]] const bool queued = processor.uiRecordingMonitors.try_enqueue ({
            processor.recordingMonitorSession.load(), 0, 0, 1, onsetPpq, processor.recordingTransportGeneration });
    }
    static bool messageThreadWorkPending (const PluginProcessor& processor) { return processor.messageThreadWorkPending.load(); }
    static void tick (PluginProcessor& processor) { processor.timerCallback(); }
    static bool hostTempoPending (const PluginProcessor& processor) { return processor.hostBpmChangePending.load(); }
    static std::array<size_t, 2> pendingRecordCounts (const PluginProcessor& processor)
    {
        return { processor.pendingMidiPatternRecordEvents.size_approx(), processor.pendingUiPatternRecordEvents.size_approx() };
    }
    static uint64_t recordingSession (const PluginProcessor& processor) { return processor.patternRecordingSession.load(); }
    static bool enqueueLateRecord (PluginProcessor& processor, uint64_t session)
    {
        return PluginProcessor::enqueuePatternRecordEvent (processor.pendingMidiPatternRecordEvents, 0, 0, 7, 99, session);
    }
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

TEST_CASE ("Recording passes undo MIDI and keyboard overdubs across loop iterations", "[plugin][record][record-undo]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 72, 63, 4, 5, 3 }));
    ksh::ui::EditorController controller (plugin);
    const auto original = plugin.editorDocument();
    juce::AudioBuffer<float> buffer (2, 512);
    const auto midiHit = [&] (double beat, int note, int velocity)
    {
        playHead.setPpqPosition (beat);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (velocity)), 0);
        plugin.processBlock (buffer, midi);
    };
    controller.command ("pattern_record_enabled", { 1, 1 });
    const auto session = PluginProcessorTestPeer::recordingSession (plugin);
    midiHit (0.0, 36, 100);
    (void) controller.poll();
    REQUIRE (controller.canUndo());
    REQUIRE (plugin.triggerRowFromUi (2, 91));
    (void) controller.poll();
    midiHit (4.25, 36, 85); // Second traversal of the same loop.
    (void) controller.poll();
    midiHit (8.0, 36, 110); // A later overwrite belongs to the same pass.
    (void) controller.poll();

    controller.begin ("Manual edit during recording");
    auto manual = controller.cell ({ 1, 5 });
    manual.enabled = true;
    manual.probability = 42;
    controller.editCell ({ 1, 5 }, manual);
    controller.commit();
    midiHit (8.5, 36, 80); // Still queued when Record is clicked off.
    controller.command ("pattern_record_enabled", { 0, 1 });
    const auto recorded = plugin.editorDocument();
    CHECK (recorded.sources[0][0][0].velocity == 110);
    CHECK (recorded.sources[0][0][1].enabled);
    CHECK (recorded.sources[0][0][2].enabled);
    CHECK (recorded.sources[0][2][0].enabled);
    CHECK_FALSE (plugin.isPatternRecordingEnabled());

    controller.undo();
    auto expected = original;
    expected.sources[0][1][5] = manual;
    CHECK (plugin.editorDocument() == expected);
    controller.redo();
    CHECK (plugin.editorDocument() == recorded);
    controller.undo();
    controller.undo();
    CHECK (plugin.editorDocument() == original);
    CHECK_FALSE (controller.canUndo());

    // An in-flight audio block cannot resurrect a stopped take after undo.
    REQUIRE (PluginProcessorTestPeer::enqueueLateRecord (plugin, session));
    PluginProcessorTestPeer::tick (plugin);
    CHECK (plugin.editorDocument() == original);
}

TEST_CASE ("Recording undo separates passes and supports undo while still armed", "[plugin][record][record-undo]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);
    ksh::ui::EditorController controller (plugin);
    const auto original = plugin.editorDocument();
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);
    controller.command ("pattern_record_enabled", { 1, 1 });
    controller.command ("pattern_record_enabled", { 0, 1 });
    CHECK_FALSE (controller.canUndo());
    controller.command ("pattern_record_enabled", { 1, 1 });
    REQUIRE (plugin.triggerRowFromUi (0, 90));
    controller.command ("pattern_record_enabled", { 0, 1 });
    const auto firstPass = plugin.editorDocument();
    REQUIRE (firstPass != original);

    controller.command ("pattern_record_enabled", { 1, 1 });
    REQUIRE (plugin.triggerRowFromUi (1, 80));
    const bool stopBeforeUndo = GENERATE (false, true);
    if (stopBeforeUndo)
        controller.command ("pattern_record_enabled", { 0, 1 });
    controller.undo();
    CHECK (plugin.editorDocument() == firstPass);
    CHECK (plugin.isPatternRecordingEnabled() == !stopBeforeUndo);
    if (!stopBeforeUndo)
    {
        REQUIRE (plugin.triggerRowFromUi (2, 70));
        controller.command ("pattern_record_enabled", { 0, 1 });
        controller.undo();
        CHECK (plugin.editorDocument() == firstPass);
    }
    controller.undo();
    CHECK (plugin.editorDocument() == original);
}

TEST_CASE ("Recording undo preserves intervening and later edits to the same cell", "[plugin][record][record-undo]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 72, 63, 4, 5, 3 }));
    ksh::ui::EditorController controller (plugin);
    auto expected = plugin.editorDocument();
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);
    controller.command ("pattern_record_enabled", { 1, 1 });
    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_row", { 1, 90 }));
    if (GENERATE (false, true))
        (void) controller.poll();
    const auto previous = plugin.editorDocument().sources[0][0][0];
    auto external = previous;
    external.velocity = 55;
    external.probability = 33;
    REQUIRE (plugin.applyNativeCellEdits ({ { 0, 0, 0, previous, external } }, controller.state.documentEpoch));
    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_row", { 1, 110 }));
    controller.command ("pattern_record_enabled", { 0, 1 });
    const auto recorded = plugin.editorDocument();
    expected.sources[0][0][0].velocity = 55;
    expected.sources[0][0][0].probability = 33;
    controller.undo();
    CHECK (plugin.editorDocument() == expected);
    controller.redo();
    CHECK (plugin.editorDocument() == recorded);

    auto later = recorded.sources[0][0][0];
    later.probability = 41;
    REQUIRE (plugin.applyNativeCellEdits ({ { 0, 0, 0, recorded.sources[0][0][0], later } }, controller.state.documentEpoch));
    controller.undo();
    expected.sources[0][0][0].probability = 41;
    CHECK (plugin.editorDocument() == expected);
}

TEST_CASE ("Replacing the project discards recording history and in-flight hits", "[plugin][record][record-undo]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);
    ksh::ui::EditorController controller (plugin);
    juce::MemoryBlock saved;
    plugin.getStateInformation (saved);
    const auto original = plugin.editorDocument();
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);
    controller.command ("pattern_record_enabled", { 1, 1 });
    const auto session = PluginProcessorTestPeer::recordingSession (plugin);
    REQUIRE (plugin.triggerRowFromUi (0, 90));
    (void) controller.poll();
    REQUIRE (controller.canUndo());
    if (GENERATE (false, true))
        plugin.resetProject();
    else
        plugin.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (PluginProcessorTestPeer::enqueueLateRecord (plugin, session));
    (void) controller.poll();
    CHECK_FALSE (plugin.isPatternRecordingEnabled());
    CHECK_FALSE (controller.canUndo());
    CHECK_FALSE (controller.canRedo());
    CHECK (plugin.editorDocument() == original);
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

TEST_CASE ("MIDI and UI recording have independent producer queues", "[plugin][bridge][record][realtime]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    playHead.setBpm (20.0);
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 64);
    plugin.setPatternRecordingEnabled (true, 0);
    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (81)), 0);
    plugin.processBlock (buffer, midi);
    REQUIRE (plugin.triggerRowFromUi (1, 93));

    REQUIRE (PluginProcessorTestPeer::pendingRecordCounts (plugin) == std::array<size_t, 2> { 1, 1 });
    PluginProcessorTestPeer::tick (plugin);
    REQUIRE (PluginProcessorTestPeer::pendingRecordCounts (plugin) == std::array<size_t, 2> { 0, 0 });
    const auto state = plugin.engineStateSnapshot();
    REQUIRE (state.sources[0][0][0].enabled);
    REQUIRE (state.sources[0][0][0].velocity == 81);
    REQUIRE (state.sources[0][1][0].enabled);
    REQUIRE (state.sources[0][1][0].velocity == 93);
}

TEST_CASE ("MIDI and UI recording preserve both streams under concurrent input", "[plugin][bridge][record][realtime]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    // Within the UI extrapolation window, triggers always quantize to step one.
    playHead.setBpm (20.0);
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 64);
    plugin.setPatternRecordingEnabled (true, 0);
    constexpr int rounds = 256;
    juce::AudioBuffer<float> initialBuffer (2, 64);
    juce::MidiBuffer initialMidi;
    plugin.processBlock (initialBuffer, initialMidi);
    PluginProcessorTestPeer::tick (plugin);
    std::atomic<int> requested { 0 }, completed { 0 };
    std::atomic<bool> cancelled { false };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;
        midi.ensureSize (32768);
        for (int round = 1; round <= rounds; ++round)
        {
            while (requested.load (std::memory_order_acquire) < round)
            {
                if (cancelled.load (std::memory_order_acquire))
                    return;
                std::this_thread::yield();
            }
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (round % 127 + 1)), 0);
            plugin.processBlock (buffer, midi);
            completed.store (round, std::memory_order_release);
        }
    });
    const juce::ScopeGuard joinAudio { [&]
    {
        cancelled.store (true, std::memory_order_release);
        audio.join();
    } };

    for (int round = 1; round <= rounds; ++round)
    {
        CAPTURE (round);
        requested.store (round, std::memory_order_release);
        const int uiVelocity = (round + 31) % 127 + 1;
        REQUIRE (plugin.triggerRowFromUi (1, uiVelocity));
        while (completed.load (std::memory_order_acquire) < round)
            std::this_thread::yield();
        PluginProcessorTestPeer::tick (plugin);
        const auto state = plugin.engineStateSnapshot();
        REQUIRE (state.sources[0][0][0].velocity == round % 127 + 1);
        REQUIRE (state.sources[0][1][0].velocity == uiVelocity);
        REQUIRE (PluginProcessorTestPeer::pendingRecordCounts (plugin) == std::array<size_t, 2> { 0, 0 });
    }
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

TEST_CASE ("Recording indicator pulses on transport beats only while armed", "[plugin][bridge][record]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    playHead.setBpm (20.0);
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    CHECK (plugin.getRecordingPulseForUi() == 1.0f);
    plugin.setPatternRecordingEnabled (true, 0);
    CHECK (plugin.getRecordingPulseForUi() == 1.0f);
    for (double beat : { 0.0, 0.5, 1.0, 1.5, 4.0, -0.5 })
    {
        playHead.setPpqPosition (beat);
        plugin.processBlock (buffer, midi);
        const float pulse = plugin.getRecordingPulseForUi();
        if (beat == std::floor (beat))
            CHECK (pulse > 0.9f);
        else
            CHECK (pulse < 0.5f);
        CHECK (pulse >= 0.35f);
    }
    plugin.setPatternRecordingEnabled (false, 0);
    CHECK (plugin.getRecordingPulseForUi() == 1.0f);
    plugin.setPlayHead (nullptr);
    plugin.processBlock (buffer, midi);
    plugin.setPatternRecordingEnabled (true, 0);
    CHECK (plugin.getRecordingPulseForUi() == 1.0f);
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

TEST_CASE ("Unlocked recording retains played timing across audio buffer sizes", "[plugin][record][record-timing]")
{
    const int blockSize = GENERATE (64, 128, 256, 512, 1024);
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, blockSize);
    plugin.setPatternRecordingQuantized (false);
    plugin.setPatternRecordingEnabled (true, 0);
    constexpr std::array<int, 5> inputSamples { 6337, 11619, 30323, 47521, 95523 };
    int monitored = 0;
    for (int sample = 0, block = 0; sample < 96000; ++block)
    {
        const int size = std::min (block % 2 == 0 ? blockSize : 73, 96000 - sample);
        juce::AudioBuffer<float> buffer (2, size);
        juce::MidiBuffer midi;
        for (size_t hit = 0; hit < inputSamples.size(); ++hit)
            if (inputSamples[hit] >= sample && inputSamples[hit] < sample + size)
                midi.addEvent (juce::MidiMessage::noteOn (1, 36 + static_cast<int> (hit), static_cast<juce::uint8> (90)), inputSamples[hit] - sample);
        playHead.setPpqPosition (static_cast<double> (sample) / 24000.0);
        plugin.processBlock (buffer, midi);
        monitored += countNoteOns (midi);
        PluginProcessorTestPeer::tick (plugin);
        sample += size;
    }
    // Capture monitoring is independent of quantization and does not double-trigger
    // a newly committed early note on the next callback.
    CHECK (monitored == static_cast<int> (inputSamples.size()));
    plugin.setPatternRecordingEnabled (false, 0);
    const auto snapshot = plugin.enginePlaybackSnapshot();
    std::array<int, 5> replaySamples {};
    replaySamples.fill (-1);
    ksh::MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    for (int sample = 0; sample < 96000; sample += blockSize)
    {
        juce::MidiBuffer midi;
        runner.processBlock (snapshot, static_cast<double> (sample) / 24000.0, 120.0, true,
                             std::min (blockSize, 96000 - sample), midi);
        for (const auto event : midi)
            if (event.getMessage().isNoteOn() && sample + event.samplePosition > 0)
            {
                const int channel = event.getMessage().getNoteNumber() - 36;
                if (channel >= 0 && channel < 5)
                    replaySamples[static_cast<size_t> (channel)] = sample + event.samplePosition;
            }
    }
    for (size_t hit = 0; hit < inputSamples.size(); ++hit)
    {
        CAPTURE (blockSize, hit, replaySamples[hit]);
        REQUIRE (replaySamples[hit] >= 0);
        CHECK (std::abs (replaySamples[hit] - inputSamples[hit]) <= 31);
    }
    CHECK (replaySamples == std::array<int, 5> { 6360, 11640, 30300, 47520, 95520 });
    CHECK (snapshot.sources[0][0][1].nudge == 6);
    CHECK (snapshot.sources[0][1][2].nudge == -6);
    CHECK (snapshot.sources[0][4][0].nudge == -8);
}

TEST_CASE ("Standalone timestamp capture agrees with the keyboard clock before block remapping", "[plugin][record][record-timing]")
{
    const int blockSize = GENERATE (64, 128, 256, 512, 1024);
    const int strength = GENERATE (0, 50, 100);
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, blockSize);
    plugin.setTimestampedStandaloneMidiInput (true);
    plugin.setPatternRecordingQuantizeStrength (strength);
    plugin.setPatternRecordingEnabled (true, 0);
    PluginProcessorTestPeer::setRecordingClock (plugin, 0.2, 1000.0);
    const auto keyboard = PluginProcessorTestPeer::uiPositionAt (plugin, 0, 0, 1015.0);
    REQUIRE (keyboard.has_value());
    CHECK (keyboard->step == 1);
    CHECK (keyboard->nudge == (strength == 100 ? 0 : strength == 50 ? -4 : -8));
    auto note = juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (93));
    note.setTimeStamp (1.015);
    plugin.captureStandaloneMidiInput (note);
    // Queue decisions survive a mode/offset change before audio or UI catches up.
    plugin.setPatternRecordingQuantized (true);
    plugin.setPatternRecordingOffsetMs (25);
    playHead.setPpqPosition (0.25);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent (note, 0); // JUCE collector representation must not record it a second time.
    plugin.processBlock (buffer, midi);
    PluginProcessorTestPeer::tick (plugin);
    const auto recorded = plugin.editorDocument().sources[0][0][1];
    CHECK (recorded.enabled);
    CHECK (recorded.velocity == 93);
    CHECK (recorded.nudge == keyboard->nudge);
    CHECK (countNoteOns (midi) == 1);
}

TEST_CASE ("Recording timing accounts for swing channel loops and playback direction", "[plugin][record][record-timing]")
{
    const auto mode = GENERATE (ksh::PlaybackMode::normal, ksh::PlaybackMode::reverse,
                                ksh::PlaybackMode::ping_pong, ksh::PlaybackMode::reverse_ping_pong);
    PluginProcessor plugin;
    auto document = plugin.editorDocument();
    document.settings.swing = 50;
    document.settings.channels[0].playbackMode = mode;
    document.settings.sourceSettings[0].loopRanges[0] = { 3, 4 };
    REQUIRE (plugin.applyEditorDocument (document));
    const auto snapshot = plugin.enginePlaybackSnapshot();
    const auto grid = ksh::patternRecordingGrid (snapshot, 0);
    // Occurrence 1 sounds at 0.3125 PPQ with this swing, so 0.33 is 7% late.
    const auto position = ksh::patternRecordingPosition (grid, 0, 0.33, 0);
    CHECK (position.nudge == 7);
    CHECK (position.step == (mode == ksh::PlaybackMode::reverse || mode == ksh::PlaybackMode::reverse_ping_pong ? 5 : 4));
    CHECK (std::abs (position.onsetPpq - 0.33) < 0.0013);
    const auto half = ksh::patternRecordingPosition (grid, 0, 0.33, 50);
    CHECK (half.nudge == 4);
    const auto locked = ksh::patternRecordingPosition (grid, 0, 0.33, 100);
    CHECK (locked.nudge == 0);

    plugin.setPatternRecordingQuantized (false);
    plugin.setPatternRecordingOffsetMs (-10);
    PluginProcessorTestPeer::setRecordingClock (plugin, 0.005, 2000.0, { true, 0.0, 4.0 });
    const auto wrapped = PluginProcessorTestPeer::uiPositionAt (plugin, 0, 0, 2000.0);
    REQUIRE (wrapped.has_value());
    CHECK (wrapped->nudge == -6); // Calibration advances across the loop seam.
    PluginProcessorTestPeer::setRecordingClock (plugin, 3.99, 3000.0, { true, 0.0, 4.0 });
    plugin.setPatternRecordingOffsetMs (0);
    const auto nextLoop = PluginProcessorTestPeer::uiPositionAt (plugin, 0, 0, 3010.0);
    REQUIRE (nextLoop.has_value());
    CHECK (nextLoop->nudge == 4);
    CHECK_FALSE (PluginProcessorTestPeer::uiPositionAt (plugin, 0, 0, 3300.0).has_value());
    PluginProcessorTestPeer::setRecordingClock (plugin, 0.0, 4000.0, {}, false);
    CHECK_FALSE (PluginProcessorTestPeer::uiPositionAt (plugin, 0, 0, 4010.0).has_value());
}

TEST_CASE ("Captured nudges survive deferred application overwrites undo and saving", "[plugin][record][record-timing][record-undo]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 64);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 2, 1, 72, 100, 1 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell_nudge", { 1, 1, 2, -30 }));
    ksh::ui::EditorController controller (plugin);
    const auto before = plugin.editorDocument();
    plugin.setPatternRecordingQuantized (false);
    controller.command ("pattern_record_enabled", { 1, 1 });
    juce::AudioBuffer<float> buffer (2, 64);
    for (const auto beat : { 0.27, 4.30 })
    {
        playHead.setPpqPosition (beat);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, static_cast<juce::uint8> (95)), 0);
        plugin.processBlock (buffer, midi);
    }
    // Deliberately do not service the message thread until both hits have been captured.
    plugin.setPatternRecordingQuantized (true);
    controller.command ("pattern_record_enabled", { 0, 1 });
    (void) controller.poll();
    const auto recorded = plugin.editorDocument();
    CHECK (recorded.sources[0][0][1].nudge == 20);
    CHECK (recorded.sources[0][0][1].velocity == 95);
    controller.undo();
    CHECK (plugin.editorDocument() == before);
    controller.redo();
    CHECK (plugin.editorDocument() == recorded);
    const auto saved = plugin.enginePersistenceState().dump();
    PluginProcessor restored;
    REQUIRE (restored.loadStateFromData (saved.data(), static_cast<int> (saved.size())));
    CHECK (restored.editorDocument() == recorded);
}

TEST_CASE ("Transport seeks invalidate queued and already scheduled recording monitors", "[plugin][record][record-timing]")
{
    const bool queuedBeforeSeek = GENERATE (true, false);
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 64);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 2, 1, 100, 100, 1 }));
    plugin.setPatternRecordingEnabled (true, 0);
    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);
    PluginProcessorTestPeer::queueRecordingMonitor (plugin, 0.25);
    if (!queuedBeforeSeek)
    {
        playHead.setPpqPosition (64.0 / 24000.0);
        midi.clear();
        plugin.processBlock (buffer, midi);
    }
    const int begin = queuedBeforeSeek ? 64 : 128;
    int notes = 0;
    for (int sample = begin; sample < 6200; sample += 64)
    {
        // Jump exactly one pattern ahead. Its next cell has the same monotonic
        // sample deadline as the old monitor, but belongs to the new transport pass.
        playHead.setPpqPosition (4.0 + static_cast<double> (sample) / 24000.0);
        midi.clear();
        plugin.processBlock (buffer, midi);
        notes += countNoteOns (midi);
    }
    CHECK (notes == 1);
}
