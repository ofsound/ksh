#include "helpers/test_helpers.h"
#include <PluginProcessor.h>

#include <KshUiBridge.h>
#include <ui/KshEditorController.h>
#include "../standalone/StandaloneMidiRouting.h"

#include <catch2/catch_approx.hpp>
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
    static std::array<size_t, 2> pendingFeedbackCounts (const PluginProcessor& processor)
    {
        return { processor.noteHitsForUi.size_approx(), processor.cycleVisitsForUi.size_approx() };
    }
    static bool usesReservedMidiStorage (const PluginProcessor& processor, const juce::MidiBuffer& midi)
    {
        return std::ranges::find (processor.reservedMidiStorage, midi.data.begin()) != processor.reservedMidiStorage.end();
    }
    static double jamFirstHitTime (const PluginProcessor& processor) { return processor.jamHits.front().timestampMs; }
    static bool enqueueLateRecord (PluginProcessor& processor, uint64_t session)
    {
        return PluginProcessor::enqueuePatternRecordEvent (processor.pendingMidiPatternRecordEvents, 0, 0, 7, 99, session);
    }
};

TEST_CASE ("Jam infers one to four bars of a free performance", "[plugin][jam]")
{
    const int bars = GENERATE (1, 2, 3, 4);
    constexpr double start = 1000.0, quarter = 480.0;
    std::vector<ksh::JamHit> hits;
    for (int beat = 0; beat < 4 * bars; ++beat)
    {
        hits.push_back ({ start + beat * quarter, beat % 2, 102 });
        hits.push_back ({ start + (beat + 0.5) * quarter + 8.0, 2, 70 });
    }
    const auto take = ksh::transcribeJam (hits, start + 4 * bars * quarter, 120.0, 0, 0);
    REQUIRE (take);
    CHECK (take->bpm == Catch::Approx (125.0));
    CHECK (take->steps == 16 * bars);
    CHECK (take->droppedHits == 0);
    CHECK (take->pattern[0][0].enabled);
    CHECK (take->pattern[1][4].velocity == 102);
    CHECK (take->pattern[2][2].velocity == 70);
    CHECK (take->pattern[2][2].nudge == 7);
}

TEST_CASE ("Jam reads steady single-row taps as quarter notes", "[plugin][jam]")
{
    const int bars = GENERATE (1, 2, 3, 4);
    constexpr double start = 1000.0, quarter = 750.0;
    std::vector<ksh::JamHit> hits;
    for (int beat = 0; beat < 4 * bars; ++beat)
        hits.push_back ({ start + beat * quarter + (beat % 2) * 11.0, 2, 40 + beat });
    const double end = start + 4 * bars * quarter;
    const auto take = ksh::transcribeJam (hits, end, 145.0, 0, 0);
    REQUIRE (take);
    CHECK (take->bpm == Catch::Approx (80.0));
    CHECK (take->steps == 16 * bars);
    CHECK (take->droppedHits == 0);
    for (int beat = 0; beat < 4 * bars; ++beat)
        CHECK (take->pattern[2][static_cast<size_t> (beat * 4)].velocity == 40 + beat);
    CHECK_FALSE (ksh::transcribeJam ({}, end, 120.0, 0, 0));
    const auto sparse = ksh::transcribeJam ({ hits.front() }, start + 2000.0, 120.0, 0, 0);
    REQUIRE (sparse);
    CHECK (sparse->steps == 16);
    CHECK (sparse->pattern[2][0].enabled);
}

TEST_CASE ("Jam prefers two or four bars when a three-bar grid also fits", "[plugin][jam]")
{
    const int bars = GENERATE (2, 4);
    const double bpm = bars == 2 ? 85.0 : 160.0;
    const double quarter = 60000.0 / bpm;
    constexpr double start = 1000.0;
    std::vector<ksh::JamHit> hits;
    for (int beat = 0; beat < 4 * bars; ++beat)
        hits.push_back ({ start + beat * quarter, beat % 2, 100 });

    const auto take = ksh::transcribeJam (hits, start + 4 * bars * quarter, 120.0, 0, 0, 100);
    REQUIRE (take);
    CHECK (take->bpm == Catch::Approx (bpm));
    REQUIRE (take->steps == 16 * bars);
    CHECK (take->droppedHits == 0);
    for (int beat = 0; beat < 4 * bars; ++beat)
        CHECK (take->pattern[static_cast<size_t> (beat % 2)][static_cast<size_t> (beat * 4)].enabled);
}

TEST_CASE ("Jam transcribes fast hits as a roll with individual velocities", "[plugin][jam]")
{
    const auto take = ksh::transcribeJam ({ { 1000.0, 0, 100 }, { 1000.0 + 125.0 / 3.0, 0, 78 },
                                          { 1000.0 + 250.0 / 3.0, 0, 108 }, { 1500.0, 1, 80 } },
                                        3000.0, 120.0, 0, 0);
    REQUIRE (take);
    CHECK (take->bpm == Catch::Approx (120.0));
    const auto& roll = take->pattern[0][0];
    CHECK (roll.roll == 3);
    CHECK (roll.nudge == 0);
    CHECK (roll.splitVelocity (0) == 100);
    CHECK (roll.splitVelocity (1) == 78);
    CHECK (roll.splitVelocity (2) == 108);
}

TEST_CASE ("Jam uses the recording quantization strength for timing offsets", "[plugin][jam]")
{
    const int strength = GENERATE (0, 50, 100);
    const auto take = ksh::transcribeJam ({ { 1000.0, 0, 100 }, { 1515.0, 2, 77 }, { 2000.0, 0, 82 } },
                                         3000.0, 120.0, 0, 0, strength);
    REQUIRE (take);
    CHECK (take->bpm == Catch::Approx (120.0));
    CHECK_FALSE (take->swing);
    CHECK (take->pattern[2][4].velocity == 77);
    CHECK (take->pattern[2][4].nudge == 12 * (100 - strength) / 100);
}

