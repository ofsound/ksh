#include <PluginProcessor.h>
#include "../standalone/StandaloneMidiClock.h"
#include "../standalone/StandaloneMidiRouting.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

TEST_CASE ("A missing preferred MIDI destination is eligible again when its identifier returns", "[standalone-midi][routing]")
{
    const juce::String preferred = "saved-id";
    juce::Array<juce::MidiDeviceInfo> devices;
    devices.add ({ "Synth", "different-id" });
    CHECK (ksh::availableStandaloneMidiOutput (devices, preferred).isEmpty());
    devices.add ({ "Synth", preferred });
    CHECK (ksh::availableStandaloneMidiOutput (devices, preferred) == preferred);
    devices.removeLast();
    CHECK (ksh::availableStandaloneMidiOutput (devices, preferred).isEmpty());
    devices.add ({ "Renamed synth", preferred });
    CHECK (ksh::availableStandaloneMidiOutput (devices, preferred) == preferred);
}

TEST_CASE ("MIDI route flush includes notes with queued releases", "[standalone-midi][routing]")
{
    ksh::StandaloneMidiHeldNotes notes;
    notes.observe (0x20923c60, 10); // Channel 3, pitch 60, still held.
    notes.observe (0x20914060, 10);
    notes.observe (0x20814000, 21); // Release has yet to reach the synth.
    notes.observe (0x20904560, 10);
    notes.observe (0x20904500, 19); // Velocity zero is a release, already delivered.
    juce::Array<uint32_t> releases;
    notes.forEachHeld (20, [&] (auto word) { releases.add (word); });
    const juce::Array<uint32_t> expected { 0x20814000u, 0x20823c00u };
    CHECK (releases == expected);
    notes.clear();
    releases.clear();
    notes.forEachHeld (20, [&] (auto word) { releases.add (word); });
    CHECK (releases.isEmpty());
}

TEST_CASE ("Link tempo edits persist and project loads leave the session tempo alone", "[standalone-link][persistence]")
{
    struct Clock final : ksh::StandaloneSync
    {
        double lastRequest = 0.0;
        int tempoRequests = 0;
        bool requestPlaying (bool, bool) override { return true; }
        bool requestTempo (double bpm) override { ++tempoRequests; lastRequest = bpm; return true; }
        double sessionTempo() const override { return 122.0; }
        ksh::StandaloneSyncBlock process (juce::Optional<juce::uint64>, double, int, double, bool) override { return {}; }
    } clock;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor plugin;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    plugin.setStandaloneTempoBpm (90.0);
    juce::MemoryBlock otherProject;
    plugin.getStateInformation (otherProject);
    plugin.markDocumentClean();
    plugin.setStandaloneSync (&clock);
    plugin.setStandaloneTempoBpm (137.0);
    CHECK (clock.lastRequest == Catch::Approx (137.0));
    CHECK (plugin.isDocumentModified());
    juce::MemoryBlock saved;
    plugin.getStateInformation (saved);
    REQUIRE (plugin.loadStateFromData (otherProject.getData(), static_cast<int> (otherProject.getSize())));
    CHECK (clock.tempoRequests == 1);
    CHECK (plugin.getStandaloneTempoBpm() == Catch::Approx (122.0));
    CHECK (plugin.editorDocument().settings.tempo == Catch::Approx (90.0));
    plugin.setStandaloneSync (nullptr);
    REQUIRE (plugin.loadStateFromData (saved.getData(), static_cast<int> (saved.getSize())));
    CHECK (plugin.getStandaloneTempoBpm() == Catch::Approx (137.0));
    CHECK_FALSE (plugin.isDocumentModified());
}

