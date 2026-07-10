#include "EngineTestHelpers.h"

#include <catch2/catch_test_macros.hpp>

using namespace ksh;
using ksh::test::EngineFixture;

TEST_CASE ("stack mode matches one source across window", "[engine][generation]")
{
    EngineFixture fixture { { 0.5 } };
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::stack);
    fixture.engine.setCell (0, 0, 0, true, 10, 100, 1);
    fixture.engine.setCell (0, 0, 1, false, 10, 100, 1);
    fixture.engine.setCell (1, 0, 0, false, 10, 100, 1);
    fixture.engine.setCell (1, 0, 1, true, 20, 100, 1);
    fixture.setRandomValues ({ 0.5 });
    fixture.engine.generateWindow (0, 4, true);

    for (int step = 0; step < 4; ++step)
        REQUIRE (fixture.engine.generatedCellAt (0, step).source == 1);

    REQUIRE_FALSE (fixture.engine.generatedCellAt (0, 0).enabled);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 10);
    REQUIRE (fixture.engine.generatedCellAt (0, 1).enabled);
    REQUIRE (fixture.engine.generatedCellAt (0, 1).velocity == 20);
}

TEST_CASE ("stack mode uses one source for all channels on step", "[engine][generation]")
{
    EngineFixture fixture { { 0.76 } };
    fixture.clearAll();
    fixture.engine.setChannelCount (2);
    fixture.engine.setGenerationMode (GenerationMode::stack);
    fixture.engine.setCell (3, 0, 0, true, 111, 100, 1);
    fixture.engine.setCell (3, 1, 0, true, 88, 100, 1);
    fixture.setRandomValues ({ 0.76 });
    fixture.engine.generateWindow (0, fixture.engine.getStepCount(), true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == 3);
    REQUIRE (fixture.engine.generatedCellAt (1, 0).source == 3);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 111);
    REQUIRE (fixture.engine.generatedCellAt (1, 0).velocity == 88);
}

TEST_CASE ("per_channel mode can choose different sources", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (2);
    fixture.engine.setGenerationMode (GenerationMode::perChannel);
    fixture.engine.setCell (0, 0, 0, true, 70, 100, 1);
    fixture.engine.setCell (3, 1, 0, true, 90, 100, 1);
    fixture.setRandomValues ({ 0.0, 0.76 });
    fixture.engine.generateWindow (0, fixture.engine.getStepCount(), true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == 0);
    REQUIRE (fixture.engine.generatedCellAt (1, 0).source == 3);
}

TEST_CASE ("static mode uses selected source", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (2);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setStaticSource (2);
    fixture.engine.setChannelLock (0, 0);
    fixture.engine.setCell (0, 0, 0, true, 40, 100, 1);
    fixture.engine.setCell (2, 0, 0, true, 70, 100, 1);
    fixture.engine.setCell (2, 1, 0, true, 90, 100, 1);
    fixture.engine.generateWindow (0, fixture.engine.getStepCount(), true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == 2);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 70);
    REQUIRE (fixture.engine.generatedCellAt (1, 0).source == 2);
    REQUIRE (fixture.engine.generatedCellAt (1, 0).velocity == 90);
}

TEST_CASE ("static mode muted source generates silence", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setStaticSource (Constants::mutedStaticSource);
    fixture.engine.setCell (0, 0, 0, true, 70, 100, 1);
    fixture.engine.generateWindow (0, fixture.engine.getStepCount(), true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == Constants::mutedStaticSource);
    REQUIRE_FALSE (fixture.engine.generatedCellAt (0, 0).enabled);
}

TEST_CASE ("random source ignores empty sources", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::stack);
    fixture.engine.setCell (0, 0, 0, true, 55, 100, 1);
    fixture.setRandomValues ({ 0.99 });
    fixture.engine.generateWindow (0, fixture.engine.getStepCount(), true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == 0);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 55);
}

TEST_CASE ("inactive channel content does not make source active", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::stack);
    fixture.engine.setCell (0, 0, 0, true, 55, 100, 1);
    fixture.engine.setCell (1, 1, 0, true, 99, 100, 1);
    fixture.setRandomValues ({ 0.99 });
    fixture.engine.generateWindow (0, fixture.engine.getStepCount(), true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == 0);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 55);
}

