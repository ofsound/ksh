#include "EngineTestHelpers.h"

#include <PluginProcessor.h>
#include <engine/KshMidiPlayback.h>
#include <engine/KshMath.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <set>

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

TEST_CASE ("Split hits apply their own probability cycle and velocity on the original timing", "[engine][transport][split-edits]")
{
    const int span = GENERATE (1, 2);
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelLoopLength (0, 4);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    Cell cell { .enabled = true, .velocity = 74, .cycle = 3, .cycleMask = 0b101, .roll = 3, .span = span };
    cell.splitGates[0].setCycle (2, 0b10);
    cell.splitGates[1].setProbability (0);
    cell.setSplitVelocity (0, 31);
    fixture.engine.setCell (0, 0, 0, cell);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    constexpr int loopSamples = 24000;
    constexpr int blockSize = 503;
    std::vector<std::pair<int, int>> onsets;
    std::vector<int> splitPhases;
    for (int sample = 0; sample < 4 * loopSamples; sample += blockSize)
    {
        const auto played = runPlaybackBlock (runner, snapshot, sample / 24000.0, 120.0, true,
                                              std::min (blockSize, 4 * loopSamples - sample));
        for (size_t i = 0; i < played.cycleVisitCount; ++i)
            splitPhases.push_back (played.cycleVisits[i].splitSteps[0]);
        for (const auto event : played.midi)
            if (const auto message = event.getMessage(); message.isNoteOn())
                onsets.emplace_back (sample + event.samplePosition, message.getVelocity());
    }
    const int lastHit = span * 4000;
    CHECK (splitPhases == std::vector<int> { 1, 2, 1, 2 });
    CHECK (onsets == std::vector<std::pair<int, int>> { { lastHit, 74 }, { 2 * loopSamples + lastHit, 74 },
                                                       { 3 * loopSamples, 31 }, { 3 * loopSamples + lastHit, 74 } });
}

TEST_CASE ("Spanned rolls play evenly on a swung sixteenth grid", "[engine][transport][realtime][triplets]")
{
    const int span = GENERATE (2, 4, 8);
    const int hits = GENERATE (3, 5);
    const int blockSize = GENERATE (257, 8192);
    const auto mode = GENERATE (PlaybackMode::normal, PlaybackMode::reverse, PlaybackMode::ping_pong, PlaybackMode::reverse_ping_pong);
    const bool reversed = mode == PlaybackMode::reverse || mode == PlaybackMode::reverse_ping_pong;
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setSwing (72);
    fixture.engine.setChannelPlaybackMode (0, mode);
    fixture.engine.setCell (0, 0, 1, Cell { .enabled = true, .velocity = 79, .cycle = 2, .cycleMask = 1, .roll = hits, .nudge = 10, .span = span });
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    std::vector<int> onsets;
    int cycleVisits = 0;
    // Use a host tempo different from the snapshot to check beat-relative spacing.
    constexpr double bpm = 144.0;
    constexpr int stepSamples = 5000;
    const int cycleLength = 16 * stepSamples;
    for (int sample = 0; sample < 2 * cycleLength; sample += blockSize)
    {
        const auto result = runPlaybackBlock (runner, snapshot, static_cast<double> (sample) / 20000.0, bpm, true,
                                              std::min (blockSize, 2 * cycleLength - sample));
        cycleVisits += static_cast<int> (result.cycleVisitCount);
        for (const auto event : result.midi)
            if (event.getMessage().isNoteOn())
                onsets.push_back (sample + event.samplePosition);
    }
    CHECK (cycleVisits == 2);
    REQUIRE (onsets.size() == static_cast<size_t> (hits));
    const int leadStep = reversed ? 15 - span : 1;
    for (int hit = 0; hit < hits; ++hit)
        CHECK (onsets[static_cast<size_t> (hit)] == leadStep * stepSamples + 500 + static_cast<int> (std::lround (static_cast<double> (stepSamples * span * hit) / hits)));
}

TEST_CASE ("Spanned rolls respect row and host loop boundaries", "[engine][transport][realtime][triplets]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setCell (0, 0, 0, Cell { .enabled = true, .roll = 3, .span = 4 });
    const bool shortened = GENERATE (false, true);
    if (shortened)
        fixture.engine.setChannelLoopLength (0, 2);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    const HostLoopRange loop { true, 0.0, 0.5 };
    std::vector<int> attacks;
    for (int sample = 0; sample < 24000; sample += 1000)
    {
        juce::MidiBuffer midi;
        runner.processBlock (snapshot, static_cast<double> (sample % 12000) / 24000.0, 120.0, true, 1000, midi, {}, loop);
        for (const auto event : midi)
            if (event.getMessage().isNoteOn())
                attacks.push_back (sample + event.samplePosition);
    }
    if (shortened)
        CHECK (attacks == std::vector<int> { 0, 2000, 4000, 12000, 14000, 16000 });
    else
        CHECK (attacks == std::vector<int> { 0, 8000, 12000, 20000 });
}