TEST_CASE ("Standalone MIDI timestamps follow the audio clock despite callback jitter", "[standalone-midi][timing]")
{
    const double rate = GENERATE (44100.0, 48000.0, 96000.0);
    const int size = GENERATE (64, 128, 256, 512, 1024);
    ksh::StandaloneMidiClock clock;
    clock.prepare (rate, size);
    const auto ns = [rate] (int64_t samples) { return static_cast<juce::uint64> (std::llround (1.0e9 * static_cast<double> (samples) / rate)); };
    const auto lead = juce::jmax<juce::uint64> (2000000, ns (size));
    constexpr juce::uint64 epoch = 123456789000000;
    for (int block = 0; block < 200; ++block)
    {
        const auto hardwareTime = epoch + ns (int64_t (block) * size);
        const auto callbackTime = hardwareTime + static_cast<juce::uint64> ((block * 173) % 1000) * 1000;
        const auto start = clock.beginBlock (hardwareTime, callbackTime, size);
        CHECK (start == hardwareTime + lead);
        CHECK (clock.eventTime (start, size - 1) == hardwareTime + lead + ns (size - 1));
        CHECK (clock.eventTime (start, 0) > callbackTime);
    }
}

TEST_CASE ("Standalone MIDI fallback clock advances by samples and resets after interruptions", "[standalone-midi][timing]")
{
    ksh::StandaloneMidiClock clock;
    clock.prepare (48000, 480);
    CHECK (clock.beginBlock (juce::nullopt, 1000000000, 480) == 1010000000);
    CHECK (clock.beginBlock (juce::nullopt, 1010700000, 480) == 1020000000);
    CHECK (clock.beginBlock (juce::uint64 (0), 1020200000, 240) == 1030000000);
    CHECK (clock.beginBlock (juce::nullopt, 1025400000, 480) == 1035000000);
    CHECK (clock.beginBlock (juce::nullopt, 2000000000, 480) == 2010000000);
    CHECK (clock.beginBlock (juce::uint64 (2010000000), 2010200000, 480) == 2020000000);
    clock.prepare (96000, 480);
    CHECK (clock.beginBlock (juce::nullopt, 3000000000, 480) == 3005000000);
    CHECK (clock.eventTime (3005000000, 240) == 3007500000);
}

TEST_CASE ("CoreMIDI clock conversion preserves precision at long device uptimes", "[standalone-midi][timing]")
{
    using Clock = ksh::StandaloneMidiClock;
    constexpr juce::uint64 ns = 1234567890123456789;
    CHECK (Clock::nanosecondsToHostTicks (ns, 1, 1) == ns);
    CHECK (Clock::nanosecondsToHostTicks (ns, 125, 3) == 29629629362962962);
    CHECK (Clock::nanosecondsToHostTicks (1000000000, 125, 3) == 24000000);
}

TEST_CASE ("Timestamped MIDI packet conversion preserves offsets, short messages and SysEx", "[standalone-midi][timing]")
{
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::midiClock(), 0);
    midi.addEvent (juce::MidiMessage::programChange (2, 7), 1);
    midi.addEvent (juce::MidiMessage::noteOn (3, 60, juce::uint8 (100)), 127);
    midi.addEvent (juce::MidiMessage::noteOff (3, 60), 255);
    const juce::uint8 payload[] { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
    midi.addEvent (juce::MidiMessage::createSysExMessage (payload, sizeof (payload)), 256);
    std::vector<uint32_t> words;
    std::vector<int> positions;
    ksh::forEachStandaloneMidiPacket (midi, 5, [&] (int offset, const juce::ump::View& packet)
    {
        positions.push_back (offset);
        words.insert (words.end(), packet.begin(), packet.end());
    });
    CHECK (positions == std::vector<int> { 0, 1, 127, 255, 256, 256, 256 });
    REQUIRE (words.size() == 10);
    CHECK (words[0] == 0x15f80000u);
    CHECK (words[1] == 0x25c10700u);
    CHECK (words[2] == 0x25923c64u);
    CHECK (words[3] == 0x25823c00u);
    CHECK (words[4] == 0x35160102u); // SysEx start, six bytes
    CHECK (words[6] == 0x35260708u); // SysEx continuation
    CHECK (words[8] == 0x35310d00u); // SysEx end, one byte
}