TEST_CASE ("Jam captures pad and MIDI hits while stopped and replaces one pattern with undo", "[plugin][jam]")
{
    const int playedSwing = GENERATE (0, 60, 100);
    const bool swung = playedSwing > 0;
    const int expectedSwing = swung ? 20 : 0;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    REQUIRE (processor.dispatchUiEngineCommand ("static_source", { 2 }));
    REQUIRE (processor.dispatchUiEngineCommand ("source_rate", { 2, "8n" }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 4, 1, 77 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 2, 1, 8, 1, 96 }));
    processor.setStandaloneTempoBpm (105.0);
    processor.setStandaloneTransportPlaying (true);
    processor.setPatternRecordingEnabled (true, 1);
    processor.setPatternRecordingQuantizeStrength (100);
    processor.setTimestampedStandaloneMidiInput (true);
    processor.prepareToPlay (48000.0, 128);

    auto controller = std::make_unique<ksh::ui::EditorController> (processor);
    const auto before = std::make_unique<ksh::DocumentState> (processor.editorDocument());
    const auto epoch = processor.getUiBridge().getDocumentEpoch();
    REQUIRE (processor.startJamCapture());
    CHECK_FALSE (processor.isStandaloneTransportPlaying());
    CHECK_FALSE (processor.isPatternRecordingEnabled());
    CHECK_FALSE (processor.pollJamCapture());
    processor.setStandaloneTransportPlaying (true);
    CHECK_FALSE (processor.isStandaloneTransportPlaying());
    REQUIRE (processor.triggerRowFromUi (0, 113));
    const double t = PluginProcessorTestPeer::jamFirstHitTime (processor);
    const auto channels = processor.engineStateSnapshot().channels;
    const auto hit = [&] (int row, double time, int velocity)
    {
        auto midi = juce::MidiMessage::noteOn (1, channels[static_cast<size_t> (row)].note, static_cast<juce::uint8> (velocity));
        midi.setTimeStamp ((t + time) / 1000.0);
        processor.captureStandaloneMidiInput (midi);
    };
    for (int beat : { 1, 2, 3 })
        hit (2, beat * 500.0 + (swung ? 125.0 + 62.5 * playedSwing / 100.0 : 12.5), 60 + beat);
    if (swung)
        hit (1, 1000.0, 88);
    hit (2, 2005.0, 90);
    REQUIRE (processor.pollJamCapture());
    const auto take = processor.finishJamCapture (t + 2000.0);
    REQUIRE (take);
    if (swung)
    {
        REQUIRE (take->swing);
        CHECK (take->swing->amount == expectedSwing);
        CHECK (take->swing->subdivision == 0);
    }
    else
        CHECK_FALSE (take->swing);
    REQUIRE (controller->applyJamTake (*take, 1, epoch));
    CHECK (processor.getStandaloneTempoBpm() == Catch::Approx (120.0).margin (1.0));
    CHECK (controller->state.playback.sourceSettings[1].rate == "16n");
    CHECK (controller->cell ({ 0, 0 }).velocity == 113);
    CHECK_FALSE (controller->cell ({ 0, 7 }).enabled);
    CHECK (controller->cell ({ 2, swung ? 5 : 4 }).velocity == 61);
    CHECK (controller->cell ({ 2, swung ? 5 : 4 }).nudge == 0);
    CHECK (controller->cell ({ 2, swung ? 13 : 12 }).velocity == 63);
    CHECK (controller->state.playback.swing == expectedSwing);
    CHECK (controller->state.playback.sources[0][0][3] == before->sources[0][0][3]);
    REQUIRE (controller->canUndo());
    controller->undo();
    CHECK (controller->state.playback.swing == 0);
    CHECK (processor.getStandaloneTempoBpm() == Catch::Approx (105.0));
    CHECK (controller->state.playback.sourceSettings[1].rate == "8n");
    CHECK (controller->cell ({ 0, 7 }) == before->sources[1][0][7]);
    CHECK_FALSE (controller->canUndo());
    controller->redo();
    CHECK (controller->state.playback.swing == expectedSwing);
    CHECK (controller->cell ({ 0, 0 }).velocity == 113);
    CHECK (processor.getStandaloneTempoBpm() == Catch::Approx (120.0).margin (1.0));
}

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

    void setTimeSignature (int numerator, int denominator)
    {
        position.setTimeSignature (TimeSignature { numerator, denominator });
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

TEST_CASE ("Host state restore filters old playback feedback while audio and UI run", "[plugin][bridge][state-restore]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 100, 1 }));
    juce::MemoryBlock state;
    plugin.getStateInformation (state);

    auto play = [&] (double ppq)
    {
        playHead.setPpqPosition (ppq);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        plugin.processBlock (buffer, midi);
    };
    int hits = 0, visits = 0;
    plugin.getUiBridge().onNoteHit = [&] (const ksh::NativeHit&) { ++hits; };
    plugin.getUiBridge().onCycleVisit = [&] (const ksh::NativeCycleVisit&) { ++visits; };
    play (0.0);
    std::thread firstRestore ([&] { plugin.setStateInformation (state.getData(), static_cast<int> (state.getSize())); });
    firstRestore.join();
    CHECK (PluginProcessorTestPeer::pendingFeedbackCounts (plugin) == std::array<size_t, 2> { 1, 1 });
    CHECK (plugin.emitPendingNoteHitsForUi() == 0);
    CHECK (hits == 0);
    CHECK (visits == 0);
    play (0.0);
    plugin.emitPendingNoteHitsForUi();
    REQUIRE (hits == 1);
    REQUIRE (visits == 1);

    constexpr int repeats = 20;
    const auto epoch = plugin.getUiBridge().getDocumentEpoch();
    juce::WaitableEvent audioStart, hostStart, audioDone, hostDone;
    std::thread audio ([&]
    {
        for (int i = 1; i <= repeats; ++i)
        {
            audioStart.wait();
            play (i * 512.0 / 24000.0);
            audioDone.signal();
        }
    });
    std::thread host ([&]
    {
        for (int i = 0; i < repeats; ++i)
        {
            hostStart.wait();
            plugin.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
            hostDone.signal();
        }
    });
    for (int i = 0; i < repeats; ++i)
    {
        audioStart.signal();
        hostStart.signal();
        PluginProcessorTestPeer::tick (plugin);
        audioDone.wait();
        hostDone.wait();
    }
    audio.join();
    host.join();
    CHECK (plugin.getUiBridge().getDocumentEpoch() == epoch + repeats);
    plugin.emitPendingNoteHitsForUi();
    play (0.0);
    CHECK (plugin.emitPendingNoteHitsForUi() == 2);
    plugin.getUiBridge().onNoteHit = {};
    plugin.getUiBridge().onCycleVisit = {};
    plugin.setPlayHead (nullptr);
}

TEST_CASE ("A host parameter callback can read the just-edited plugin state", "[plugin][bridge][state-restore]")
{
    PluginProcessor plugin;
    struct HostListener final : juce::AudioProcessorParameter::Listener
    {
        explicit HostListener (PluginProcessor& owner) : processor (owner) {}

        void parameterValueChanged (int, float) override { processor.getStateInformation (saved); }
        void parameterGestureChanged (int, bool) override {}

        PluginProcessor& processor;
        juce::MemoryBlock saved;
    };
    HostListener host (plugin);
    auto* swing = plugin.getValueTreeState().getParameter ("swing");
    REQUIRE (swing != nullptr);
    swing->addListener (&host);
    const juce::ScopeGuard remove { [&] { swing->removeListener (&host); } };
    REQUIRE (plugin.dispatchUiEngineCommand ("swing", { 57 }));
    REQUIRE (host.saved.getSize() > 0);
    const auto* bytes = static_cast<const char*> (host.saved.getData());
    CHECK (nlohmann::json::parse (std::string_view (bytes, host.saved.getSize()))["swing"] == 57);
}

TEST_CASE ("State restore and New or Open notify the host of changed macros without edit gestures", "[plugin][bridge][state-restore]")
{
    PluginProcessor plugin;
    PluginProcessor modified;
    REQUIRE (modified.dispatchUiEngineCommand ("rate", { "8n" }));
    REQUIRE (modified.dispatchUiEngineCommand ("swing", { 57 }));
    REQUIRE (modified.dispatchUiEngineCommand ("velocity_humanize", { 22 }));
    REQUIRE (modified.dispatchUiEngineCommand ("timing_humanize", { 33 }));
    REQUIRE (modified.dispatchUiEngineCommand ("device_active", { 0 }));
    const auto project = modified.enginePersistenceState().dump();

    struct HostListener final : juce::AudioProcessorParameter::Listener
    {
        void parameterValueChanged (int index, float value) override { changes.emplace_back (index, value); }
        void parameterGestureChanged (int, bool) override { ++gestures; }
        std::vector<std::pair<int, float>> changes;
        int gestures = 0;
    } host;
    std::vector<juce::RangedAudioParameter*> observed;
    const juce::ScopeGuard remove { [&] { for (auto* parameter : observed) parameter->removeListener (&host); } };
    for (const auto* id : { "rate", "swing", "velocity_humanize", "timing_humanize", "device_active" })
    {
        auto* parameter = plugin.getValueTreeState().getParameter (id);
        REQUIRE (parameter != nullptr);
        observed.push_back (parameter);
        parameter->addListener (&host);
    }
    const auto checkChanges = [&host, &observed]
    {
        REQUIRE (host.changes.size() == observed.size());
        for (auto* parameter : observed)
            CHECK (std::ranges::find (host.changes, std::pair { parameter->getParameterIndex(), parameter->getValue() }) != host.changes.end());
        CHECK (host.gestures == 0);
        host.changes.clear();
    };

    SECTION ("Host state restore") { plugin.setStateInformation (project.data(), static_cast<int> (project.size())); }
    SECTION ("Open project") { REQUIRE (plugin.loadStateFromData (project.data(), static_cast<int> (project.size()))); }
    checkChanges();
    plugin.resetProject();
    checkChanges();
    plugin.resetProject();
    CHECK (host.changes.empty());
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

TEST_CASE ("Offline automation changes rendered MIDI without message-thread ticks", "[plugin][bridge][offline-automation]")
{
    const auto [id, automatedValue] = GENERATE (std::pair { "rate", 6.0f }, std::pair { "swing", 65.0f },
                                                std::pair { "timing_humanize", 100.0f }, std::pair { "velocity_humanize", 100.0f },
                                                std::pair { "device_active", 0.0f });
    CAPTURE (id);
    using Event = std::tuple<int, bool, int>;
    constexpr int blockSize = 600;
    constexpr int automationStart = 5 * blockSize;
    constexpr int automationEnd = 25 * blockSize;
    const auto bounce = [&] (bool automate, bool tick)
    {
        PluginProcessor processor;
        FixedPlayHead playHead;
        processor.setPlayHead (&playHead);
        processor.setNonRealtime (true);
        for (int step = 1; step <= 16; ++step)
            REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, step, 1, 72 }));
        processor.prepareToPlay (48000.0, blockSize);
        auto* parameter = processor.getValueTreeState().getParameter (id);
        REQUIRE (parameter != nullptr);
        const float initialValue = parameter->getValue();

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<Event> rendered;
        for (int sample = 0; sample < 30000; sample += blockSize)
        {
            if (automate && sample == automationStart)
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (automatedValue));
            if (automate && sample == automationEnd)
                parameter->setValueNotifyingHost (initialValue);
            if (tick)
                PluginProcessorTestPeer::tick (processor);
            playHead.setPpqPosition (sample / 24000.0);
            midi.clear();
            processor.processBlock (buffer, midi);
            for (const auto event : midi)
            {
                const auto message = event.getMessage();
                if (message.isNoteOnOrOff())
                    rendered.emplace_back (sample + event.samplePosition, message.isNoteOn(), message.getVelocity());
            }
        }
        return rendered;
    };

    const auto automated = bounce (true, false);
    CHECK (automated == bounce (true, false));
    CHECK (automated == bounce (true, true));
    CHECK (automated != bounce (false, false));
    if (std::string_view (id) == "device_active")
    {
        CHECK (std::find (automated.begin(), automated.end(), Event { automationStart, false, 0 }) != automated.end());
        CHECK (std::none_of (automated.begin(), automated.end(), [] (const auto& event)
        {
            const auto [sample, on, velocity] = event;
            return on && sample >= automationStart && sample < automationEnd;
        }));
        CHECK (std::any_of (automated.begin(), automated.end(), [] (const auto& event)
        {
            return std::get<1> (event) && std::get<0> (event) >= automationEnd;
        }));
    }
}

