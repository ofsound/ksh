#include "EngineTestHelpers.h"

#include <engine/KshEngineCommands.h>

#include <catch2/catch_test_macros.hpp>

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
    fixture.engine.setPlaybackStateForTests (0, 5);

    REQUIRE (dispatchEngineCommand (fixture.engine, "reset", {}));
    REQUIRE (fixture.engine.getCurrentStep() == 0);
    REQUIRE (fixture.engine.getPlayingStepOneBased() == 0);
    REQUIRE (fixture.engine.sourceCellAt (0, 0, 3).enabled);
}
