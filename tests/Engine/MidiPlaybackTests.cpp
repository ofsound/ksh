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

TEST_CASE ("midi playback keeps cycle one active", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1, 0);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto result = runPlaybackBlock (runner, fixture.engine.makePlaybackSnapshot(), 0.0, 120.0, true, 512);

    REQUIRE (countNoteOns (result.midi) == 1);
    REQUIRE (result.noteHitCount == 1);
}

TEST_CASE ("midi playback applies an inspector cycle pattern independently to multiple steps", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);

    for (int step = 2; step <= 5; ++step)
        fixture.engine.setCell (0, 0, step, true, 100, 100, 3, 0b100);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    const double beatsPerStep = snapshot.beatsPerStep;
    std::vector<int> hitSteps;

    for (int step = 0; step < 24; ++step)
    {
        const auto result = runPlaybackBlock (runner,
                                              snapshot,
                                              static_cast<double> (step) * beatsPerStep,
                                              120.0,
                                              true,
                                              512);

        if (countNoteOns (result.midi) > 0)
            hitSteps.push_back (step);
    }

    REQUIRE (hitSteps == std::vector<int> { 18, 19, 20, 21 });
}

TEST_CASE ("midi playback resets cycle phases when the playback definition changes", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (2);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 3, 0b001);
    fixture.engine.setCell (0, 0, 1, true, 100, 100, 1, 0b001);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto initialSnapshot = fixture.engine.makePlaybackSnapshot();
    const double beatsPerStep = initialSnapshot.beatsPerStep;

    [[maybe_unused]] const auto first = runPlaybackBlock (runner, initialSnapshot, 0.0, 120.0, true, 512);
    [[maybe_unused]] const auto second = runPlaybackBlock (runner, initialSnapshot, beatsPerStep, 120.0, true, 512);
    [[maybe_unused]] const auto third = runPlaybackBlock (runner, initialSnapshot, 2.0 * beatsPerStep, 120.0, true, 512);

    fixture.engine.setCellCycleMask (0, 0, 0, 0b100);
    fixture.engine.setCellCycle (0, 0, 1, 3);
    fixture.engine.setCellCycleMask (0, 0, 1, 0b100);
    const auto updatedSnapshot = fixture.engine.makePlaybackSnapshot();
    std::vector<int> hitSteps;

    for (int step = 3; step <= 8; ++step)
    {
        const auto result = runPlaybackBlock (runner,
                                              updatedSnapshot,
                                              static_cast<double> (step) * beatsPerStep,
                                              120.0,
                                              true,
                                              512);

        if (countNoteOns (result.midi) > 0)
            hitSteps.push_back (step);
    }

    REQUIRE (hitSteps == std::vector<int> { 7, 8 });
}

TEST_CASE ("midi playback preserves cycle phases across transport window refreshes", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 3, 0b001);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const double beatsPerStep = fixture.engine.beatsPerStep();
    std::vector<int> hitSteps;

    for (int step = 0; step < 6; ++step)
    {
        fixture.engine.transportPosition (static_cast<double> (step) * beatsPerStep, true);
        const auto result = runPlaybackBlock (runner,
                                              fixture.engine.makePlaybackSnapshot(),
                                              static_cast<double> (step) * beatsPerStep,
                                              120.0,
                                              true,
                                              512);

        if (countNoteOns (result.midi) > 0)
            hitSteps.push_back (step);
    }

    REQUIRE (hitSteps == std::vector<int> { 0, 3 });
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

