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

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

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

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

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

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

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

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

    REQUIRE (built.stepCount == 3);
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

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

    REQUIRE (built.stepCount == 3);
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

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

    REQUIRE (built.stepCount == 4);
    requireNativeRow (rows[0], {});
    requireNativeRow (rows[1], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[2], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[3], nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
}

TEST_CASE ("native playback rows treat inverted cycle one as muted", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 1, 0, true);

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

    REQUIRE (built.stepCount == 1);
    requireNativeRow (rows[0], {});
}

TEST_CASE ("native playback rows use least common cycle period", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (2);
    fixture.engine.setCell (0, 0, 0, true, 100, 100, 2);
    fixture.engine.setCell (0, 1, 0, true, 90, 100, 3);

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

    REQUIRE (built.stepCount == 6);
    requireNativeRow (rows[0], concatNativeRows (
                                        nativeHitRow (fixture.engine.channelAt (0).note, 100, 100, 1, 0.0, 1, 1, 1, 1),
                                        nativeHitRow (fixture.engine.channelAt (1).note, 90, 100, 1, 0.0, 2, 1, 1, 1)));
    requireNativeRow (rows[1], {});
    requireNativeRow (rows[2], nativeHitRow (fixture.engine.channelAt (0).note, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (rows[3], nativeHitRow (fixture.engine.channelAt (1).note, 90, 100, 1, 0.0, 2, 1, 1, 1));
    requireNativeRow (rows[4], nativeHitRow (fixture.engine.channelAt (0).note, 100, 100, 1, 0.0, 1, 1, 1, 1));
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

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

    REQUIRE (built.stepCount == 16);
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

    test::EngineTestPeer::resetRandomCallCount (fixture.engine);
    [[maybe_unused]] const auto built = fixture.engine.buildNativePlaybackRows();

    REQUIRE (test::EngineTestPeer::randomCallCount (fixture.engine) == 16);
}

TEST_CASE ("native playback rows evaluate cycle before probability", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (1);
    fixture.engine.setChannelCount (1);
    fixture.engine.setCell (0, 0, 0, true, 100, 50, 2);
    fixture.setRandomValues ({ 0.0 });

    test::EngineTestPeer::resetRandomCallCount (fixture.engine);
    const auto built = fixture.engine.buildNativePlaybackRows();

    REQUIRE (built.stepCount == 32);
    REQUIRE (test::EngineTestPeer::randomCallCount (fixture.engine) == 16);
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

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

    REQUIRE (built.stepCount == 16);
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

    const auto built = fixture.engine.buildNativePlaybackRows();

    REQUIRE (built.stepCount == 16);
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

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

    REQUIRE (built.stepCount == 16);
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

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

    REQUIRE (built.stepCount == 32);
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

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

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
    test::EngineTestPeer::setGeneratedCellSourceStep (fixture.engine, 0, 0, 3);

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

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

TEST_CASE ("reverse playback mirrors transport position across offset active range", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (8);
    fixture.engine.setChannelCount (1);
    fixture.engine.setChannelLoopLength (0, 3, 2);
    fixture.engine.setChannelPlaybackMode (0, PlaybackMode::reverse);

    REQUIRE (fixture.engine.playbackStepForChannel (0, 0) == 4);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 1) == 3);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 2) == 2);
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

TEST_CASE ("forward playback wraps within offset loop range", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (1);
    fixture.engine.setChannelLoopLength (0, 10, 2);

    REQUIRE (fixture.engine.playbackStepForChannel (0, 0) == 2);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 1) == 3);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 10) == 2);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 11) == 3);
    REQUIRE (fixture.engine.playbackStepForChannel (0, 16) == 8);
}

