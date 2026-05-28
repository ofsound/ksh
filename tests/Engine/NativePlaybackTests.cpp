#include "EngineTestHelpers.h"
#include "NativePlaybackTestHelpers.h"

#include <catch2/catch_test_macros.hpp>

using namespace ksh;
using ksh::test::EngineFixture;
using ksh::test::concatNativeRows;
using ksh::test::requireNativeRow;

TEST_CASE ("native playback rows include deterministic hits and swing", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (2);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setSwing (100);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.engine.setCell (0, 0, 1, true, 80, 100, 1);

    const auto rows = fixture.engine.buildNativePlaybackRows();

    requireNativeRow (rows[0], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[1], nativeHitRow (36, 80, 100, 1, 62.5, 1, 2, 1, 2));
}

TEST_CASE ("native playback rows expand rolls inside step", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1, 0, false, 4);

    const auto rows = fixture.engine.buildNativePlaybackRows();

    requireNativeRow (rows[0], concatNativeRows (
                                        nativeHitRow (36, 100, 28, 1, 0.0, 1, 1, 1, 1),
                                        concatNativeRows (nativeHitRow (36, 100, 28, 1, 31.25, 1, 1, 1, 1),
                                                          concatNativeRows (nativeHitRow (36, 100, 28, 1, 62.5, 1, 1, 1, 1),
                                                                            nativeHitRow (36, 100, 28, 1, 93.75, 1, 1, 1, 1)))));
}

TEST_CASE ("native playback rows apply swing only to first roll hit", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (2);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setSwing (100);
    fixture.engine.setCell (0, 0, 1, true, 80, 100, 1, 0, false, 4);

    const auto rows = fixture.engine.buildNativePlaybackRows();

    requireNativeRow (rows[1], concatNativeRows (
                                        nativeHitRow (36, 80, 28, 1, 62.5, 1, 2, 1, 2),
                                        concatNativeRows (nativeHitRow (36, 80, 28, 1, 31.25, 1, 2, 1, 2),
                                                          concatNativeRows (nativeHitRow (36, 80, 28, 1, 62.5, 1, 2, 1, 2),
                                                                            nativeHitRow (36, 80, 28, 1, 93.75, 1, 2, 1, 2)))));
}

TEST_CASE ("native playback rows precompute cycle gates", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 3);

    const auto rows = fixture.engine.buildNativePlaybackRows();

    REQUIRE (fixture.engine.nativePlaybackStepCount == 3);
    requireNativeRow (rows[0], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[1], {});
    requireNativeRow (rows[2], {});
}

TEST_CASE ("native playback rows precompute cycle offsets", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 3, 2);

    const auto rows = fixture.engine.buildNativePlaybackRows();

    REQUIRE (fixture.engine.nativePlaybackStepCount == 3);
    requireNativeRow (rows[0], {});
    requireNativeRow (rows[1], {});
    requireNativeRow (rows[2], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
}

TEST_CASE ("native playback rows precompute cycle inversion", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 4, 0, true);

    const auto rows = fixture.engine.buildNativePlaybackRows();

    REQUIRE (fixture.engine.nativePlaybackStepCount == 4);
    requireNativeRow (rows[0], {});
    requireNativeRow (rows[1], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[2], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[3], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
}