TEST_CASE ("midi playback emits note_hit for step 1 on every loop wrap", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.engine.setCell (0, 0, 2, true, 100, 100, 1);
    fixture.engine.generateWindow (0, 4, true);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    const double beatsPerStep = snapshot.beatsPerStep;
    int step1Hits = 0;

    for (int globalStep = 0; globalStep < 12; ++globalStep)
    {
        const auto result = runPlaybackBlock (runner,
                                              snapshot,
                                              static_cast<double> (globalStep) * beatsPerStep,
                                              120.0,
                                              true,
                                              512);

        if (globalStep % 4 == 0)
        {
            REQUIRE (countNoteOns (result.midi) == 1);
            REQUIRE (result.noteHitCount == 1);
            REQUIRE (result.noteHits[0].uiGeneratedStep == 1);
            REQUIRE (result.noteHits[0].uiSourceStep == 1);
            ++step1Hits;
        }
    }

    REQUIRE (step1Hits == 3);
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

TEST_CASE ("midi playback does not emit next boundary at previous block end", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setStaticSource (4);
    fixture.engine.setCell (4, 0, 1, true, 100, 100, 1);
    fixture.engine.setCell (0, 0, 1, true, 100, 100, 1);
    fixture.engine.generateWindow (0, 4, true);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (48000.0);

    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    const auto block0 = runPlaybackBlock (runner, snapshot, 0.0, 120.0, true, 6000);

    REQUIRE (countNoteOns (block0.midi) == 0);

    juce::MidiBuffer midi;
    MidiPatternSelectionBlock selections;
    selections.add (0, 0);
    const auto block1 = runner.processBlock (snapshot, 0.25, 120.0, true, 6000, midi, selections);

    REQUIRE (countNoteOns (midi) == 1);
    REQUIRE (block1.noteHitCount == 1);
    REQUIRE (block1.noteHits[0].uiSource == 1);
    REQUIRE (block1.noteHits[0].uiSourceStep == 2);
}

TEST_CASE ("midi pattern selection suppresses queued old-pattern note-ons", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setStaticSource (4);
    fixture.engine.setCell (4, 0, 0, true, 100, 100, 1, 0, false, 2);
    fixture.engine.setCell (0, 0, 1, true, 100, 100, 1);
    fixture.engine.generateWindow (0, 4, true);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (48000.0);

    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    const auto block0 = runPlaybackBlock (runner, snapshot, 0.0, 120.0, true, 3000);
    REQUIRE (countNoteOns (block0.midi) == 1);
    REQUIRE (block0.noteHitCount == 1);
    REQUIRE (block0.noteHits[0].uiSource == 5);

    juce::MidiBuffer switchMidi;
    MidiPatternSelectionBlock selections;
    selections.add (0, 0);
    const auto switchBlock = runner.processBlock (snapshot, 0.125, 120.0, true, 3000, switchMidi, selections);
    REQUIRE (countNoteOns (switchMidi) == 0);
    REQUIRE (switchBlock.noteHitCount == 0);

    const auto nextBlock = runPlaybackBlock (runner, snapshot, 0.25, 120.0, true, 3000);
    REQUIRE (countNoteOns (nextBlock.midi) == 1);
    REQUIRE (nextBlock.noteHitCount == 1);
    REQUIRE (nextBlock.noteHits[0].uiSource == 1);
    REQUIRE (nextBlock.noteHits[0].uiSourceStep == 2);
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
    const auto step1 = runPlaybackBlock (runner, snapshot, 0.25, 120.0, true, 4096);

    int noteOnSample = -1;

    for (const auto metadata : step1.midi)
    {
        if (metadata.getMessage().isNoteOn())
            noteOnSample = metadata.samplePosition;
    }

    REQUIRE (noteOnSample >= 0);
    REQUIRE (noteOnSample > 0);
}

TEST_CASE ("midi playback uses the selected swing subdivision phase", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (2);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setSwing (100);
    fixture.engine.setCell (0, 0, 1, true, 80, 100, 1, 0, false, 4);
    fixture.engine.generateWindow (0, 2, true);

    ksh::MidiPlaybackRunner defaultRunner;
    defaultRunner.prepare (44100.0);
    const auto defaultSnapshot = fixture.engine.makePlaybackSnapshot();
    [[maybe_unused]] const auto defaultStep0 = runPlaybackBlock (defaultRunner,
                                                                  defaultSnapshot,
                                                                  0.0,
                                                                  120.0,
                                                                  true,
                                                                  512);
    const auto defaultStep1 = runPlaybackBlock (defaultRunner,
                                                defaultSnapshot,
                                                0.25,
                                                120.0,
                                                true,
                                                4096);

    REQUIRE (defaultStep1.noteHitCount > 0);
    REQUIRE (defaultStep1.noteHits[0].delayMs == 62.5);

    fixture.engine.setSwingSubdivisionIndex (0);
    ksh::MidiPlaybackRunner fineRunner;
    fineRunner.prepare (44100.0);
    const auto fineSnapshot = fixture.engine.makePlaybackSnapshot();
    [[maybe_unused]] const auto fineStep0 = runPlaybackBlock (fineRunner,
                                                               fineSnapshot,
                                                               0.0,
                                                               120.0,
                                                               true,
                                                               512);
    const auto fineStep1 = runPlaybackBlock (fineRunner,
                                             fineSnapshot,
                                             0.25,
                                             120.0,
                                             true,
                                             4096);

    REQUIRE (fineStep1.noteHitCount > 0);
    REQUIRE (fineStep1.noteHits[0].delayMs == 0.0);
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

TEST_CASE ("midi pattern selection plays selected source on beat one", "[engine][transport]")
{
    auto runSelectionCase = [] (bool setStaticSourceFirst)
    {
        EngineFixture fixture;
        fixture.clearAll();
        fixture.engine.setStepCount (16);
        fixture.engine.setChannelCount (8);
        fixture.engine.setGenerationMode (GenerationMode::staticSource);
        fixture.engine.setRate ("16n");
        fixture.engine.setTempo (120.0);

        if (setStaticSourceFirst)
        {
            fixture.engine.setStaticSource (4);
            fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
        }
        else
        {
            fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
            fixture.engine.setStaticSource (4);
        }

        fixture.engine.generateWindow (0, 16, true);

        ksh::MidiPlaybackRunner runner;
        runner.prepare (44100.0);

        juce::MidiBuffer midi;
        MidiPatternSelectionBlock selections;
        selections.add (0, 0);
        const auto result = runner.processBlock (fixture.engine.makePlaybackSnapshot(),
                                                 0.0,
                                                 120.0,
                                                 true,
                                                 512,
                                                 midi,
                                                 selections);

        return containsNoteOn (midi, 36) && result.noteHitCount == 1;
    };

    REQUIRE (runSelectionCase (false));
    REQUIRE (runSelectionCase (true));
}

TEST_CASE ("plugin processor initializes with blank default pattern", "[processor][transport]")
{
    PluginProcessor processor;
    const auto engine = processor.engineStateSnapshot();

    REQUIRE (engine.deviceActive);
    REQUIRE_FALSE (engine.sources[0][0][0].enabled);
}

TEST_CASE ("midi playback emits after velocity humanize", "[engine][transport]")
{
    PluginProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    REQUIRE (processor.dispatchUiEngineCommand ("velocity_humanize", { 10 }));
    REQUIRE (processor.engineStateSnapshot().deviceActive);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto result = runPlaybackBlock (runner, processor.enginePlaybackSnapshot(), 0.0, 120.0, true, 512);

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

TEST_CASE ("midi playback emits evenly spaced roll hits across blocks", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1, 0, false, 2);
    fixture.engine.generateWindow (0, 1, true);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    REQUIRE (snapshot.generated[0][0].roll == 2);

    const auto withinBlock = runPlaybackBlock (runner, snapshot, 0.0, 120.0, true, 4096);

    std::vector<int> noteOnSamples;

    for (const auto metadata : withinBlock.midi)
    {
        if (metadata.getMessage().isNoteOn())
            noteOnSamples.push_back (metadata.samplePosition);
    }

    REQUIRE (noteOnSamples.size() == 2);

    const int expectedSecondHit = static_cast<int> (std::llround (0.0625 * 44100.0));
    REQUIRE (noteOnSamples[0] == 0);
    REQUIRE (std::abs (noteOnSamples[1] - expectedSecondHit) <= 1);

    runner.reset();
    const auto block0 = runPlaybackBlock (runner, snapshot, 0.0, 120.0, true, 512);
    REQUIRE (countNoteOns (block0.midi) == 1);

    const double beatsPerBlock = (512.0 / 44100.0) * (120.0 / 60.0);
    double ppq = beatsPerBlock;
    int deferredHits = 0;

    for (int block = 1; block < 8; ++block)
    {
        const auto result = runPlaybackBlock (runner, snapshot, ppq, 120.0, true, 512);
        deferredHits += countNoteOns (result.midi);
        ppq += beatsPerBlock;

        if (deferredHits > 0)
            break;
    }

    REQUIRE (deferredHits == 1);
}

TEST_CASE ("midi playback repeats offset loop hits on loop length cadence", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setChannelLoopLength (0, 10, 2);
    fixture.engine.setCell (0, 0, 2, true, 100, 100, 1);
    fixture.engine.setCell (0, 0, 8, true, 100, 100, 1);
    fixture.engine.setCell (0, 0, 10, true, 100, 100, 1);
    fixture.engine.generateWindow (0, 16, true);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    const double beatsPerStep = snapshot.beatsPerStep;
    std::vector<int> hitSteps;

    for (int step = 0; step < 20; ++step)
    {
        const auto result = runPlaybackBlock (runner, snapshot, static_cast<double> (step) * beatsPerStep, 120.0, true, 512);

        if (countNoteOns (result.midi) > 0)
            hitSteps.push_back (step);
    }

    REQUIRE (hitSteps == std::vector<int> { 0, 6, 8, 10, 16, 18 });
}