namespace
{
    struct CapturingMidiOutput final : ksh::StandaloneMidiOutput
    {
        void prepare (double newRate, int size) override { rate = newRate; blockSize = size; ++prepares; }
        void stop() override { ++stops; }
        void send (const juce::MidiBuffer& buffer, juce::Optional<juce::uint64> host, int size, juce::Optional<juce::uint64> outputTime) override
        {
            midi = buffer;
            hostTime = host;
            presentationTime = outputTime;
            samples = size;
            ++sends;
        }
        juce::MidiBuffer midi;
        juce::Optional<juce::uint64> hostTime, presentationTime;
        double rate = 0;
        int blockSize = 0, samples = 0, prepares = 0, stops = 0, sends = 0;
    };

    struct TimestampPlayHead final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setHostTimeNs (987654321000);
            return info;
        }
    };
}

TEST_CASE ("Standalone sends processed MIDI with the hardware timestamp and detaches safely", "[standalone-midi][timing][plugin]")
{
    CapturingMidiOutput first, second;
    TimestampPlayHead playHead;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor plugin;
    plugin.setStandaloneMidiThru (true);
    plugin.setRateAndBufferSizeDetails (48000, 256);
    plugin.prepareToPlay (48000, 256);
    plugin.setPlayHead (&playHead);
    plugin.setStandaloneMidiOutput (&first);
    CHECK (first.rate == Catch::Approx (48000));
    CHECK (first.blockSize == 256);
    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, juce::uint8 (100)), 103);
    plugin.processBlock (audio, midi);
    CHECK (first.sends == 1);
    CHECK (first.samples == 256);
    CHECK (first.hostTime == juce::Optional<juce::uint64> (987654321000));
    REQUIRE_FALSE (first.midi.isEmpty());
    CHECK ((*first.midi.begin()).samplePosition == 103);
    CHECK ((*first.midi.begin()).getMessage().isNoteOn());
    plugin.setStandaloneMidiOutput (&first);
    CHECK (first.prepares == 1); // Unchanged route must not reset the clock.
    plugin.setStandaloneMidiOutput (&second);
    CHECK (first.stops == 1);
    midi.clear();
    plugin.processBlockBypassed (audio, midi);
    CHECK (first.sends == 1);
    CHECK (second.sends == 1);
    plugin.releaseResources();
    CHECK (second.stops == 1);
    plugin.prepareToPlay (44100, 512);
    CHECK (second.prepares == 2);
    CHECK (second.rate == Catch::Approx (44100));
    plugin.setStandaloneMidiOutput (nullptr);
    CHECK (second.stops == 2);
    plugin.processBlock (audio, midi);
    CHECK (second.sends == 1);
    plugin.setPlayHead (nullptr);
}

TEST_CASE ("Hosted plugins never use the standalone MIDI sink", "[standalone-midi][timing][plugin]")
{
    CapturingMidiOutput output;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AudioUnitv3);
    PluginProcessor plugin;
    plugin.prepareToPlay (48000, 256);
    plugin.setStandaloneMidiOutput (&output);
    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    plugin.processBlock (audio, midi);
    CHECK (output.prepares == 0);
    CHECK (output.sends == 0);
}

TEST_CASE ("Standalone transport stop reaches the timestamped output and restart generates notes again", "[standalone-midi][timing][plugin]")
{
    CapturingMidiOutput output;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor plugin;
    plugin.setRateAndBufferSizeDetails (48000, 256);
    plugin.prepareToPlay (48000, 256);
    plugin.setStandaloneMidiOutput (&output);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));
    plugin.setStandaloneTransportPlaying (true);
    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    const auto hasNoteOn = [&]
    {
        for (const auto item : output.midi)
            if (item.getMessage().isNoteOn())
                return true;
        return false;
    };
    plugin.processBlock (audio, midi);
    CHECK (hasNoteOn());
    plugin.setStandaloneTransportPlaying (false);
    midi.clear();
    plugin.processBlock (audio, midi);
    CHECK_FALSE (hasNoteOn());
    int allNotesOff = 0;
    for (const auto item : output.midi)
        if (item.getMessage().isAllNotesOff())
            ++allNotesOff;
    CHECK (allNotesOff == 16);
    plugin.setStandaloneTransportPlaying (true);
    midi.clear();
    plugin.processBlock (audio, midi);
    CHECK (hasNoteOn());
    plugin.setStandaloneMidiOutput (nullptr);
}