TEST_CASE ("native playback rows use least common cycle period", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (2);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 2);
    fixture.engine.setCell (0, 1, 0, true, 90, 100, 3);

    const auto rows = fixture.engine.buildNativePlaybackRows();

    REQUIRE (fixture.engine.nativePlaybackStepCount == 6);
    requireNativeRow (rows[0], concatNativeRows (
                                        nativeHitRow (fixture.engine.channels[0].note, 100, 100, 1, 0.0, 1, 1, 1, 1),
                                        nativeHitRow (fixture.engine.channels[1].note, 90, 100, 1, 0.0, 2, 1, 1, 1)));
    requireNativeRow (rows[1], {});
    requireNativeRow (rows[2], nativeHitRow (fixture.engine.channels[0].note, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[3], nativeHitRow (fixture.engine.channels[1].note, 90, 100, 1, 0.0, 2, 1, 1, 1));
    requireNativeRow (rows[4], nativeHitRow (fixture.engine.channels[0].note, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[5], {});
}

TEST_CASE ("native playback rows preroll probability", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setCell (0, 0, 0, true, 100, 50, 1);
    fixture.setRandomValues ({ 0.1, 0.9, 0.2, 0.8 });

    const auto rows = fixture.engine.buildNativePlaybackRows();

    REQUIRE (fixture.engine.nativePlaybackStepCount == 16);
    requireNativeRow (rows[0], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[1], {});
    requireNativeRow (rows[2], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[3], {});
}

TEST_CASE ("native playback rows evaluate probability once per roll step", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setCell (0, 0, 0, true, 100, 50, 1, 0, false, 4);
    fixture.setRandomValues ({ 0.0 });

    fixture.engine.randomCallCount = 0;
    [[maybe_unused]] const auto built = fixture.engine.buildNativePlaybackRows();

    REQUIRE (fixture.engine.randomCallCount == 16);
}

TEST_CASE ("native playback rows evaluate cycle before probability", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setCell (0, 0, 0, true, 100, 50, 2);
    fixture.setRandomValues ({ 0.0 });

    fixture.engine.randomCallCount = 0;
    [[maybe_unused]] const auto built = fixture.engine.buildNativePlaybackRows();

    REQUIRE (fixture.engine.nativePlaybackStepCount == 32);
    REQUIRE (fixture.engine.randomCallCount == 16);
}

TEST_CASE ("native playback rows preroll velocity humanize", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setVelocityHumanize (20);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.setRandomValues ({ 0.0, 1.0, 0.5 });

    const auto rows = fixture.engine.buildNativePlaybackRows();

    REQUIRE (fixture.engine.nativePlaybackStepCount == 16);
    requireNativeRow (rows[0], nativeHitRow (36, 80, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[1], nativeHitRow (36, 120, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[2], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
}

TEST_CASE ("native playback rows share variation expansion for probability and velocity", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setVelocityHumanize (20);
    fixture.engine.setCell (0, 0, 0, true, 100, 50, 1);
    fixture.setRandomValues ({ 0.0, 0.0, 0.0, 0.0 });

    [[maybe_unused]] const auto built = fixture.engine.buildNativePlaybackRows();

    REQUIRE (fixture.engine.nativePlaybackStepCount == 16);
}

TEST_CASE ("native playback rows preroll late timing humanize", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setTimingHumanize (100);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.setRandomValues ({ 1.0 });

    const auto rows = fixture.engine.buildNativePlaybackRows();

    REQUIRE (fixture.engine.nativePlaybackStepCount == 16);
    requireNativeRow (rows[0], nativeHitRow (36, 100, 100, 1, 25.0, 1, 1, 1, 1));
}

TEST_CASE ("native playback rows preroll early timing humanize", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (2);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setTimingHumanize (100);
    fixture.engine.setCell (0, 0, 1, true, 100, 100, 1);
    fixture.setRandomValues ({ 0.0 });

    const auto rows = fixture.engine.buildNativePlaybackRows();

    REQUIRE (fixture.engine.nativePlaybackStepCount == 32);
    requireNativeRow (rows[0], nativeHitRow (36, 100, 100, 1, 100.0, 1, 2, 1, 2));
    requireNativeRow (rows[1], {});
}

TEST_CASE ("native playback rows clamp first step early timing", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (2);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setTimingHumanize (100);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.setRandomValues ({ 0.0 });

    const auto rows = fixture.engine.buildNativePlaybackRows();

    requireNativeRow (rows[0], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[1], nativeHitRow (36, 100, 100, 1, 100.0, 1, 1, 1, 1));
}

TEST_CASE ("native playback rows include note hit metadata", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.engine.generateWindow (0, 1, true);
    fixture.engine.generated[0][0].sourceStep = 3;

    const auto rows = fixture.engine.buildNativePlaybackRows();

    REQUIRE (rows[0].size() == 1);
    REQUIRE (rows[0][0].uiChannel == 1);
    REQUIRE (rows[0][0].uiGeneratedStep == 1);
    REQUIRE (rows[0][0].uiSource == 1);
    REQUIRE (rows[0][0].uiSourceStep == 4);
}

TEST_CASE ("reverse playback mirrors transport position across active length", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setChannelPlaybackMode (0, PlaybackMode::reverse);

    REQUIRE (fixture.engine.playbackStepForChannel (0, 0) == 3);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 1) == 2);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 2) == 1);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 3) == 0);
}

TEST_CASE ("boomerang playback repeats endpoints across active length", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setChannelLoopLength (0, 3);
    fixture.engine.setChannelPlaybackMode (0, PlaybackMode::boomerang);
    fixture.engine.setCell (0, 0, 0, true, 10, 100, 1);
    fixture.engine.setCell (0, 0, 1, true, 20, 100, 1);
    fixture.engine.setCell (0, 0, 2, true, 30, 100, 1);

    REQUIRE (fixture.engine.playbackStepForChannel (0, 0) + 1 == 1);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 1) + 1 == 2);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 2) + 1 == 3);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 3) + 1 == 3);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 4) + 1 == 2);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 5) + 1 == 1);
}