TEST_CASE ("Changing the current pattern releases queued hits from a spanned roll", "[engine][transport][realtime][triplets]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setCell (0, 0, 0, Cell { .enabled = true, .roll = 3, .span = 8 });
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    CHECK (countNoteOns (runPlaybackBlock (runner, fixture.engine.makePlaybackSnapshot(), 0.0, 120.0, true, 6000).midi) == 1);
    fixture.engine.setStaticSource (1);
    CHECK (countNoteOns (runPlaybackBlock (runner, fixture.engine.makePlaybackSnapshot(), 0.25, 120.0, true, 36000).midi) == 0);
}

TEST_CASE ("bipolar cell nudges move every roll hit by a percentage of the step", "[engine][transport][realtime][nudge]")
{
    const int nudge = GENERATE (-100, -50, -1, 0, 1, 50, 100);
    const int blockSize = GENERATE (64, 512, 4096, 13000);
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setTempo (120.0);
    fixture.engine.setRate ("16n");
    fixture.engine.setCell (0, 0, 1, true, 100, 100, 1, 1, 2);
    fixture.engine.setCellNudge (0, 0, 1, nudge);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    std::vector<int> ons;
    for (int sample = 0, block = 0; sample < 17000; ++block)
    {
        const int size = block % 2 == 0 ? blockSize : 73;
        const auto result = runPlaybackBlock (runner, snapshot, static_cast<double> (sample) / 24000.0, 120.0, true, size);
        for (const auto event : result.midi)
            if (event.getMessage().isNoteOn())
                ons.push_back (sample + event.samplePosition);
        sample += size;
    }
    CHECK (ons == std::vector<int> { 6000 + nudge * 60, 9000 + nudge * 60 });
}

TEST_CASE ("nudged generative playback is repeatable across different callback sizes", "[engine][transport][nudge]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (2);
    fixture.engine.setGenerationMode (GenerationMode::perChannel);
    fixture.engine.setRefreshSteps (4);
    fixture.engine.setTempo (120.0);
    fixture.engine.setRate ("16n");
    fixture.engine.setSwing (23);
    fixture.engine.setVelocityHumanize (11);
    fixture.engine.setTimingHumanize (12);
    for (int source = 0; source < 2; ++source)
        for (int channel = 0; channel < 2; ++channel)
            for (int step = 0; step < 16; ++step)
            {
                fixture.engine.setCell (source, channel, step, true, 75 + source * 20, 80, 2, 3, 2);
                fixture.engine.setCellNudge (source, channel, step, (step % 3 - 1) * 50);
            }
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    const auto render = [&] (int blockSize)
    {
        MidiPlaybackRunner runner;
        runner.prepare (48000.0);
        std::vector<std::tuple<int, int, int>> hits;
        for (int sample = 0; sample < 200000;)
        {
            const int size = std::min (blockSize, 200000 - sample);
            const auto result = runPlaybackBlock (runner, snapshot, static_cast<double> (sample) / 24000.0, 120.0, true, size);
            for (const auto event : result.midi)
                if (event.getMessage().isNoteOn())
                    hits.emplace_back (sample + event.samplePosition, event.getMessage().getNoteNumber(), event.getMessage().getVelocity());
            sample += size;
        }
        return hits;
    };
    const auto small = render (257);
    REQUIRE_FALSE (small.empty());
    CHECK (render (8189) == small);
}

TEST_CASE ("early loop-start cells wait for the host wrap without duplicate hits", "[engine][transport][nudge]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setTempo (120.0);
    fixture.engine.setRate ("16n");
    fixture.engine.setCell (0, 0, 0, true, 100);
    fixture.engine.setCellNudge (0, 0, 0, -50);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    std::vector<int> ons;
    constexpr int loopSamples = 24000;
    const HostLoopRange loop { true, 0.0, 1.0 };
    for (int sample = 0; sample < loopSamples * 3; sample += 300)
    {
        juce::MidiBuffer midi;
        runner.processBlock (snapshot, static_cast<double> (sample % loopSamples) / 24000.0,
                             120.0, true, 300, midi, {}, loop);
        for (const auto event : midi)
            if (event.getMessage().isNoteOn())
                ons.push_back (sample + event.samplePosition);
    }
    CHECK (ons == std::vector<int> { 0, 24000, 48000 });
}

TEST_CASE ("deferred roll notes and gates keep exact sample timing across buffer boundaries", "[engine][transport][realtime]")
{
    const int blockSize = GENERATE (64, 128, 512, 1024, 3000);
    const bool variableBlocks = GENERATE (false, true);
    CAPTURE (blockSize, variableBlocks);
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setStepCount (16);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setTempo (120.0);
    fixture.engine.setRate ("16n");
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1, 0, false, 2);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    std::vector<int> ons, offs;
    int block = 0;
    for (int sample = 0; sample < 7500; ++block)
    {
        const int size = variableBlocks && block % 2 != 0 ? 73 : blockSize;
        const auto result = runPlaybackBlock (runner, snapshot, static_cast<double> (sample) / 24000.0, 120.0, true, size);
        for (const auto event : result.midi)
        {
            const auto message = event.getMessage();
            if (message.isNoteOn())
                ons.push_back (sample + event.samplePosition);
            else if (message.isNoteOff())
                offs.push_back (sample + event.samplePosition);
        }
        sample += size;
    }
    REQUIRE (ons == std::vector<int> { 0, 3000 });
    REQUIRE (offs == std::vector<int> { 2688, 5688 });
}