TEST_CASE ("Pattern automation plays a last-beat fill and follows host position on seek", "[plugin][bridge][offline-automation]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 6000);
    plugin.setNonRealtime (true);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 12, 1, 88 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 13, 1, 88 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 2, 1, 13, 1, 99 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 2, 1, 14, 1, 101 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("source_rate", { 16, "8n" }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 16, 1, 3, 1, 115 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 16, 1, 6, 1, 116 }));

    auto* pattern = plugin.getValueTreeState().getParameter ("pattern");
    REQUIRE (pattern != nullptr);
    REQUIRE (pattern->isDiscrete());
    CHECK (pattern->getText (pattern->getDefaultValue(), 16) == "Manual");
    CHECK (pattern->getText (pattern->convertTo0to1 (17.0f), 16) == "M");
    juce::AudioBuffer<float> buffer (2, 6000);
    juce::MidiBuffer midi;
    const auto render = [&] (double ppq, int slot)
    {
        pattern->setValueNotifyingHost (pattern->convertTo0to1 (static_cast<float> (slot)));
        playHead.setPpqPosition (ppq);
        midi.clear();
        plugin.processBlock (buffer, midi);
    };

    render (2.75, 0);
    CHECK (containsNoteOnWithVelocity (midi, 36, 88));
    render (3.0, 2);
    CHECK (containsNoteOnWithVelocity (midi, 36, 99));
    CHECK (countNoteOns (midi) == 1);
    render (3.25, 17);
    CHECK (countNoteOns (midi) == 0);
    render (9.0, 16);
    CHECK (containsNoteOnWithVelocity (midi, 36, 115));
    auto* rate = plugin.getValueTreeState().getParameter ("rate");
    REQUIRE (rate != nullptr);
    rate->setValueNotifyingHost (rate->getValueForText ("16n"));
    render (9.25, 16);
    CHECK (containsNoteOnWithVelocity (midi, 36, 116));
    CHECK (plugin.engineStateSnapshot().staticSource == 0);
    plugin.setPlayHead (nullptr);
}