TEST_CASE ("native playback rows apply playback modes to metadata", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setChannelLoopLength (0, 3);
    fixture.engine.setChannelPlaybackMode (0, PlaybackMode::boomerang);
    fixture.engine.setCell (0, 0, 0, true, 10, 100, 1);
    fixture.engine.setCell (0, 0, 1, true, 20, 100, 1);
    fixture.engine.setCell (0, 0, 2, true, 30, 100, 1);
    fixture.engine.generateWindow (0, 4, true);

    const auto rows = fixture.engine.buildNativePlaybackRows();

    REQUIRE (fixture.engine.nativePlaybackStepCount == 12);
    requireNativeRow (rows[0], nativeHitRow (36, 10, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[2], nativeHitRow (36, 30, 100, 1, 0.0, 1, 3, 1, 3));
    requireNativeRow (rows[3], nativeHitRow (36, 30, 100, 1, 0.0, 1, 3, 1, 3));
    requireNativeRow (rows[5], nativeHitRow (36, 10, 100, 1, 0.0, 1, 1, 1, 1));
}

TEST_CASE ("engine uses native playback by default", "[engine][native]")
{
    EngineFixture fixture;
    REQUIRE (fixture.engine.nativePlaybackActive());
}

TEST_CASE ("transport uses native playback only", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (4);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1);
    fixture.notes.clear();

    fixture.engine.transportPosition (0.0, true);

    REQUIRE (fixture.engine.nativePlaybackActive());
    REQUIRE (fixture.notes.empty());
}

TEST_CASE ("native playback refreshes generated window on transport boundary", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setRefreshSteps (4);
    fixture.engine.setGenerationMode (GenerationMode::stack);
    fixture.engine.setCell (0, 0, 4, true, 44, 100, 1);
    fixture.engine.setCell (3, 0, 4, true, 99, 100, 1);
    fixture.setRandomValues ({ 0.0 });
    fixture.engine.generateWindow (4, 4, true);

    REQUIRE (fixture.engine.generated[0][4].source == 0);
    REQUIRE (fixture.engine.generated[0][4].velocity == 44);

    fixture.notes.clear();
    fixture.setRandomValues ({ 0.0, 0.99, 0.0 });
    fixture.engine.transportPosition (0.0, true);
    fixture.engine.transportPosition (0.25, true);
    fixture.engine.transportPosition (0.5, true);
    fixture.engine.transportPosition (0.75, true);
    fixture.engine.transportPosition (1.0, true);

    REQUIRE (fixture.engine.nativePlaybackActive());
    REQUIRE (fixture.notes.empty());
    REQUIRE (fixture.engine.generated[0][4].source == 3);
    REQUIRE (fixture.engine.generated[0][4].velocity == 99);
    requireNativeRow (fixture.engine.nativePlaybackRows[4], nativeHitRow (36, 99, 100, 1, 0.0, 1, 5, 4, 5));

    fixture.engine.transportPosition (1.01, true);
    REQUIRE (fixture.engine.generated[0][4].source == 3);
}

TEST_CASE ("native playback keeps early humanized refresh boundary hit readable", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setRefreshSteps (4);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setTimingHumanize (1);
    fixture.engine.setCell (0, 0, 4, true, 100, 100, 1);
    fixture.setRandomValues ({ 0.0 });

    fixture.engine.transportPosition (0.0, true);
    fixture.engine.transportPosition (0.25, true);
    fixture.engine.transportPosition (0.5, true);
    fixture.engine.transportPosition (0.75, true);
    fixture.engine.transportPosition (1.0, true);

    REQUIRE (fixture.engine.nativePlaybackStepCount == 128);
    requireNativeRow (fixture.engine.nativePlaybackRows[3], {});
    requireNativeRow (fixture.engine.nativePlaybackRows[4], nativeHitRow (36, 100, 100, 1, 0.0, 1, 5, 1, 5));
}

TEST_CASE ("timing humanize change keeps next hit readable during playback", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setCell (0, 0, 5, true, 100, 100, 1);
    fixture.engine.transportPlaying = 1;
    fixture.engine.lastReportedGlobalStep = 4;
    fixture.setRandomValues ({ 0.0 });

    fixture.engine.setTimingHumanize (1);

    REQUIRE (fixture.engine.nativePlaybackStepCount == 128);
    requireNativeRow (fixture.engine.nativePlaybackRows[4], {});
    requireNativeRow (fixture.engine.nativePlaybackRows[5], nativeHitRow (36, 100, 100, 1, 0.0, 1, 6, 1, 6));
}

TEST_CASE ("timing humanize refresh one does not queue future hits from current row", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (1);
    fixture.engine.setRate ("16n");
    fixture.engine.setTempo (120.0);
    fixture.engine.setRefreshSteps (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setTimingHumanize (1);

    for (int step = 0; step < 16; ++step)
        fixture.engine.setCell (0, 0, step, true, 100, 100, 1);

    fixture.setRandomValues ({ 0.0 });
    fixture.engine.transportPosition (0.0, true);

    REQUIRE (fixture.engine.nativePlaybackStepCount == 256);
    requireNativeRow (fixture.engine.nativePlaybackRows[0], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (fixture.engine.nativePlaybackRows[1], nativeHitRow (36, 100, 100, 1, 0.0, 1, 2, 1, 2));
}