TEST_CASE ("deferred swing and humanize timing matches a single block", "[engine][transport][realtime]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setStepCount (16);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setTempo (120.0);
    fixture.engine.setRate ("16n");
    fixture.engine.setSwing (100);
    fixture.engine.setTimingHumanize (80);
    fixture.engine.setCell (0, 0, 1, true, 100, 100, 1, 0, false, 4);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    auto timestamps = [&] (int blockSize) {
        MidiPlaybackRunner runner;
        runner.prepare (48000.0);
        std::vector<std::pair<int, bool>> events;
        for (int sample = 0; sample < 18000; sample += blockSize)
        {
            const auto result = runPlaybackBlock (runner, snapshot, static_cast<double> (sample) / 24000.0, 120.0, true, blockSize);
            for (const auto event : result.midi)
                events.emplace_back (sample + event.samplePosition, event.getMessage().isNoteOn());
        }
        return events;
    };
    const auto expected = timestamps (18000);
    REQUIRE (expected.size() == 8);
    REQUIRE (timestamps (64) == expected);
    REQUIRE (timestamps (512) == expected);
    REQUIRE (timestamps (3000) == expected);
}

TEST_CASE ("triangular timing humanize clusters near each swung or straight hit including rolls", "[engine][transport][realtime][humanize]")
{
    const int swing = GENERATE (0, 30);
    const int roll = GENERATE (1, 2);
    const int nudge = GENERATE (0, -100);
    CAPTURE (swing, roll, nudge);
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setStepCount (16);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setTempo (120.0);
    fixture.engine.setRate ("16n");
    fixture.engine.setSwing (swing);
    fixture.engine.setTimingHumanize (40);
    for (int step = 0; step < 16; ++step)
    {
        fixture.engine.setCell (0, 0, step, true, 100, 100, 1, 1, roll);
        fixture.engine.setCellNudge (0, 0, step, nudge);
    }
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    constexpr int stepSamples = 6000;
    constexpr int stepCount = 128;
    const auto render = [&] (int blockSize)
    {
        MidiPlaybackRunner runner;
        runner.prepare (48000.0);
        std::vector<int> hits;
        for (int sample = 0; sample < (stepCount + 2) * stepSamples; sample += blockSize)
        {
            juce::MidiBuffer midi;
            runner.processBlock (snapshot, static_cast<double> (sample) / 24000.0,
                                 120.0, true, blockSize, midi, {}, {}, 0.0, stepCount);
            for (const auto event : midi)
                if (event.getMessage().isNoteOn())
                    hits.push_back (sample + event.samplePosition);
        }
        return hits;
    };
    const auto hits = render (64);
    REQUIRE (hits.size() == static_cast<size_t> (stepCount * roll));
    CHECK (render (8189) == hits);
    for (int rollIndex = 0; rollIndex < roll; ++rollIndex)
    {
        int early = 0, late = 0, insideHalfRange = 0;
        double sum = 0.0;
        for (int step = 2; step < stepCount; ++step)
        {
            const double position = step + static_cast<double> (rollIndex) / roll;
            const double center = (position + swingDelayStepsForPosition (position, swing, snapshot.swingSubdivisionIndex)
                                   + static_cast<double> (nudge) / 100.0) * stepSamples;
            const double offset = hits[static_cast<size_t> (step * roll + rollIndex)] - center;
            CHECK (std::abs (offset) <= 480.5);
            early += offset < -0.5 ? 1 : 0;
            late += offset > 0.5 ? 1 : 0;
            insideHalfRange += std::abs (offset) <= 240.0 ? 1 : 0;
            sum += offset;
        }
        // A fixed random phase exercises both halves without requiring exact equal counts.
        CHECK (early > 40);
        CHECK (late > 40);
        const double innerShare = static_cast<double> (insideHalfRange) / (stepCount - 2);
        CHECK (std::abs (innerShare - 0.75) < 0.12);
        CHECK (std::abs (sum / (stepCount - 2)) < 75.0);
    }
}

TEST_CASE ("humanized early hits respect host loop boundaries regardless of buffer size", "[engine][transport][humanize]")
{
    const int nudge = GENERATE (0, -100);
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setTempo (120.0);
    fixture.engine.setRate ("16n");
    fixture.engine.setTimingHumanize (100);
    for (int step = 0; step < 4; ++step)
    {
        fixture.engine.setCell (0, 0, step, true, 100);
        fixture.engine.setCellNudge (0, 0, step, nudge);
    }
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    const auto render = [&] (int blockSize, bool looping)
    {
        MidiPlaybackRunner runner;
        runner.prepare (48000.0);
        std::vector<int> hits;
        constexpr int loopSamples = 24000;
        for (int sample = 0; sample < loopSamples * 8;)
        {
            const int count = std::min (blockSize, loopSamples - sample % loopSamples);
            juce::MidiBuffer midi;
            auto state = snapshot;
            if (! looping)
                state.stepCount = 4;
            runner.processBlock (state, static_cast<double> (looping ? sample % loopSamples : sample) / 24000.0,
                                 120.0, true, count, midi, {}, { looping, 0.0, 1.0 }, 0.0,
                                 looping ? std::nullopt : std::optional<int> { 32 });
            for (const auto event : midi)
                if (event.getMessage().isNoteOn())
                    hits.push_back (sample + event.samplePosition);
            sample += count;
        }
        return hits;
    };
    auto expected = render (64, false);
    REQUIRE (expected.size() == 32);
    for (size_t i = 0; i < expected.size(); ++i)
        expected[i] = std::max (expected[i], static_cast<int> (i / 4) * 24000);
    CHECK (render (64, true) == expected);
    CHECK (render (3000, true) == expected);
}