TEST_CASE ("Standalone sync launches MIDI and the metronome in the same block", "[standalone-link][standalone-midi][timing]")
{
    struct Clock final : ksh::StandaloneSync
    {
        ksh::StandaloneSyncBlock block;
        bool playingRequested = false;
        bool requestPlaying (bool play, bool) override { playingRequested = play; return true; }
        bool requestTempo (double) override { return true; }
        double sessionTempo() const override { return 100.0; }
        ksh::StandaloneSyncBlock process (juce::Optional<juce::uint64>, double, int, double, bool) override { return block; }
    } clock;

    CapturingMidiOutput output;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor plugin;
    plugin.setRateAndBufferSizeDetails (48000, 256);
    plugin.prepareToPlay (48000, 256);
    plugin.setStandaloneSync (&clock);
    plugin.setStandaloneMidiOutput (&output);
    plugin.setStandaloneMetronomeEnabled (true);
    REQUIRE (plugin.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));
    plugin.setStandaloneTransportPlaying (true);
    CHECK (clock.playingRequested);

    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    clock.block = { juce::uint64 (987654321000), -0.25, 100.0, 0, false, true, false };
    plugin.processBlock (audio, midi);
    CHECK (plugin.isStandaloneLaunchPending());
    CHECK (output.midi.isEmpty());
    CHECK (audio.getMagnitude (0, 256) == Catch::Approx (0));

    const auto nowNs = static_cast<juce::uint64> (juce::Time::getMillisecondCounterHiRes() * 1.0e6);
    constexpr int launchSample = 103;
    clock.block = { nowNs - 10000000, -launchSample * 100.0 / (60.0 * 48000.0),
                    100.0, launchSample, true, false, true };
    plugin.processBlock (audio, midi);
    CHECK_FALSE (plugin.isStandaloneLaunchPending());
    REQUIRE_FALSE (output.midi.isEmpty());
    CHECK ((*output.midi.begin()).getMessage().isNoteOn());
    CHECK ((*output.midi.begin()).samplePosition == launchSample);
    CHECK (output.presentationTime == clock.block.presentationTimeNs);
    CHECK (audio.getMagnitude (0, launchSample) == Catch::Approx (0));
    CHECK (audio.getMagnitude (launchSample, 256 - launchSample) > 0.0f);
    CHECK (plugin.getStandaloneTempoBpm() == Catch::Approx (100.0));

    // A touch can occur before the currently rendered block reaches output,
    // even while an earlier block has already started playing.
    plugin.setPatternRecordingEnabled (true, 0);
    midi.clear();
    clock.block = { nowNs + 50000000, 0.1, 100.0, 0, true, false, false };
    plugin.processBlock (audio, midi);
    REQUIRE (plugin.triggerRowFromUi (1, 91));
    REQUIRE (plugin.takePatternRecordingEdits().cells.size() == 1);
    CHECK (plugin.engineStateSnapshot().sources[0][1][0].velocity == 91);

    clock.block = { nowNs + 55000000, 0.11, 100.0, 0, false, false, false };
    midi.clear();
    plugin.processBlock (audio, midi);
    CHECK_FALSE (plugin.isStandaloneTransportPlaying());
    REQUIRE_FALSE (output.midi.isEmpty());
    CHECK (std::any_of (output.midi.begin(), output.midi.end(), [] (const auto item) { return item.getMessage().isNoteOff(); }));
    plugin.setStandaloneMidiOutput (nullptr);
    plugin.setStandaloneSync (nullptr);
}