TEST_CASE ("Host row mutes gate notes and CC without changing saved row mutes", "[plugin][bridge][offline-automation]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 6000);
    REQUIRE (plugin.dispatchUiEngineCommand ("channel_cc", { 3, 1, 74, 1 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("source_channel_mute", { 1, 2, 1 }));
    std::vector<PluginProcessor::CellEdit> edits;
    for (int step = 0; step < 3; ++step)
    {
        edits.push_back ({ 0, 0, step, {}, { .enabled = true, .velocity = 90 } });
        edits.push_back ({ 0, 1, step, {}, { .enabled = true, .velocity = 90 } });
        edits.push_back ({ 0, 2, step, {}, { .enabled = true, .ccValue = 30 + step * 40, .ccGlide = 1 } });
    }
    REQUIRE (plugin.applyNativeCellEdits (edits, plugin.getUiBridge().getDocumentEpoch()));
    auto* noteMute = plugin.getValueTreeState().getParameter ("row_1_mute");
    auto* ccMute = plugin.getValueTreeState().getParameter ("row_3_mute");
    REQUIRE (noteMute != nullptr);
    REQUIRE (ccMute != nullptr);
    CHECK (noteMute->isBoolean());
    juce::AudioBuffer<float> buffer (2, 6000);
    juce::MidiBuffer midi;
    const auto render = [&] (double ppq, float muted)
    {
        noteMute->setValueNotifyingHost (muted);
        ccMute->setValueNotifyingHost (muted);
        playHead.setPpqPosition (ppq);
        midi.clear();
        plugin.processBlock (buffer, midi);
        return std::any_of (midi.begin(), midi.end(), [] (auto event) { return event.getMessage().isController(); });
    };
    CHECK (render (0.0, 0.0f));
    CHECK (containsNoteOn (midi, 36));
    CHECK_FALSE (containsNoteOn (midi, 38));
    CHECK_FALSE (render (0.25, 1.0f));
    CHECK (countNoteOns (midi) == 0);
    CHECK (render (0.5, 0.0f));
    CHECK (containsNoteOn (midi, 36));
    CHECK_FALSE (containsNoteOn (midi, 38));
    const auto mutes = plugin.engineStateSnapshot().sourceChannelMutes[0];
    CHECK_FALSE (mutes[0]);
    CHECK (mutes[1]);
    CHECK_FALSE (mutes[2]);
    plugin.setPlayHead (nullptr);
}

TEST_CASE ("Dense CC glides at a fast tempo fit the preallocated MIDI output buffer", "[plugin][bridge][cc][realtime]")
{
    constexpr int rows = ksh::Constants::maxChannels;
    constexpr int blockSize = 8192;
    constexpr double sampleRate = 48000.0, bpm = 300.0;
    PluginProcessor plugin;
    FixedPlayHead playHead;
    playHead.setBpm (bpm);
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (sampleRate, blockSize);
    REQUIRE (plugin.dispatchUiEngineCommand ("rate", { "16n" }));
    std::vector<PluginProcessor::CellEdit> edits;
    for (int row = 0; row < rows; ++row)
    {
        REQUIRE (plugin.dispatchUiEngineCommand ("channel_cc", { row + 1, 1, 74, row + 1 }));
        for (int step = 0; step < 16; ++step)
            edits.push_back ({ 0, row, step, {}, { .enabled = true, .ccValue = step % 2 == 0 ? 0 : 127, .ccGlide = 1 } });
    }
    REQUIRE (plugin.applyNativeCellEdits (edits, plugin.getUiBridge().getDocumentEpoch()));

    juce::AudioBuffer<float> audio (2, blockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 4; ++block)
    {
        playHead.setPpqPosition (block * blockSize * bpm / (60.0 * sampleRate));
        midi.clear();
        plugin.processBlock (audio, midi);
        CHECK (PluginProcessorTestPeer::usesReservedMidiStorage (plugin, midi));
        CHECK (midi.getNumEvents() >= 163 * rows);
        CHECK (midi.getNumEvents() <= 164 * rows);
    }
    plugin.setPlayHead (nullptr);
}

TEST_CASE ("Pattern buttons switch immediately at the current transport phase", "[plugin][bridge]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    playHead.setTimeSignature (3, 4);
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 6000);
    REQUIRE (plugin.dispatchUiEngineCommand ("source_steps", { 2, 8 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 11, 1, 80 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 12, 1, 80 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 2, 1, 4, 1, 110 }));
    juce::AudioBuffer<float> buffer (2, 6000);
    juce::MidiBuffer midi;
    playHead.setPpqPosition (2.375);
    plugin.processBlock (buffer, midi);
    REQUIRE (containsNoteOnWithVelocity (midi, 36, 80));

    auto editor = std::make_unique<ksh::ui::EditorController> (plugin);
    editor->selectSource (1);
    CHECK (editor->source() == 1);
    CHECK (plugin.engineStateSnapshot().staticSource == 1);

    playHead.setPpqPosition (2.625);
    midi.clear();
    plugin.processBlock (buffer, midi);
    CHECK (containsNoteOnWithVelocity (midi, 36, 110));
    REQUIRE (countNoteOns (midi) == 1);
    for (const auto event : midi)
        if (event.getMessage().isNoteOn())
            CHECK (event.samplePosition == 3000);
    plugin.setPlayHead (nullptr);
}

TEST_CASE ("Restored pattern rate supersedes older host automation without a UI tick", "[plugin][bridge][offline-automation]")
{
    PluginProcessor processor;
    FixedPlayHead playHead;
    processor.setPlayHead (&playHead);
    REQUIRE (processor.dispatchUiEngineCommand ("rate", { "8n" }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 2, 1, 90 }));
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    processor.prepareToPlay (48000.0, 512);
    auto* rate = processor.getValueTreeState().getParameter ("rate");
    REQUIRE (rate != nullptr);
    rate->setValueNotifyingHost (rate->convertTo0to1 (4.0f)); // 16ths
    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    playHead.setPpqPosition (0.25);
    processor.processBlock (buffer, midi);
    CHECK (countNoteOns (midi) == 0);

    rate->setValueNotifyingHost (rate->convertTo0to1 (4.0f)); // The same automation value is a new host event.
    midi.clear();
    processor.processBlock (buffer, midi);
    CHECK (countNoteOns (midi) == 1);
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

TEST_CASE ("Buffer restarts deliver pending note-offs before any new attacks", "[plugin][bridge][realtime]")
{
    const bool bypass = GENERATE (false, true);
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 256);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));
    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    plugin.processBlock (audio, midi);
    REQUIRE (containsNoteOn (midi, 36));
    REQUIRE_FALSE (containsNoteOff (midi, 36));

    SECTION ("The host releases resources first") { plugin.releaseResources(); }
    SECTION ("The host prepares again directly") {}
    plugin.prepareToPlay (48000.0, 512);
    audio.setSize (2, 512);
    midi.clear();
    if (bypass)
        plugin.processBlockBypassed (audio, midi);
    else
        plugin.processBlock (audio, midi);
    REQUIRE_FALSE (midi.isEmpty());
    CHECK ((*midi.begin()).getMessage().isNoteOff());
    CHECK ((*midi.begin()).getMessage().getNoteNumber() == 36);
    CHECK ((*midi.begin()).samplePosition == 0);
    CHECK (countNoteOns (midi) == (bypass ? 0 : 1));
    midi.clear();
    playHead.setPpqPosition (512.0 / 24000.0);
    plugin.processBlock (audio, midi);
    CHECK_FALSE (containsNoteOff (midi, 36));
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

    const int target = GENERATE (0, 1, 2);
    REQUIRE (plugin.dispatchUiEngineCommand ("swing_subdivision", { target }));
    REQUIRE (plugin.engineStateSnapshot().swingSubdivisionIndex == std::min (target, 1));
}