TEST_CASE ("random source uses only populated source when others empty", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::perChannel);
    fixture.engine.setCell (2, 0, 0, true, 66, 100, 1);
    fixture.setRandomValues ({ 0.0, 0.99 });
    fixture.engine.generateWindow (0, fixture.engine.getStepCount(), true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == 2);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 66);
}

TEST_CASE ("source channel mute suppresses generated output", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::perChannel);
    fixture.engine.setCell (0, 0, 0, true, 77, 100, 1);
    fixture.engine.setSourceChannelMute (0, 0, true);
    fixture.setRandomValues ({ 0.0 });
    fixture.engine.generateWindow (0, fixture.engine.getStepCount(), true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == 0);
    REQUIRE_FALSE (fixture.engine.generatedCellAt (0, 0).enabled);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 100);
}

TEST_CASE ("muted source channel does not make source active", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::stack);
    fixture.engine.setCell (0, 0, 0, true, 55, 100, 1);
    fixture.engine.setCell (1, 0, 0, true, 88, 100, 1);
    fixture.engine.setSourceChannelMute (1, 0, true);
    fixture.setRandomValues ({ 0.99 });
    fixture.engine.generateWindow (0, fixture.engine.getStepCount(), true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == 0);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 55);
}

TEST_CASE ("source channel reset clears cells and mute", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setChannelLoopLength (0, 3);
    fixture.engine.setCell (0, 0, 0, true, 44, 30, 3);
    fixture.engine.setCell (0, 0, 1, true, 45, 40, 4);
    fixture.engine.setSourceChannelMute (0, 0, true);
    fixture.engine.resetSourceChannel (0, 0);

    REQUIRE_FALSE (fixture.engine.sourceChannelMutedAt (0, 0));
    REQUIRE (fixture.engine.channelAt (0).loopLength == 8);
    REQUIRE_FALSE (fixture.engine.sourceCellAt (0, 0, 0).enabled);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 0).velocity == 100);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 0).probability == 100);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 0).cycle == 1);
    REQUIRE_FALSE (fixture.engine.sourceCellAt (0, 0, 1).enabled);
}

TEST_CASE ("channel loop length wraps source step lookup", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setStaticSource (0);
    fixture.engine.setChannelLoopLength (0, 3);
    fixture.engine.setCell (0, 0, 0, true, 10, 100, 1);
    fixture.engine.setCell (0, 0, 1, true, 20, 100, 1);
    fixture.engine.setCell (0, 0, 2, true, 30, 100, 1);
    fixture.engine.setCell (0, 0, 3, true, 99, 100, 1);
    fixture.engine.generateWindow (0, 8, true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 10);
    REQUIRE (fixture.engine.generatedCellAt (0, 1).velocity == 20);
    REQUIRE (fixture.engine.generatedCellAt (0, 2).velocity == 30);
    REQUIRE (fixture.engine.generatedCellAt (0, 3).velocity == 10);
    REQUIRE (fixture.engine.generatedCellAt (0, 4).velocity == 20);
    REQUIRE (fixture.engine.generatedCellAt (0, 5).velocity == 30);
    REQUIRE (fixture.engine.generatedCellAt (0, 6).velocity == 10);
    REQUIRE (fixture.engine.generatedCellAt (0, 7).velocity == 20);
}

TEST_CASE ("channel loop range offsets wrapped source step lookup", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setStaticSource (0);
    fixture.engine.setChannelLoopLength (0, 3, 2);
    fixture.engine.setCell (0, 0, 0, true, 99, 100, 1);
    fixture.engine.setCell (0, 0, 2, true, 20, 100, 1);
    fixture.engine.setCell (0, 0, 3, true, 30, 100, 1);
    fixture.engine.setCell (0, 0, 4, true, 40, 100, 1);
    fixture.engine.generateWindow (0, 8, true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 30);
    REQUIRE (fixture.engine.generatedCellAt (0, 1).velocity == 40);
    REQUIRE (fixture.engine.generatedCellAt (0, 2).velocity == 20);
    REQUIRE (fixture.engine.generatedCellAt (0, 3).velocity == 30);
    REQUIRE (fixture.engine.generatedCellAt (0, 4).velocity == 40);
    REQUIRE (fixture.engine.generatedCellAt (0, 5).velocity == 20);
}