TEST_CASE ("host loop downbeats never anticipate the wrap or disappear between callbacks", "[engine][transport][humanize][loop-boundary]")
{
    const int loopSteps = GENERATE (1, 4, 16);
    const int blockSize = GENERATE (64, 512, 700, 4096);
    const int nudge = GENERATE (0, -100);
    CAPTURE (loopSteps, blockSize, nudge);
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setStepCount (loopSteps);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setTempo (120.0);
    fixture.engine.setRate ("16n");
    fixture.engine.setTimingHumanize (100);
    fixture.engine.setCell (0, 0, 0, true, 100);
    fixture.engine.setCellNudge (0, 0, 0, nudge);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    const int loopSamples = loopSteps * 6000;
    const auto render = [&] (bool looping)
    {
        MidiPlaybackRunner runner;
        runner.prepare (48000.0);
        std::vector<int> hits;
        for (int sample = 0; sample < loopSamples * 8; sample += blockSize)
        {
            juce::MidiBuffer midi;
            const int count = std::min (blockSize, loopSamples * 8 - sample);
            runner.processBlock (snapshot, static_cast<double> (looping ? sample % loopSamples : sample) / 24000.0,
                                 120.0, true, count, midi, {}, { looping, 0.0, loopSteps * 0.25 }, 0.0,
                                 looping ? std::nullopt : std::optional<int> { loopSteps * 8 });
            for (const auto event : midi)
                if (event.getMessage().isNoteOn())
                    hits.push_back (sample + event.samplePosition);
        }
        return hits;
    };
    auto expected = render (false);
    REQUIRE (expected.size() == 8);
    for (size_t i = 0; i < expected.size(); ++i)
        expected[i] = std::max (expected[i], static_cast<int> (i) * loopSamples);
    CHECK (render (true) == expected);
}

TEST_CASE ("the initial host loop downbeat cannot be humanized into the lead-in", "[engine][transport][humanize][loop-boundary]")
{
    const double loopStart = GENERATE (0.0, 4.0);
    const int blockSize = GENERATE (64, 512, 700);
    CAPTURE (loopStart, blockSize);
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setStepCount (4);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setTempo (120.0);
    fixture.engine.setRate ("16n");
    fixture.engine.setVelocityHumanize (11);
    fixture.engine.setTimingHumanize (100);
    fixture.engine.setCell (0, 0, 0, true, 100);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    const auto render = [&] (bool reportLoop)
    {
        MidiPlaybackRunner runner;
        runner.prepare (48000.0);
        std::vector<int> hits;
        for (int sample = 0; sample < 12000; sample += blockSize)
        {
            juce::MidiBuffer midi;
            runner.processBlock (snapshot, loopStart - 0.25 + static_cast<double> (sample) / 24000.0,
                                 120.0, true, blockSize, midi, {}, { reportLoop, loopStart, loopStart + 1.0 });
            for (const auto event : midi)
                if (event.getMessage().isNoteOn())
                    hits.push_back (sample + event.samplePosition);
        }
        return hits;
    };
    const auto unbounded = render (false);
    REQUIRE (unbounded.size() == 1);
    REQUIRE (unbounded.front() < 6000);
    CHECK (render (true) == std::vector<int> { 6000 });
}

TEST_CASE ("audition note gate expires at its exact sample deadline", "[engine][transport][realtime]")
{
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    runner.queueAuditionNote ({ 42, 100, 1, 100, 0.0 });
    PlaybackSnapshot snapshot;
    std::vector<int> offs;
    int sample = 0;
    for (const int blockSize : { 512, 128, 1024, 64, 2048, 2048 })
    {
        const auto result = runPlaybackBlock (runner, snapshot, 0.0, 120.0, false, blockSize);
        for (const auto event : result.midi)
            if (event.getMessage().isNoteOff())
                offs.push_back (sample + event.samplePosition);
        sample += blockSize;
    }
    REQUIRE (offs == std::vector<int> { 4800 });
}

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
        for (size_t hit = 0; hit < result.noteHitCount; ++hit)
            CHECK (result.noteHits[hit].uiCycleStep == 3);
    }

    REQUIRE (hitSteps == std::vector<int> { 18, 19, 20, 21 });
}

TEST_CASE ("cycle feedback reports inactive turns without emitting notes", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 3, 0b101);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    for (int turn = 0; turn < 6; ++turn)
    {
        const auto result = runPlaybackBlock (runner, snapshot, turn * snapshot.beatsPerStep, 120.0, true, 512);
        REQUIRE (result.cycleVisitCount == 1);
        const auto& visit = result.cycleVisits[0];
        CHECK (visit.source == 1);
        CHECK (visit.channel == 1);
        CHECK (visit.step == 1);
        CHECK (visit.cycleStep == turn % 3 + 1);
        CHECK (countNoteOns (result.midi) == (turn % 3 == 1 ? 0 : 1));
        CHECK (result.noteHitCount == (turn % 3 == 1 ? 0 : 1));
    }
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