TEST_CASE ("host macro parameter changes update engine", "[plugin][bridge]")
{
    PluginProcessor plugin;
    auto* swing = plugin.getValueTreeState().getParameter ("swing");

    REQUIRE (swing != nullptr);

    swing->setValueNotifyingHost (swing->convertTo0to1 (41.0f));
    PluginProcessorTestPeer::tick (plugin);

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
    const int steps = GENERATE (16, 64);
    const double loopBeats = steps * 0.25;
    REQUIRE (plugin.dispatchUiEngineCommand ("source_steps", { 1, steps }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 72, 63, 4, 5, 3 }));
    ksh::ui::EditorController controller (plugin);
    const auto original = snapshotEditorDocument (plugin);
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
    midiHit (loopBeats - 0.25, 36, 92);
    (void) controller.poll();
    midiHit (loopBeats + 0.25, 36, 85); // Second traversal of the same loop.
    (void) controller.poll();
    midiHit (loopBeats * 2.0, 36, 110); // A later overwrite belongs to the same pass.
    (void) controller.poll();

    controller.begin ("Manual edit during recording");
    auto manual = controller.cell ({ 1, 5 });
    manual.enabled = true;
    manual.probability = 42;
    controller.editCell ({ 1, 5 }, manual);
    controller.commit();
    midiHit (loopBeats * 2.0 + 0.5, 36, 80); // Still queued when Record is clicked off.
    controller.command ("pattern_record_enabled", { 0, 1 });
    const auto recorded = snapshotEditorDocument (plugin);
    CHECK (recorded->sources[0][0][0].velocity == 110);
    CHECK (recorded->sources[0][0][1].enabled);
    CHECK (recorded->sources[0][0][2].enabled);
    CHECK (recorded->sources[0][0][static_cast<size_t> (steps - 1)].velocity == 92);
    CHECK (recorded->sources[0][2][0].enabled);
    CHECK_FALSE (plugin.isPatternRecordingEnabled());

    controller.undo();
    const auto expected = std::make_unique<ksh::DocumentState> (*original);
    expected->sources[0][1][5] = manual;
    CHECK (*snapshotEditorDocument (plugin) == *expected);
    controller.redo();
    CHECK (*snapshotEditorDocument (plugin) == *recorded);
    controller.undo();
    controller.undo();
    CHECK (*snapshotEditorDocument (plugin) == *original);
    CHECK_FALSE (controller.canUndo());

    // An in-flight audio block cannot resurrect a stopped take after undo.
    REQUIRE (PluginProcessorTestPeer::enqueueLateRecord (plugin, session));
    PluginProcessorTestPeer::tick (plugin);
    CHECK (*snapshotEditorDocument (plugin) == *original);
}

