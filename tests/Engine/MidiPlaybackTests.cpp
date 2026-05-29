#include "EngineTestHelpers.h"

#include <PluginProcessor.h>
#include <engine/KshMidiPlayback.h>

#include <catch2/catch_test_macros.hpp>

using namespace ksh;
using ksh::test::EngineFixture;

namespace
{
struct TestMidiPlaybackResult : MidiPlaybackResult
{
    juce::MidiBuffer midi;
};

TestMidiPlaybackResult runPlaybackBlock (MidiPlaybackRunner& runner,
                                         const PlaybackSnapshot& snapshot,
                                         double ppqPosition,
                                         double bpm,
                                         bool isPlaying,
                                         int numSamples)
{
    TestMidiPlaybackResult out;
    static_cast<MidiPlaybackResult&> (out) =
        runner.processBlock (snapshot, ppqPosition, bpm, isPlaying, numSamples, out.midi);
    return out;
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
} // namespace

TEST_CASE ("midi playback emits kick on first transport step", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto result = runPlaybackBlock (runner, fixture.engine.makePlaybackSnapshot(), 0.0, 120.0, true, 512);

    REQUIRE (countNoteOns (result.midi) == 1);
    REQUIRE (containsNoteOn (result.midi, 36));
    REQUIRE (result.noteHitCount == 1);
}

TEST_CASE ("midi playback does not emit while stopped", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto result = runPlaybackBlock (runner, fixture.engine.makePlaybackSnapshot(), 0.0, 120.0, false, 512);

    REQUIRE (countNoteOns (result.midi) == 0);
    REQUIRE (result.noteHitCount == 0);
}

TEST_CASE ("audition note emits while transport stopped", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    runner.queueAuditionNote ({ 42, 100, 1, 100, 0.0 });

    const auto result = runPlaybackBlock (runner, fixture.engine.makePlaybackSnapshot(), 0.0, 120.0, false, 512);

    REQUIRE (countNoteOns (result.midi) == 1);
    REQUIRE (containsNoteOn (result.midi, 42));
}

TEST_CASE ("midi playback does not emit when device inactive", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.engine.setDeviceActive (false);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto result = runPlaybackBlock (runner, fixture.engine.makePlaybackSnapshot(), 0.0, 120.0, true, 512);

    REQUIRE (countNoteOns (result.midi) == 0);
}

TEST_CASE ("midi playback does not emit on transport jump", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.engine.setCell (0, 0, 1, true, 100, 100, 1);
    fixture.engine.setCell (0, 0, 2, true, 100, 100, 1);
    fixture.engine.setCell (0, 0, 3, true, 100, 100, 1);
    fixture.engine.generateWindow (0, 4, true);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    [[maybe_unused]] const auto first = runPlaybackBlock (runner, snapshot, 0.0, 120.0, true, 512);
    const auto jumped = runPlaybackBlock (runner, snapshot, 0.75, 120.0, true, 512);

    REQUIRE (countNoteOns (jumped.midi) == 0);
    REQUIRE (jumped.currentStepOneBased == 4);
}

TEST_CASE ("midi playback advances one step per sixteenth at 120 bpm", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.engine.setCell (0, 0, 1, true, 100, 100, 1);
    fixture.engine.generateWindow (0, 4, true);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    [[maybe_unused]] const auto step0 = runPlaybackBlock (runner, snapshot, 0.0, 120.0, true, 512);
    const auto step1 = runPlaybackBlock (runner, snapshot, 0.25, 120.0, true, 512);

    REQUIRE (countNoteOns (step1.midi) == 1);
    REQUIRE (step1.currentStepOneBased == 2);
}

TEST_CASE ("midi playback applies swing delay within block", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (2);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setSwing (100);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.engine.setCell (0, 0, 1, true, 80, 100, 1);
    fixture.engine.generateWindow (0, 2, true);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    [[maybe_unused]] const auto step0 = runPlaybackBlock (runner, snapshot, 0.0, 120.0, true, 512);
    const auto step1 = runPlaybackBlock (runner, snapshot, 0.25, 120.0, true, 512);

    int noteOnSample = -1;

    for (const auto metadata : step1.midi)
    {
        if (metadata.getMessage().isNoteOn())
            noteOnSample = metadata.samplePosition;
    }

    REQUIRE (noteOnSample >= 0);
    REQUIRE (noteOnSample > 0);
}

TEST_CASE ("phase offset shifts step boundary earlier", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setTempo (120.0);
    fixture.engine.setPhaseOffsetBeats (-0.2);

    REQUIRE (fixture.engine.globalStepForBeats (0.05) == 1);
    REQUIRE (fixture.engine.globalStepForBeats (0.0) == 0);
}

TEST_CASE ("transport position does not fire while stopped", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    fixture.engine.transportPosition (0.0, false);
    fixture.engine.transportPosition (0.25, false);

    const auto result = runPlaybackBlock (runner, fixture.engine.makePlaybackSnapshot(), 0.25, 120.0, false, 512);

    REQUIRE (countNoteOns (result.midi) == 0);
}

TEST_CASE ("midi playback survives velocity humanize", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.engine.generateWindow (0, 16, true);
    fixture.engine.setVelocityHumanize (1);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto result = runPlaybackBlock (runner, fixture.engine.makePlaybackSnapshot(), 0.0, 120.0, true, 512);

    REQUIRE (countNoteOns (result.midi) == 1);
}

TEST_CASE ("plugin processor initializes with playable default pattern", "[processor][transport]")
{
    PluginProcessor processor;

    REQUIRE (processor.getEngine().nativePlaybackActive());
    REQUIRE (processor.getEngine().sourceCellAt (0, 0, 0).enabled);
}

TEST_CASE ("midi playback emits after velocity humanize", "[engine][transport]")
{
    PluginProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    processor.getEngine().setVelocityHumanize (10);
    REQUIRE (processor.getEngine().nativePlaybackActive());

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto result = runPlaybackBlock (runner, processor.getEngine().makePlaybackSnapshot(), 0.0, 120.0, true, 512);

    REQUIRE (countNoteOns (result.midi) >= 1);
}

TEST_CASE ("midi playback stays active with velocity humanize", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (32);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 9);
    fixture.engine.generateWindow (0, 32, true);
    fixture.engine.setVelocityHumanize (10);

    REQUIRE (fixture.engine.nativePlaybackActive());

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto result = runPlaybackBlock (runner, fixture.engine.makePlaybackSnapshot(), 0.0, 120.0, true, 512);

    REQUIRE (countNoteOns (result.midi) >= 1);
}