TEST_CASE ("forward playback repeats offset loop hits on loop length cadence", "[engine][native]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (1);
    fixture.engine.setGenerationMode (GenerationMode::staticSource);
    fixture.engine.setChannelLoopLength (0, 10, 2);
    fixture.engine.setCell (0, 0, 2, true, 10, 100, 1);
    fixture.engine.setCell (0, 0, 8, true, 20, 100, 1);
    fixture.engine.setCell (0, 0, 10, true, 30, 100, 1);
    fixture.engine.generateWindow (0, 16, true);

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

    REQUIRE (built.stepCount == 80);
    requireNativeRow (rows[0], nativeHitRow (36, 10, 100, 1, 0.0, 1, 3, 1, 3));
    requireNativeRow (rows[6], nativeHitRow (36, 20, 100, 1, 0.0, 1, 9, 1, 9));
    requireNativeRow (rows[8], nativeHitRow (36, 30, 100, 1, 0.0, 1, 11, 1, 11));
    requireNativeRow (rows[10], nativeHitRow (36, 10, 100, 1, 0.0, 1, 3, 1, 3));
    requireNativeRow (rows[16], nativeHitRow (36, 20, 100, 1, 0.0, 1, 9, 1, 9));
    requireNativeRow (rows[18], nativeHitRow (36, 30, 100, 1, 0.0, 1, 11, 1, 11));
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

    const auto built = fixture.engine.buildNativePlaybackRows();
    const auto& rows = built.rows;

    REQUIRE (built.stepCount == 12);
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

    REQUIRE (fixture.engine.generatedCellAt (0, 4).source == 0);
    REQUIRE (fixture.engine.generatedCellAt (0, 4).velocity == 44);

    fixture.notes.clear();
    fixture.setRandomValues ({ 0.0, 0.99, 0.0 });
    fixture.engine.transportPosition (0.0, true);
    fixture.engine.transportPosition (0.25, true);
    fixture.engine.transportPosition (0.5, true);
    fixture.engine.transportPosition (0.75, true);
    fixture.engine.transportPosition (1.0, true);

    REQUIRE (fixture.engine.nativePlaybackActive());
    REQUIRE (fixture.notes.empty());
    REQUIRE (fixture.engine.generatedCellAt (0, 4).source == 3);
    REQUIRE (fixture.engine.generatedCellAt (0, 4).velocity == 99);
    fixture.engine.syncNativePlaybackTable();
    requireNativeRow (fixture.engine.nativePlaybackRowAt (4), nativeHitRow (36, 99, 100, 1, 0.0, 1, 5, 4, 5));

    fixture.engine.transportPosition (1.01, true);
    REQUIRE (fixture.engine.generatedCellAt (0, 4).source == 3);
}

TEST_CASE ("native playback flushes refreshed generated preview", "[engine][native]")
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
    fixture.previews.clear();

    fixture.setRandomValues ({ 0.0, 0.99, 0.0 });
    fixture.engine.transportPosition (0.0, true);
    fixture.engine.transportPosition (0.25, true);
    fixture.engine.transportPosition (0.5, true);
    fixture.engine.transportPosition (0.75, true);
    fixture.engine.transportPosition (1.0, true);

    REQUIRE (fixture.previews.empty());

    fixture.engine.flushPreview();

    REQUIRE (fixture.previews.size() == 1);
    REQUIRE (fixture.previews.back()["generated"][0][4]["source"] == 4);
    REQUIRE (fixture.previews.back()["generated"][0][4]["velocity"] == 99);
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

    fixture.engine.syncNativePlaybackTable();
    REQUIRE (fixture.engine.getNativePlaybackStepCount() == 128);
    requireNativeRow (fixture.engine.nativePlaybackRowAt (3), {});
    requireNativeRow (fixture.engine.nativePlaybackRowAt (4), nativeHitRow (36, 100, 100, 1, 0.0, 1, 5, 1, 5));
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
    test::EngineTestPeer::setTransportState (fixture.engine, 1, 4);
    fixture.setRandomValues ({ 0.0 });

    fixture.engine.setTimingHumanize (1);

    fixture.engine.syncNativePlaybackTable();
    REQUIRE (fixture.engine.getNativePlaybackStepCount() == 128);
    requireNativeRow (fixture.engine.nativePlaybackRowAt (4), {});
    requireNativeRow (fixture.engine.nativePlaybackRowAt (5), nativeHitRow (36, 100, 100, 1, 0.0, 1, 6, 1, 6));
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

    fixture.engine.syncNativePlaybackTable();
    REQUIRE (fixture.engine.getNativePlaybackStepCount() == 256);
    requireNativeRow (fixture.engine.nativePlaybackRowAt (0), nativeHitRow (36, 100, 100, 1, 0.0, 1, 1, 1, 1));
    requireNativeRow (fixture.engine.nativePlaybackRowAt (1), nativeHitRow (36, 100, 100, 1, 0.0, 1, 2, 1, 2));
}