TEST_CASE ("channel loop length refreshes all wrapped generated cells", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setStaticSource (0);
    fixture.engine.setChannelLoopLength (0, 3);
    fixture.engine.setCell (0, 0, 0, true, 10, 100, 1);
    fixture.engine.setCell (0, 0, 1, true, 20, 100, 1);
    fixture.engine.setCell (0, 0, 2, true, 30, 100, 1);
    fixture.engine.generateWindow (0, 8, true);

    fixture.engine.setCellVelocity (0, 0, 0, 88);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 88);
    REQUIRE (fixture.engine.generatedCellAt (0, 3).velocity == 88);
    REQUIRE (fixture.engine.generatedCellAt (0, 6).velocity == 88);
    REQUIRE (fixture.engine.generatedCellAt (0, 1).velocity == 20);

    fixture.engine.setCellVelocity (0, 0, 5, 44);
    REQUIRE (fixture.engine.generatedCellAt (0, 5).velocity == 30);
}

TEST_CASE ("channel loop range refreshes all offset wrapped generated cells", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setStaticSource (0);
    fixture.engine.setChannelLoopLength (0, 3, 2);
    fixture.engine.setCell (0, 0, 2, true, 20, 100, 1);
    fixture.engine.setCell (0, 0, 3, true, 30, 100, 1);
    fixture.engine.setCell (0, 0, 4, true, 40, 100, 1);
    fixture.engine.generateWindow (0, 8, true);

    fixture.engine.setCellVelocity (0, 0, 2, 88);

    REQUIRE (fixture.engine.generatedCellAt (0, 2).velocity == 88);
    REQUIRE (fixture.engine.generatedCellAt (0, 5).velocity == 88);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 30);

    fixture.engine.setCellVelocity (0, 0, 1, 44);
    REQUIRE (fixture.engine.generatedCellAt (0, 2).velocity == 88);
}

TEST_CASE ("channel loop length follows step count only when it matched the old step count", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelLoopLength (0, 16);
    fixture.engine.setChannelLoopLength (1, 6);
    fixture.engine.setStepCount (8);

    REQUIRE (fixture.engine.channelAt (0).loopLength == 8);
    REQUIRE (fixture.engine.channelAt (1).loopLength == 6);

    fixture.engine.setStepCount (16);
    REQUIRE (fixture.engine.channelAt (0).loopLength == 16);
    REQUIRE (fixture.engine.channelAt (1).loopLength == 6);
}

TEST_CASE ("trailing cells do not make source active", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setChannelLoopLength (0, 3);
    fixture.engine.setCell (0, 0, 5, true, 99, 100, 1);

    REQUIRE (test::EngineTestPeer::activeSourceIndicesEmpty (fixture.engine));
}

TEST_CASE ("cells before an offset channel loop range do not make source active", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setChannelLoopLength (0, 3, 2);
    fixture.engine.setCell (0, 0, 1, true, 99, 100, 1);

    REQUIRE (test::EngineTestPeer::activeSourceIndicesEmpty (fixture.engine));
}

TEST_CASE ("playback snapshot version ignores metadata labels and tracks playback edits", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();

    const auto initialVersion = fixture.engine.playbackSnapshotVersion();

    fixture.engine.setChannelLabel (0, "Kick");
    REQUIRE (fixture.engine.playbackSnapshotVersion() == initialVersion);

    fixture.engine.setChannelNote (0, 42);
    REQUIRE (fixture.engine.playbackSnapshotVersion() == initialVersion + 1);

    const auto noteVersion = fixture.engine.playbackSnapshotVersion();
    fixture.engine.setChannelPlaybackMode (0, PlaybackMode::reverse);
    REQUIRE (fixture.engine.playbackSnapshotVersion() == noteVersion + 1);
}

TEST_CASE ("channel lock overrides random source", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::stack);
    fixture.engine.setChannelLock (0, 2);
    fixture.engine.setCell (2, 0, 0, true, 101, 100, 1);
    fixture.setRandomValues ({ 0.0 });
    fixture.engine.generateWindow (0, fixture.engine.getStepCount(), true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == 2);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 101);
}

TEST_CASE ("cell edit does not re-roll generated sources", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::stack);
    fixture.engine.setCell (0, 0, 0, true, 50, 100, 1);
    fixture.engine.setCell (2, 0, 0, true, 90, 100, 1);
    fixture.setRandomValues ({ 0.99 });
    fixture.engine.generateWindow (0, 4, true);

    for (int step = 0; step < 4; ++step)
        REQUIRE (fixture.engine.generatedCellAt (0, step).source == 2);

    fixture.setRandomValues ({ 0.0 });
    fixture.engine.setCellVelocity (0, 0, 0, 77);

    for (int step = 0; step < 4; ++step)
        REQUIRE (fixture.engine.generatedCellAt (0, step).source == 2);
}