TEST_CASE ("Recording undo separates passes and supports undo while still armed", "[plugin][record][record-undo]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);
    ksh::ui::EditorController controller (plugin);
    const auto original = snapshotEditorDocument (plugin);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);
    controller.command ("pattern_record_enabled", { 1, 1 });
    controller.command ("pattern_record_enabled", { 0, 1 });
    CHECK_FALSE (controller.canUndo());
    controller.command ("pattern_record_enabled", { 1, 1 });
    REQUIRE (plugin.triggerRowFromUi (0, 90));
    controller.command ("pattern_record_enabled", { 0, 1 });
    const auto firstPass = snapshotEditorDocument (plugin);
    REQUIRE (*firstPass != *original);

    controller.command ("pattern_record_enabled", { 1, 1 });
    REQUIRE (plugin.triggerRowFromUi (1, 80));
    const bool stopBeforeUndo = GENERATE (false, true);
    if (stopBeforeUndo)
        controller.command ("pattern_record_enabled", { 0, 1 });
    controller.undo();
    CHECK (*snapshotEditorDocument (plugin) == *firstPass);
    CHECK (plugin.isPatternRecordingEnabled() == !stopBeforeUndo);
    if (!stopBeforeUndo)
    {
        REQUIRE (plugin.triggerRowFromUi (2, 70));
        controller.command ("pattern_record_enabled", { 0, 1 });
        controller.undo();
        CHECK (*snapshotEditorDocument (plugin) == *firstPass);
    }
    controller.undo();
    CHECK (*snapshotEditorDocument (plugin) == *original);
}

TEST_CASE ("Recording undo preserves intervening and later edits to the same cell", "[plugin][record][record-undo]")
{
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 72, 63, 4, 5, 3 }));
    ksh::ui::EditorController controller (plugin);
    auto expected = snapshotEditorDocument (plugin);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);
    controller.command ("pattern_record_enabled", { 1, 1 });
    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_row", { 1, 90 }));
    if (GENERATE (false, true))
        (void) controller.poll();
    const auto previous = snapshotEditorDocument (plugin)->sources[0][0][0];
    auto external = previous;
    external.velocity = 55;
    external.probability = 33;
    REQUIRE (plugin.applyNativeCellEdits ({ { 0, 0, 0, previous, external } }, controller.state.documentEpoch));
    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_row", { 1, 110 }));
    controller.command ("pattern_record_enabled", { 0, 1 });
    const auto recorded = snapshotEditorDocument (plugin);
    expected->sources[0][0][0].velocity = 55;
    expected->sources[0][0][0].probability = 33;
    controller.undo();
    CHECK (*snapshotEditorDocument (plugin) == *expected);
    controller.redo();
    CHECK (*snapshotEditorDocument (plugin) == *recorded);

    auto later = recorded->sources[0][0][0];
    later.probability = 41;
    REQUIRE (plugin.applyNativeCellEdits ({ { 0, 0, 0, recorded->sources[0][0][0], later } }, controller.state.documentEpoch));
    controller.undo();
    expected->sources[0][0][0].probability = 41;
    CHECK (*snapshotEditorDocument (plugin) == *expected);
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
    playHead.setBpm (20.0);
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (44100.0, 512);

    REQUIRE (plugin.dispatchUiEngineCommand ("pattern_record_enabled", { 1, 2 }));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    plugin.processBlock (buffer, midi);

    REQUIRE (plugin.triggerRowFromUi (2, 91));
    REQUIRE (PluginProcessorTestPeer::pendingRecordCounts (plugin) == std::array<size_t, 2> { 0, 1 });

    midi.clear();
    plugin.processBlock (buffer, midi);
    REQUIRE (containsNoteOnWithVelocity (midi, 38, 91));

    PluginProcessorTestPeer::tick (plugin);

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

    REQUIRE (PluginProcessorTestPeer::messageThreadWorkPending (plugin));
    PluginProcessorTestPeer::tick (plugin);
    REQUIRE (plugin.engineStateSnapshot().staticSource == 0);

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 7, static_cast<juce::uint8> (100)), 0);

    plugin.processBlock (buffer, midi);

    REQUIRE_FALSE (containsNoteOn (midi, 7));

    PluginProcessorTestPeer::tick (plugin);
    REQUIRE (plugin.engineStateSnapshot().staticSource == 7);

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 15, static_cast<juce::uint8> (100)), 0);

    plugin.processBlock (buffer, midi);

    REQUIRE_FALSE (containsNoteOn (midi, 15));

    PluginProcessorTestPeer::tick (plugin);
    REQUIRE (plugin.engineStateSnapshot().staticSource == 15);

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 16, static_cast<juce::uint8> (100)), 0);

    plugin.processBlock (buffer, midi);

    REQUIRE_FALSE (containsNoteOn (midi, 16));

    PluginProcessorTestPeer::tick (plugin);
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

    PluginProcessorTestPeer::tick (plugin);
    REQUIRE (plugin.engineStateSnapshot().staticSource == 0);
}

