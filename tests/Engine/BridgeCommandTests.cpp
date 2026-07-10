#include "EngineTestHelpers.h"

#include <engine/KshEngineCommands.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ksh;
using ksh::test::EngineFixture;

TEST_CASE ("dispatchEngineCommand sets steps and channels", "[engine][bridge]")
{
    EngineFixture fixture;
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (1);

    REQUIRE (dispatchEngineCommand (fixture.engine, "steps", { 8 }));
    REQUIRE (dispatchEngineCommand (fixture.engine, "channels", { 4 }));

    REQUIRE (fixture.engine.getStepCount() == 8);
    REQUIRE (fixture.engine.getChannelCount() == 4);
}

TEST_CASE ("dispatchEngineCommand sets source pattern resolution", "[engine][bridge]")
{
    EngineFixture fixture;
    fixture.engine.setStepCount (16);
    fixture.engine.setRate ("16n");

    REQUIRE (dispatchEngineCommand (fixture.engine, "source_steps", { 2, 12 }));
    REQUIRE (dispatchEngineCommand (fixture.engine, "source_rate", { 2, "8n" }));

    REQUIRE (fixture.engine.getSourceStepCount (0) == 16);
    REQUIRE (std::string { fixture.engine.getSourceRate (0) } == "16n");
    REQUIRE (fixture.engine.getSourceStepCount (1) == 12);
    REQUIRE (std::string { fixture.engine.getSourceRate (1) } == "8n");

    REQUIRE (dispatchEngineCommand (fixture.engine, "static_source", { 2 }));
    REQUIRE (fixture.engine.getStepCount() == 12);
    REQUIRE (std::string { fixture.engine.getRate() } == "8n");
}

TEST_CASE ("dispatchEngineCommand edits cells with one-based args", "[engine][bridge]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (2);

    REQUIRE (dispatchEngineCommand (fixture.engine,
                                    "cell",
                                    { 1, 2, 5, 1, 90, 100, 1 }));

    REQUIRE (fixture.engine.sourceCellAt (0, 1, 4).enabled);
    REQUIRE (fixture.engine.sourceCellAt (0, 1, 4).velocity == 90);
}

TEST_CASE ("dispatchEngineCommand sets channel loop range with one-based start", "[engine][bridge]")
{
    EngineFixture fixture;
    fixture.engine.setStepCount (16);

    REQUIRE (dispatchEngineCommand (fixture.engine, "channel_loop_length", { 1, 5, 4 }));

    REQUIRE (fixture.engine.channelAt (0).loopStart == 3);
    REQUIRE (fixture.engine.channelAt (0).loopLength == 5);
}

TEST_CASE ("dispatchEngineCommand accepts muted static source", "[engine][bridge]")
{
    EngineFixture fixture;
    fixture.clearAll();

    REQUIRE (dispatchEngineCommand (fixture.engine, "static_source", { "M" }));
    REQUIRE (fixture.engine.getStaticSource() == Constants::mutedStaticSource);
}

TEST_CASE ("dispatchEngineCommand copies source patterns with one-based args", "[engine][bridge]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setStepCount (16);
    fixture.engine.setChannelCount (2);

    fixture.engine.setCell (0, 1, 4, true, 91, 37, 4, 2, true, 3);
    fixture.engine.setCell (1, 1, 4, true, 12, 100, 1, 0, false, 1);
    fixture.engine.setSourceChannelMute (0, 1, true);
    fixture.engine.setSourceChannelMute (1, 1, false);
    fixture.engine.setSourceStepCount (0, 12);
    fixture.engine.setSourceRate (0, "8n");

    REQUIRE (dispatchEngineCommand (fixture.engine, "source_pattern_copy", { 1, 2 }));

    const auto copied = fixture.engine.sourceCellAt (1, 1, 4);
    REQUIRE (copied.enabled);
    REQUIRE (copied.velocity == 91);
    REQUIRE (copied.probability == 37);
    REQUIRE (copied.cycle == 4);
    REQUIRE (copied.cycleOffset == 2);
    REQUIRE (copied.cycleInverted);
    REQUIRE (copied.roll == 3);
    REQUIRE (fixture.engine.sourceChannelMutedAt (1, 1));
    REQUIRE (fixture.engine.getSourceStepCount (1) == 12);
    REQUIRE (std::string { fixture.engine.getSourceRate (1) } == "8n");

    REQUIRE (dispatchEngineCommand (fixture.engine, "source_pattern_copy", { 2, 2 }));
    REQUIRE (fixture.engine.sourceCellAt (1, 1, 4) == copied);
}

TEST_CASE ("dispatchEngineCommand rejects unknown selector", "[engine][bridge]")
{
    EngineFixture fixture;
    REQUIRE_FALSE (dispatchEngineCommand (fixture.engine, "not_a_command", {}));
}

TEST_CASE ("dispatchEngineCommand resets playback window", "[engine][bridge]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setCell (0, 0, 3, true, 80, 100, 1);
    test::EngineTestPeer::setPlaybackState (fixture.engine, 0, 5);

    REQUIRE (dispatchEngineCommand (fixture.engine, "reset", {}));
    REQUIRE (fixture.engine.getCurrentStep() == 0);
    REQUIRE (fixture.engine.getPlayingStepOneBased() == 0);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 3).enabled);
}