TEST_CASE ("midi playback preserves cycle phases across fresh snapshots", "[engine][transport]")
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
    const auto jumped = runPlaybackBlock (runner, snapshot, 0.8, 120.0, true, 512);

    REQUIRE (countNoteOns (jumped.midi) == 0);
    REQUIRE (jumped.currentStepOneBased == 4);

    const auto nextBoundary = runPlaybackBlock (runner, snapshot, 1.0, 120.0, true, 512);
    REQUIRE (countNoteOns (nextBoundary.midi) == 1);
    REQUIRE (nextBoundary.currentStepOneBased == 1);
}

TEST_CASE ("midi playback emits the landing step when a jump lands on its boundary", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.engine.setCell (0, 0, 3, true, 100, 100, 1);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    [[maybe_unused]] const auto first = runPlaybackBlock (runner, snapshot, 0.0, 120.0, true, 512);
    const auto jumped = runPlaybackBlock (runner, snapshot, 0.75, 120.0, true, 512);

    REQUIRE (countNoteOns (jumped.midi) == 1);
    REQUIRE (jumped.noteHitCount == 1);
    REQUIRE (jumped.noteHits[0].uiGeneratedStep == 4);
}

namespace
{
struct LoopRunEvents
{
    int kicks = 0;
    int snares = 0;
};

// Plays a continuous transport that the "host" wraps from loopEnd to loopStart
// without splitting the block at the wrap point.
LoopRunEvents runHostLoop (const PlaybackSnapshot& snapshot, const HostLoopRange& reportedLoop, double loopEnd, int passes)
{
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    constexpr int blockSize = 700;
    const double beatsPerBlock = static_cast<double> (blockSize) / 24000.0;
    const double loopLength = loopEnd;
    LoopRunEvents events;
    double position = 0.0;

    while (position < loopLength * passes)
    {
        juce::MidiBuffer midi;
        runner.processBlock (snapshot, std::fmod (position, loopLength), 120.0, true, blockSize, midi, {}, reportedLoop);

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOn() && message.getNoteNumber() == 36)
                ++events.kicks;
            else if (message.isNoteOn() && message.getNoteNumber() == 38)
                ++events.snares;
        }

        position += beatsPerBlock;
    }

    return events;
}
} // namespace

TEST_CASE ("host loop wraps play the loop-start step every pass and never the loop-end step", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (2);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setChannelNote (0, 36);
    fixture.engine.setChannelNote (1, 38);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.engine.setCell (0, 1, 4, true, 100, 100, 1);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();

    const auto events = runHostLoop (snapshot, { true, 0.0, 1.0 }, 1.0, 6);

    REQUIRE (events.kicks == 6);
    REQUIRE (events.snares == 0);
}

TEST_CASE ("host loop wraps keep cycle phases running", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setChannelNote (0, 36);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 2, 0b10);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();

    const auto events = runHostLoop (snapshot, { true, 0.0, 1.0 }, 1.0, 6);

    REQUIRE (events.kicks == 3);
}

TEST_CASE ("Changing the live rate rebases scheduling at the new grid", "[engine][transport][rate-change]")
{
    const bool faster = GENERATE (true, false);
    const bool wrapping = GENERATE (true, false);
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setTempo (120.0);
    fixture.engine.setRate (faster ? "16n" : "32n");
    for (int step = 0; step < 16; ++step)
        fixture.engine.setCell (0, 0, step, true, 100, 100, 1);

    auto snapshot = std::make_unique<PlaybackSnapshot> (fixture.engine.makePlaybackSnapshot());
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    constexpr int samples = 512;
    constexpr double blockBeats = samples / 24000.0;
    const HostLoopRange loop { wrapping, 384.0, 400.0 };
    juce::MidiBuffer midi;
    runner.processBlock (*snapshot, 400.0 - blockBeats, 120.0, true, samples, midi, {}, loop);
    REQUIRE (countNoteOns (midi) == 1);

    fixture.engine.setRate (faster ? "32n" : "16n");
    *snapshot = fixture.engine.makePlaybackSnapshot();
    const double landing = wrapping ? loop.startPpq : 400.0;
    midi.clear();
    runner.processBlock (*snapshot, landing, 120.0, true, samples, midi, {}, loop);
    REQUIRE (countNoteOns (midi) == 1);
    auto event = midi.begin();
    CHECK ((*event).getMessage().isNoteOff());
    ++event;
    REQUIRE (event != midi.end());
    CHECK ((*event).getMessage().isNoteOn());
    CHECK ((*event).samplePosition == 0);

    midi.clear();
    runner.processBlock (*snapshot, landing + blockBeats, 120.0, true, samples, midi, {}, loop);
    CHECK (countNoteOns (midi) == 0);
}