TEST_CASE ("An explicit pattern choice supersedes MIDI selections the audio thread has not acknowledged", "[plugin][bridge][midi-input]")
{
    const bool synchronized = GENERATE (false, true);
    const int selected = GENERATE (1, 3);
    PluginProcessor plugin;
    FixedPlayHead playHead;
    plugin.setPlayHead (&playHead);
    plugin.prepareToPlay (48000.0, 512);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 2, 1, 71, 100, 1 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 2, 1, 1, 1, 82, 100, 1 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 2, 1, 2, 1, 82, 100, 1 }));
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 3, 1, 2, 1, 93, 100, 1 }));
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 1, juce::uint8 { 100 }), 0);
    plugin.processBlock (buffer, midi);
    REQUIRE (containsNoteOn (midi, 36));
    if (synchronized)
        PluginProcessorTestPeer::tick (plugin);

    REQUIRE (plugin.dispatchUiEngineCommand ("static_source", { selected }));
    playHead.setPpqPosition (0.25);
    midi.clear();
    plugin.processBlock (buffer, midi);
    REQUIRE (countNoteOns (midi) == 1);
    for (const auto event : midi)
        if (event.getMessage().isNoteOn())
            CHECK (event.getMessage().getVelocity() == (selected == 1 ? 71 : 93));
    PluginProcessorTestPeer::tick (plugin);
    CHECK (plugin.engineStateSnapshot().staticSource == selected - 1);
    plugin.setPlayHead (nullptr);
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

    PluginProcessorTestPeer::tick (plugin);
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

    PluginProcessorTestPeer::tick (plugin);

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
    document.settings.swingSubdivisionIndex = 0; // 1x swings alternating grid steps.
    document.settings.sourceSettings[0].playbackModes[0] = mode;
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
    const auto before = snapshotEditorDocument (plugin);
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
    const auto recorded = snapshotEditorDocument (plugin);
    CHECK (recorded->sources[0][0][1].nudge == 20);
    CHECK (recorded->sources[0][0][1].velocity == 95);
    controller.undo();
    CHECK (*snapshotEditorDocument (plugin) == *before);
    controller.redo();
    CHECK (*snapshotEditorDocument (plugin) == *recorded);
    const auto saved = plugin.enginePersistenceState().dump();
    PluginProcessor restored;
    REQUIRE (restored.loadStateFromData (saved.data(), static_cast<int> (saved.size())));
    CHECK (*snapshotEditorDocument (restored) == *recorded);
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

TEST_CASE ("Standalone metronome clicks at quarter notes across audio callbacks", "[plugin][metronome]")
{
    const int blockSize = GENERATE (128, 511);
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    CHECK_FALSE (processor.isMidiEffect());
    processor.setStandaloneTempoBpm (120.0);
    processor.prepareToPlay (48000.0, blockSize);
    processor.setStandaloneMetronomeEnabled (true);
    juce::AudioBuffer<float> audio (2, blockSize);
    juce::MidiBuffer midi;
    processor.processBlock (audio, midi);
    CHECK (audio.hasBeenCleared());
    CHECK (midi.isEmpty());

    processor.setStandaloneTransportPlaying (true);
    std::vector<int> clicks;
    bool wasAudible = false;
    for (int offset = 0; offset < 96500; offset += blockSize)
    {
        processor.processBlock (audio, midi);
        CHECK (std::equal (audio.getReadPointer (0), audio.getReadPointer (0) + blockSize, audio.getReadPointer (1)));
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const float left = audio.getSample (0, sample);
            const bool audible = std::abs (left) > 0.001f;
            if (audible && !wasAudible && (clicks.empty() || offset + sample - clicks.back() > 1000))
                clicks.push_back (offset + sample);
            wasAudible = audible;
        }
        CHECK (midi.isEmpty());
    }
    CHECK (clicks == std::vector<int> { 1, 24001, 48001, 72001, 96001 });
    CHECK (processor.getStandaloneMidiActivity() == 0);
    REQUIRE (processor.auditionRowFromUi (0));
    processor.setStandaloneMetronomeEnabled (false);
    processor.processBlock (audio, midi);
    CHECK (audio.hasBeenCleared());
    CHECK_FALSE (midi.isEmpty());
    CHECK (processor.getStandaloneMidiActivity() == 1);
}

TEST_CASE ("Standalone metronome settings change the click independently of transport", "[plugin][metronome]")
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    processor.setStandaloneTempoBpm (120.0);
    processor.prepareToPlay (48000.0, 256);
    processor.setStandaloneMetronomeEnabled (true);
    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    const auto renderClick = [&]
    {
        processor.setStandaloneTransportPlaying (true);
        processor.processBlock (audio, midi);
        processor.setStandaloneTransportPlaying (false);
        return audio.getSample (0, 10);
    };
    const auto classic = renderClick();
    REQUIRE (std::abs (classic) > 0.01f);
    processor.setMetronomeVolume (50);
    CHECK (renderClick() == Catch::Approx (classic * 0.5f));
    processor.setMetronomeVolume (0);
    CHECK (renderClick() == Catch::Approx (0.0f));
    processor.setMetronomeVolume (100);
    processor.setMetronomePitch (1);
    CHECK (renderClick() != Catch::Approx (classic));
    processor.setMetronomePitch (0);
    processor.setMetronomeSound (PluginProcessor::MetronomeSound::wood);
    const auto wood = renderClick();
    CHECK (wood != Catch::Approx (classic));
    processor.setMetronomeSound (PluginProcessor::MetronomeSound::claves);
    CHECK (renderClick() != Catch::Approx (wood));
    CHECK (processor.isStandaloneMetronomeEnabled());
}