TEST_CASE ("cell edit only mutates generated when source matches", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::stack);
    fixture.engine.setCell (0, 0, 0, true, 50, 100, 1);
    fixture.engine.setCell (2, 0, 0, true, 90, 100, 1);
    fixture.setRandomValues ({ 0.99 });
    fixture.engine.generateWindow (0, 4, true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 90);

    fixture.engine.setCellVelocity (2, 0, 0, 33);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 33);

    fixture.engine.setCellVelocity (0, 0, 0, 7);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 33);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 0).velocity == 7);

    fixture.engine.setCellEnabled (0, 0, 0, false);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).enabled);
    REQUIRE_FALSE (fixture.engine.sourceCellAt (0, 0, 0).enabled);

    fixture.engine.setCellEnabled (2, 0, 0, false);
    REQUIRE_FALSE (fixture.engine.generatedCellAt (0, 0).enabled);
}

TEST_CASE ("channel lock routes source edits to generated", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (2);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::stack);
    fixture.engine.setChannelLock (0, 1);
    fixture.engine.setCell (1, 0, 0, true, 64, 100, 1);
    fixture.setRandomValues ({ 0.0 });
    fixture.engine.generateWindow (0, 2, true);

    REQUIRE (fixture.engine.generatedCellAt (0, 0).source == 1);

    fixture.engine.setCellVelocity (1, 0, 0, 99);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 99);

    fixture.engine.setCellVelocity (2, 0, 0, 5);
    REQUIRE (fixture.engine.generatedCellAt (0, 0).velocity == 99);
}

TEST_CASE ("generateWindow scans active sources once", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (8);
    fixture.engine.setGenerationMode (GenerationMode::perChannel);
    fixture.engine.setCell (0, 0, 0, true, 80, 100, 1);
    fixture.engine.setCell (1, 1, 1, true, 90, 100, 1);
    fixture.engine.setCell (2, 2, 2, true, 100, 100, 1);
    fixture.engine.setCell (3, 3, 3, true, 110, 100, 1);

    test::EngineTestPeer::resetActiveSourceIndicesCallCount (fixture.engine);
    fixture.engine.generateWindow (0, 16, true);

    REQUIRE (test::EngineTestPeer::activeSourceIndicesCallCount (fixture.engine) == 1);
}

TEST_CASE ("cell edits reach steps beyond sixteen", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (32);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setCell (0, 0, 31, true, 123, 100, 1);
    fixture.engine.setCell (0, 0, 20, true, 77, 50, 1);

    REQUIRE (fixture.engine.sourceCellAt (0, 0, 31).enabled);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 31).velocity == 123);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 20).enabled);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 20).velocity == 77);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 20).probability == 50);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 20).cycle == 1);

    fixture.engine.setCellVelocity (0, 0, 28, 64);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 28).velocity == 64);

    fixture.engine.setCellEnabled (0, 0, 24, true);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 24).enabled);

    fixture.engine.setCellCycle (0, 0, 17, 4);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 17).probability == 100);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 17).cycle == 4);

    REQUIRE_FALSE (fixture.engine.sourceCellAt (0, 0, 15).enabled);
    REQUIRE_FALSE (fixture.engine.sourceCellAt (0, 0, 16).enabled);
}

TEST_CASE ("cycle offset clamps to cycle range", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 4, 9);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 0).cycleOffset == 3);

    fixture.engine.setCellCycleOffset (0, 0, 0, 2);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 0).cycleOffset == 2);

    fixture.engine.setCellCycle (0, 0, 0, 2);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 0).cycleOffset == 1);

    fixture.engine.setCellCycle (0, 0, 0, 1);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 0).cycleOffset == 0);
}

TEST_CASE ("cycle inversion is allowed when cycle is one", "[engine][generation]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 4, 0, true);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 0).cycleInverted);

    fixture.engine.setCellCycle (0, 0, 0, 1);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 0).cycleInverted);

    fixture.engine.setCellCycleInverted (0, 0, 0, false);
    REQUIRE_FALSE (fixture.engine.sourceCellAt (0, 0, 0).cycleInverted);

    fixture.engine.setCellCycleInverted (0, 0, 0, true);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 0).cycleInverted);
}