TEST_CASE ("A rate change mid-step discards deferred hits from the old grid", "[engine][transport][rate-change]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setTempo (120.0);
    fixture.engine.setRate ("16n");
    fixture.engine.setCell (0, 0, 0, Cell { .enabled = true, .roll = 4 });
    auto snapshot = std::make_unique<PlaybackSnapshot> (fixture.engine.makePlaybackSnapshot());
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    CHECK (countNoteOns (runPlaybackBlock (runner, *snapshot, 0.0, 120.0, true, 1024).midi) == 1);

    fixture.engine.setRate ("32n");
    *snapshot = fixture.engine.makePlaybackSnapshot();
    const auto after = runPlaybackBlock (runner, *snapshot, 1024.0 / 24000.0, 120.0, true, 1900);
    CHECK (countNoteOns (after.midi) == 0);
}

TEST_CASE ("midi playback releases held notes on stop, seek, and device off", "[engine][transport][realtime]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();

    const auto expectReleasedAfter = [&snapshot] (auto&& secondBlock)
    {
        MidiPlaybackRunner runner;
        runner.prepare (48000.0);
        const auto first = runPlaybackBlock (runner, snapshot, 0.0, 120.0, true, 256);
        REQUIRE (containsNoteOn (first.midi, 36));

        juce::MidiBuffer midi;
        secondBlock (runner, midi);
        bool released = false;

        for (const auto metadata : midi)
            released = released || (metadata.getMessage().isNoteOff() && metadata.getMessage().getNoteNumber() == 36);

        REQUIRE (released);
    };

    SECTION ("transport stop")
    {
        expectReleasedAfter ([&snapshot] (MidiPlaybackRunner& runner, juce::MidiBuffer& midi)
                             { runner.processBlock (snapshot, 0.0107, 120.0, false, 256, midi); });
    }

    SECTION ("seek")
    {
        expectReleasedAfter ([&snapshot] (MidiPlaybackRunner& runner, juce::MidiBuffer& midi)
                             { runner.processBlock (snapshot, 2.6, 120.0, true, 256, midi); });
    }

    SECTION ("device off")
    {
        auto inactive = snapshot;
        inactive.deviceActive = false;
        expectReleasedAfter ([&inactive] (MidiPlaybackRunner& runner, juce::MidiBuffer& midi)
                             { runner.processBlock (inactive, 0.0107, 120.0, true, 256, midi); });
    }

    SECTION ("suspend")
    {
        expectReleasedAfter ([] (MidiPlaybackRunner& runner, juce::MidiBuffer& midi) { runner.suspend (midi); });
    }
}

TEST_CASE ("transport cancellation drops deferred rolls before emitting them", "[engine][transport][realtime]")
{
    const int blockSize = GENERATE (64, 128, 512, 1024);
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setStepCount (16);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setTempo (120.0);
    fixture.engine.setRate ("16n");
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1, 0, false, 2);
    auto snapshot = fixture.engine.makePlaybackSnapshot();
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);

    // The second roll hit is due at sample 3000, inside the next callback.
    const int cancelSample = 3000 - blockSize / 2;
    const auto first = runPlaybackBlock (runner, snapshot, 0.0, 120.0, true, cancelSample);
    REQUIRE (countNoteOns (first.midi) == 1);
    double ppq = static_cast<double> (cancelSample) / 24000.0;
    double bpm = 120.0;
    bool playing = true;

    SECTION ("stop") { playing = false; }
    SECTION ("device off") { snapshot.deviceActive = false; }
    SECTION ("invalid tempo") { bpm = 0.0; }
    SECTION ("seek to the middle of an empty step") { ppq = 2.6; }
    SECTION ("bypass")
    {
        juce::MidiBuffer released;
        runner.suspend (released);
        REQUIRE (countNoteOns (released) == 0);
        playing = false;
    }

    for (int block = 0; block < 8; ++block)
    {
        CAPTURE (blockSize, block);
        const auto result = runPlaybackBlock (runner, snapshot, ppq, bpm, playing, blockSize);
        REQUIRE (countNoteOns (result.midi) == 0);
        REQUIRE (result.noteHitCount == 0);
        ppq += static_cast<double> (blockSize) / 48000.0 * bpm / 60.0;
    }
}

TEST_CASE ("midi playback generates refresh windows on the audio thread deterministically", "[engine][transport][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setGeneratedStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setRefreshSteps (4);
    fixture.engine.setGenerationMode (GenerationMode::stack);

    for (int step = 0; step < 8; ++step)
    {
        fixture.engine.setCell (0, 0, step, true, 40, 100, 1);
        fixture.engine.setCell (1, 0, step, true, 90, 100, 1);
    }

    const auto snapshot = fixture.engine.makePlaybackSnapshot();

    const auto render = [&snapshot]
    {
        MidiPlaybackRunner runner;
        runner.prepare (48000.0);
        std::vector<std::pair<int, int>> sourcesByStep;
        int boundaryFrames = 0;

        for (int step = 0; step < 64; ++step)
        {
            const auto result = runPlaybackBlock (runner, snapshot, static_cast<double> (step) * 0.25, 120.0, true, 512);

            if (result.generatedSourcesChanged)
            {
                ++boundaryFrames;
                const auto frame = runner.generatedSourcesFrame();
                REQUIRE (result.noteHitCount == 1);
                REQUIRE (frame.sources[0][static_cast<size_t> (step % 8)] + 1 == result.noteHits[0].uiSource);
            }

            for (size_t hit = 0; hit < result.noteHitCount; ++hit)
                sourcesByStep.emplace_back (step, result.noteHits[hit].uiSource);
        }

        REQUIRE (boundaryFrames == 16);
        return sourcesByStep;
    };

    const auto first = render();
    REQUIRE (first.size() == 64);
    REQUIRE (first == render());

    std::set<int> sourcesUsed;
    for (const auto& [step, source] : first)
        sourcesUsed.insert (source);

    REQUIRE (sourcesUsed == std::set<int> { 1, 2 });
}