TEST_CASE ("Standalone MIDI discovery excludes its virtual output and preserves external input choices", "[plugin][standalone-midi][realtime]")
{
    const juce::MidiDeviceInfo output { "KSH Output", "ksh-source" };
    const juce::MidiDeviceInfo keyboard { "USB keyboard", "keyboard" };
    const juce::MidiDeviceInfo app { "External controller", "controller" };
    juce::Array<juce::MidiDeviceInfo> previous;
    std::map<juce::String, bool> enabled;
    enabled[output.identifier] = true; // Simulate a restored setting that would feed back.
    const auto reconcile = [&] (const juce::Array<juce::MidiDeviceInfo>& available, bool autoOpen = true)
    {
        ksh::updateStandaloneMidiInputs (available, previous, output, autoOpen,
                                         [&] (const juce::String& id, bool value) { enabled[id] = value; });
    };
    reconcile ({ keyboard, output });
    CHECK (enabled[keyboard.identifier]);
    CHECK_FALSE (enabled[output.identifier]);
    enabled[keyboard.identifier] = false; // The user disables this controller.
    enabled[output.identifier] = true;    // Even a manual enable cannot persist for KSH Output.
    reconcile ({ keyboard, output, app });
    CHECK_FALSE (enabled[keyboard.identifier]);
    CHECK_FALSE (enabled[output.identifier]);
    CHECK (enabled[app.identifier]);
    reconcile ({ output, app });
    CHECK_FALSE (enabled[keyboard.identifier]);
    reconcile ({ keyboard, output, app });
    CHECK (enabled[keyboard.identifier]); // Reconnected hardware follows auto-open policy.
    previous.clear();
    enabled.clear();
    enabled[output.identifier] = true;
    reconcile ({ keyboard, output }, false);
    CHECK_FALSE (enabled[output.identifier]);
    CHECK (enabled.count (keyboard.identifier) == 0); // Desktop/manual input mode stays manual.
}

TEST_CASE ("Standalone MIDI input guard breaks note and controller feedback without filtering external performances", "[plugin][standalone-midi][realtime]")
{
    const juce::MidiDeviceInfo output { "KSH Output", "ksh-source" };
    CHECK (ksh::isStandaloneMidiOutputSource ({ "Renamed source", "ksh-source" }, output));
    CHECK (ksh::isStandaloneMidiOutputSource ({ "KSH Output", "another-ksh-instance" }, output));
    CHECK_FALSE (ksh::isStandaloneMidiOutputSource ({ "Keyboard", "keyboard" }, output));
    CHECK_FALSE (ksh::isStandaloneMidiOutputSource ({ "KSH Output controller", "keyboard" }, output));
    CHECK_FALSE (ksh::isStandaloneMidiOutputSource ({ "Keyboard", "keyboard" }, {}));
    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 64);
    juce::MidiBuffer incoming;
    const auto receive = [&] (const juce::MidiDeviceInfo& source, const juce::MidiMessage& message)
    {
        // The holder applies the same guard before both player forwarding and recording capture.
        if (!ksh::isStandaloneMidiOutputSource (source, output))
            incoming.addEvent (message, 0);
    };
    const juce::MidiDeviceInfo keyboard { "Keyboard", "keyboard" };
    receive (keyboard, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)));
    receive (keyboard, juce::MidiMessage::controllerEvent (1, 7, 96));
    receive (keyboard, juce::MidiMessage::noteOff (1, 60));
    juce::AudioBuffer<float> audio (2, 64);
    plugin.processBlock (audio, incoming);
    REQUIRE (incoming.getNumEvents() == 3);
    for (const auto event : incoming)
        receive (output, event.getMessage());
    // Self-generated events add nothing to the input stream, including note-offs/CC.
    CHECK (incoming.getNumEvents() == 3);
    incoming.clear();
    plugin.processBlock (audio, incoming);
    CHECK (incoming.isEmpty());
}

TEST_CASE ("Standalone MIDI Thru is opt-in while playback and input recording stay active", "[plugin][standalone-midi][realtime]")
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    REQUIRE_FALSE (processor.isStandaloneMidiThruEnabled());
    REQUIRE (processor.dispatchUiEngineCommand ("channel_note", { 2, 38 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 97 }));
    processor.prepareToPlay (48000.0, 64);
    processor.setStandaloneTransportPlaying (true);
    processor.setPatternRecordingEnabled (true, 0);
    juce::AudioBuffer<float> audio (2, 64);
    juce::MidiBuffer midi;

    midi.addEvent (juce::MidiMessage::noteOn (1, 38, static_cast<juce::uint8> (81)), 5);
    midi.addEvent (juce::MidiMessage::midiClock(), 9);
    processor.processBlock (audio, midi);
    REQUIRE (countNoteOns (midi) == 1);
    CHECK (containsNoteOn (midi, 36));
    for (const auto event : midi)
        CHECK_FALSE (event.getMessage().isMidiClock());
    PluginProcessorTestPeer::tick (processor);
    const auto recorded = processor.engineStateSnapshot();
    CHECK (recorded.sources[0][1][0].enabled);
    CHECK (recorded.sources[0][1][0].velocity == 81);

    processor.setStandaloneMidiThru (true);
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (90)), 10);
    midi.addEvent (juce::MidiMessage::midiStart(), 11);
    midi.addEvent (juce::MidiMessage::midiClock(), 12);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 13);
    processor.processBlock (audio, midi);
    REQUIRE (midi.getNumEvents() == 4);
    CHECK (containsNoteOn (midi, 60));

    processor.setStandaloneMidiThru (false);
    midi.clear();
    midi.addEvent (juce::MidiMessage::midiClock(), 0);
    processor.processBlock (audio, midi);
    REQUIRE (midi.getNumEvents() == 3);
    CHECK (containsNoteOff (midi, 60));
    auto event = midi.begin();
    CHECK ((*event).getMessage().isSustainPedalOff());
    std::advance (event, 2);
    CHECK ((*event).getMessage().isMidiStop());

    midi.clear();
    midi.addEvent (juce::MidiMessage::midiClock(), 0);
    processor.processBlockBypassed (audio, midi);
    for (const auto bypassedEvent : midi)
        CHECK_FALSE (bypassedEvent.getMessage().isMidiClock());
}

TEST_CASE ("Network MIDI destination listed by JUCE is not duplicated beside the native route", "[plugin][standalone-midi]")
{
    const juce::MidiDeviceInfo usb { "USB MIDI", "usb" };
    const auto listed = ksh::withoutStandaloneNetworkDestination ({ usb, { "Network Session 1", "1 2" } }, "1 2");
    REQUIRE (listed.size() == 1);
    CHECK (listed[0] == usb);
    const auto missing = ksh::withoutStandaloneNetworkDestination ({ usb }, {});
    REQUIRE (missing.size() == 1);
    CHECK (missing[0] == usb);
}
