#include "EngineTestHelpers.h"
#include <engine/KshMidiExport.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace ksh;
using ksh::test::EngineFixture;

namespace
{
juce::MidiFile exportedPattern (const PlaybackSnapshot& snapshot, int source = 0)
{
    juce::TemporaryFile temporary (".mid");
    REQUIRE (writePatternMidiFile (snapshot, source, temporary.getFile(), "KSH - P" + juce::String (source + 1)).wasOk());
    juce::MemoryBlock bytes;
    REQUIRE (temporary.getFile().loadFileAsData (bytes));
    REQUIRE (bytes.getSize() > 14);
    const auto* raw = static_cast<const unsigned char*> (bytes.getData());
    CHECK (raw[8] == 0);
    CHECK (raw[9] == 0); // SMF0: one clip, all drum pitches on one track.
    juce::FileInputStream input (temporary.getFile());
    juce::MidiFile midi;
    REQUIRE (midi.readFrom (input));
    REQUIRE (midi.getNumTracks() == 1);
    REQUIRE (midi.getTimeFormat() == 960);
    return midi;
}

std::vector<std::tuple<int, int, double>> noteOns (const juce::MidiFile& midi)
{
    std::vector<std::tuple<int, int, double>> notes;
    const auto& track = *midi.getTrack (0);
    for (int i = 0; i < track.getNumEvents(); ++i)
    {
        const auto& message = track.getEventPointer (i)->message;
        if (message.isNoteOn())
            notes.emplace_back (message.getNoteNumber(), message.getVelocity(), message.getTimeStamp());
        CHECK_FALSE (message.isTempoMetaEvent());
    }
    return notes;
}
}

TEST_CASE ("MIDI export uses the dragged pattern's rate and length without changing playback", "[engine][midi-export]")
{
    const auto rate = GENERATE ("4n", "4nt", "8n", "8nt", "16n", "16nt", "32n", "32nt");
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (2);
    fixture.engine.setCell (0, 0, 0, true, 127);
    fixture.engine.setCell (1, 0, 0, true, 91);
    fixture.engine.setCell (1, 1, 2, true, 83);
    fixture.engine.setSourceStepCount (1, 8);
    fixture.engine.setSourceRate (1, rate);
    fixture.engine.setGenerationMode (GenerationMode::perChannel);
    fixture.engine.setStaticSource (-1);
    const auto before = fixture.engine.serializeForPersistence();
    const auto midi = exportedPattern (fixture.engine.makePlaybackSnapshot(), 1);
    const auto notes = noteOns (midi);
    REQUIRE (notes.size() == 2);
    CHECK (std::get<0> (notes[0]) == 36);
    CHECK (std::get<1> (notes[0]) == 91);
    CHECK (std::get<2> (notes[0]) == Catch::Approx (0));
    CHECK (std::get<0> (notes[1]) == 37);
    CHECK (std::get<1> (notes[1]) == 83);
    CHECK (std::get<2> (notes[1]) == std::round (2 * Constants::beatsForRate (rate) * 960));
    CHECK (midi.getLastTimestamp() == std::round (8 * Constants::beatsForRate (rate) * 960));
    CHECK (fixture.engine.serializeForPersistence() == before);
}

TEST_CASE ("MIDI export renders early notes and rolls without leaking another pass", "[engine][midi-export]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setTempo (120);
    fixture.engine.setCell (0, 0, 0, true, 100);
    fixture.engine.setCellNudge (0, 0, 0, -50);
    fixture.engine.setCell (0, 0, 1, true, 90, 100, 1, 1, 2);
    fixture.engine.setCellNudge (0, 0, 1, -50);
    fixture.engine.setCell (0, 0, 15, true, 80);
    fixture.engine.setCellNudge (0, 0, 15, -50);
    const auto notes = noteOns (exportedPattern (fixture.engine.makePlaybackSnapshot()));
    const std::vector<std::tuple<int, int, double>> expected {
        { 36, 100, 0 }, { 36, 90, 120 }, { 36, 90, 240 }, { 36, 80, 3480 }
    };
    CHECK (notes == expected);
}

TEST_CASE ("MIDI export respects channel loops direction mutes and cycle gates", "[engine][midi-export]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (3);
    fixture.engine.setSourceStepCount (0, 8);
    fixture.engine.setCell (0, 0, 2, true, 100);
    fixture.engine.setChannelLoopLength (0, 2, 2);
    fixture.engine.setChannelPlaybackMode (0, PlaybackMode::reverse);
    fixture.engine.setCell (0, 1, 0, true, 90);
    fixture.engine.setSourceChannelMute (0, 1, true);
    fixture.engine.setCell (0, 2, 0, true, 80, 100, 2, 2); // First cycle visit is silent.
    const auto notes = noteOns (exportedPattern (fixture.engine.makePlaybackSnapshot()));
    const std::vector<std::tuple<int, int, double>> expected {
        { 36, 100, 240 }, { 36, 100, 720 }, { 36, 100, 1200 }, { 36, 100, 1680 }
    };
    CHECK (notes == expected);
}

TEST_CASE ("MIDI export preserves silence and closes final notes at the clip boundary", "[engine][midi-export]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setSourceStepCount (0, 4);
    fixture.engine.setRate ("32n");
    fixture.engine.setTempo (300);
    SECTION ("Empty pattern still has its full duration")
    {
        const auto midi = exportedPattern (fixture.engine.makePlaybackSnapshot());
        CHECK (noteOns (midi).empty());
        CHECK (midi.getLastTimestamp() == Catch::Approx (480));
    }
    SECTION ("A final sustained note is paired and ends at the boundary")
    {
        fixture.engine.setCell (0, 0, 3, true, 100);
        const auto midi = exportedPattern (fixture.engine.makePlaybackSnapshot());
        const auto& track = *midi.getTrack (0);
        REQUIRE (noteOns (midi).size() == 1);
        for (int i = 0; i < track.getNumEvents(); ++i)
        {
            const auto* event = track.getEventPointer (i);
            if (event->message.isNoteOn())
            {
                REQUIRE (event->noteOffObject != nullptr);
                CHECK (event->noteOffObject->message.getTimeStamp() == Catch::Approx (480));
            }
        }
        CHECK (midi.getLastTimestamp() == Catch::Approx (480));
    }
}

TEST_CASE ("MIDI export chance and humanization are repeatable and write failures are reported", "[engine][midi-export]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setSwing (40);
    fixture.engine.setVelocityHumanize (20);
    fixture.engine.setTimingHumanize (20);
    for (int step = 0; step < 16; ++step)
        fixture.engine.setCell (0, 0, step, true, 90, 70);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    const auto first = exportedPattern (snapshot);
    CHECK (noteOns (exportedPattern (snapshot)) == noteOns (first));
    CHECK_FALSE (noteOns (first).empty());
    juce::TemporaryFile missing (".mid");
    CHECK (writePatternMidiFile (snapshot, -1, missing.getFile(), "invalid").failed());
    CHECK_FALSE (missing.getFile().exists());
    CHECK (writePatternMidiFile (snapshot, 0, missing.getFile().getChildFile ("missing.mid"), "unwritable").failed());
}