TEST_CASE ("Generated playback wraps independently while each source keeps its own loop", "[engine][transport][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    auto& engine = fixture.engine;
    engine.setStepCount (4);
    engine.setSourceStepCount (1, 7);
    engine.setChannelCount (2);
    engine.setGeneratedStepCount (11);
    engine.setRefreshSteps (5);
    for (int step = 0; step < 4; ++step)
        engine.setCell (0, 0, step, Cell { .enabled = true, .velocity = 40 + step });
    for (int step = 0; step < 7; ++step)
        engine.setCell (1, 1, step, Cell { .enabled = true, .velocity = 80 + step });
    engine.setGenerationMode (GENERATE (GenerationMode::stack, GenerationMode::perChannel));
    engine.setChannelLock (0, 0);
    engine.setChannelLock (1, 1);
    auto snapshot = engine.makePlaybackSnapshot();
    CHECK (snapshot.generated[0][10].velocity == 42);
    CHECK (snapshot.generated[1][10].velocity == 83);

    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    for (int step = 0; step < 23; ++step)
    {
        if (step == 6)
        {
            engine.setStaticSource (1);
            snapshot = engine.makePlaybackSnapshot();
            CHECK (snapshot.stepCount == 7);
            CHECK (snapshot.refreshSteps == 5);
        }
        CAPTURE (step);
        const int outputStep = step % 11;
        const auto result = runPlaybackBlock (runner, snapshot, step * snapshot.beatsPerStep, 120.0, true, 512);
        CHECK (result.currentStepOneBased == outputStep + 1);
        CHECK (result.generatedSourcesChanged == (outputStep % 5 == 0));
        REQUIRE (result.noteHitCount == 2);
        CHECK (result.noteHits[0].uiGeneratedStep == outputStep + 1);
        CHECK (result.noteHits[0].uiSourceStep == (outputStep % 4) + 1);
        CHECK (result.noteHits[1].uiGeneratedStep == outputStep + 1);
        CHECK (result.noteHits[1].uiSourceStep == (outputStep % 7) + 1);
    }
}

TEST_CASE ("engine mirrors audio-thread generated sources only for its current generation", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRefreshSteps (4);
    fixture.engine.setGenerationMode (GenerationMode::stack);
    fixture.engine.setCell (0, 0, 0, true, 40, 100, 1);
    fixture.engine.setCell (3, 0, 0, true, 90, 100, 1);
    fixture.setRandomValues ({ 0.0 });
    fixture.engine.generateWindow (0, 4, false);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == 0);

    GeneratedSourcesFrame frame;
    frame.basisVersion = fixture.engine.generationVersion();
    for (auto& step : frame.sources[0])
        step = 3;

    REQUIRE (fixture.engine.adoptGeneratedSources (frame));
    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == 3);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 90);
    REQUIRE (fixture.engine.makePlaybackSnapshot().generationVersion == frame.basisVersion);

    fixture.engine.generateWindow (0, 4, false);
    for (auto& step : frame.sources[0])
        step = 0;

    REQUIRE_FALSE (fixture.engine.adoptGeneratedSources (frame));
}

TEST_CASE ("editing one cell keeps the cycle phase of every other cell", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (2);
    fixture.engine.setChannelCount (2);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setChannelNote (0, 36);
    fixture.engine.setChannelNote (1, 38);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 3, 0b001);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);
    auto snapshot = fixture.engine.makePlaybackSnapshot();
    std::vector<int> hitSteps;

    for (int step = 0; step <= 8; ++step)
    {
        if (step == 3)
        {
            fixture.engine.setCell (0, 1, 1, true, 80, 100, 1);
            snapshot = fixture.engine.makePlaybackSnapshot();
        }

        const auto result = runPlaybackBlock (runner, snapshot, static_cast<double> (step) * 0.25, 120.0, true, 512);

        if (containsNoteOn (result.midi, 36))
            hitSteps.push_back (step);
    }

    REQUIRE (hitSteps == std::vector<int> { 0, 6 });
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
    fixture.engine.setSwingSubdivisionIndex (0);
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

    fixture.engine.setSwingSubdivisionIndex (1);
    ksh::MidiPlaybackRunner coarseRunner;
    coarseRunner.prepare (44100.0);
    const auto coarseSnapshot = fixture.engine.makePlaybackSnapshot();
    [[maybe_unused]] const auto coarseStep0 = runPlaybackBlock (coarseRunner,
                                                               coarseSnapshot,
                                                               0.0,
                                                               120.0,
                                                               true,
                                                               512);
    const auto coarseStep1 = runPlaybackBlock (coarseRunner,
                                             coarseSnapshot,
                                             0.25,
                                             120.0,
                                             true,
                                             4096);

    REQUIRE (coarseStep1.noteHitCount > 0);
    REQUIRE (coarseStep1.noteHits[0].delayMs == 0.0);
}

TEST_CASE ("midi playback does not fire while transport is stopped", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

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

TEST_CASE ("midi M selection mutes beat one immediately", "[engine][transport]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setStaticSource (15);
    fixture.engine.setCell (15, 0, 0, true, 100, 100, 1);
    fixture.engine.generateWindow (0, 16, true);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);

    juce::MidiBuffer midi;
    MidiPatternSelectionBlock selections;
    selections.add (0, Constants::mutedStaticSource);
    const auto result = runner.processBlock (fixture.engine.makePlaybackSnapshot(),
                                             0.0,
                                             120.0,
                                             true,
                                             512,
                                             midi,
                                             selections);

    REQUIRE (countNoteOns (midi) == 0);
    REQUIRE (result.noteHitCount == 0);
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
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1 }));
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

    REQUIRE (fixture.engine.isDeviceActive());

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

TEST_CASE ("midi playback follows each channel playback mode inside its loop range", "[engine][transport]")
{
    struct ModeCase
    {
        PlaybackMode mode;
        int stepCount, loopLength, loopStart;
        std::vector<int> expectedVelocities;
    };
    const auto modeCase = GENERATE (values<ModeCase> ({
        { PlaybackMode::reverse, 4, 4, 0, { 40, 30, 20, 10, 40 } },
        { PlaybackMode::reverse, 8, 3, 2, { 50, 40, 30, 50 } },
        { PlaybackMode::ping_pong, 4, 3, 0, { 10, 20, 30, 30, 20, 10, 10 } },
        { PlaybackMode::reverse_ping_pong, 4, 3, 0, { 30, 20, 10, 10, 20, 30, 30 } },
        { PlaybackMode::normal, 16, 10, 2, { 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 30, 40 } },
    }));

    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (modeCase.stepCount);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    for (int step = 0; step < modeCase.stepCount; ++step)
        fixture.engine.setCell (0, 0, step, true, std::min (127, 10 * (step + 1)), 100, 1);
    fixture.engine.setChannelLoopLength (0, modeCase.loopLength, modeCase.loopStart);
    fixture.engine.setChannelPlaybackMode (0, modeCase.mode);
    fixture.engine.generateWindow (0, modeCase.stepCount, true);

    ksh::MidiPlaybackRunner runner;
    runner.prepare (44100.0);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    std::vector<int> velocities;

    for (size_t step = 0; step < modeCase.expectedVelocities.size(); ++step)
    {
        const auto result = runPlaybackBlock (runner, snapshot, static_cast<double> (step) * snapshot.beatsPerStep, 120.0, true, 512);
        for (const auto metadata : result.midi)
            if (metadata.getMessage().isNoteOn())
                velocities.push_back (metadata.getMessage().getVelocity());
    }

    CHECK (velocities == modeCase.expectedVelocities);
}

TEST_CASE ("Timestamped keyboard monitoring keeps phase with a consistent block delay", "[engine][record-timing][realtime]")
{
    const int blockSize = GENERATE (64, 128, 256, 512, 1024);
    const int phase = GENERATE (0, 1, 31, 63);
    EngineFixture fixture;
    fixture.clearAll();
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    const double eventMs = 1000.0 + phase / 48.0;
    runner.queueAuditionNote ({ 36, 100, 1, 100, 0.0, eventMs });
    juce::MidiBuffer midi;
    runner.processBlock (snapshot, 0.0, 120.0, false, blockSize, midi, {}, {}, 1000.0 + blockSize / 48.0);
    int onset = -1;
    for (const auto event : midi)
        if (event.getMessage().isNoteOn())
            onset = event.samplePosition;
    CHECK (onset == phase);
    // In the synthetic wall clock, output begins at the current callback, exactly
    // one block after the preceding callback. Arrival phase must not alter latency.
    CHECK (blockSize + onset - phase == blockSize);
}

TEST_CASE ("Swing targets follow the displayed multiples of the current grid division", "[engine][transport][swing]")
{
    const int target = GENERATE (0, 1);
    const auto rate = GENERATE ("4n", "8n", "16n", "16nt");
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate (rate);
    fixture.engine.setTempo (120.0);
    fixture.engine.setSwing (100);
    fixture.engine.setSwingSubdivisionIndex (target);
    for (int step = 0; step < 16; ++step)
        fixture.engine.setCell (0, 0, step, true, 100, 100, 1);
    const auto snapshot = fixture.engine.makePlaybackSnapshot();
    const double samplesPerStep = snapshot.beatsPerStep * 24000.0;
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    const auto result = runPlaybackBlock (runner, snapshot, 0.0, 120.0, true,
                                          static_cast<int> (samplesPerStep * 16));
    std::vector<int> actual;
    for (const auto event : result.midi)
        if (event.getMessage().isNoteOn())
            actual.push_back (event.samplePosition);
    std::vector<int> expected;
    const int group = 1 << target;
    for (int step = 0; step < 16; ++step)
    {
        const double delay = (step / group) % 2 == 1 ? group * 0.5 : 0.0;
        const int sample = static_cast<int> (std::lround ((step + delay) * samplesPerStep));
        if (sample < static_cast<int> (samplesPerStep * 16))
            expected.push_back (sample);
    }
    std::sort (expected.begin(), expected.end());
    CHECK (actual == expected);
}
