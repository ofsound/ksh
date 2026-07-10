#include <engine/KshConstants.h>
#include <engine/KshTypes.h>

#include <catch2/catch_test_macros.hpp>

using namespace ksh;

TEST_CASE ("Constants match M4L device limits", "[engine][foundation]")
{
    REQUIRE (Constants::maxSteps == 32);
    REQUIRE (Constants::maxChannels == 8);
    REQUIRE (Constants::sourceCount == 16);
    REQUIRE (Constants::defaultChannelCount == 8);
    REQUIRE (Constants::maxRoll == 8);
    REQUIRE (Constants::nativeHitFieldCount == 9);
    REQUIRE (std::string { Constants::defaultRate } == "16n");
    REQUIRE (std::string { Constants::defaultGenerationMode } == "static");
}

TEST_CASE ("Rate normalization matches ksh_constants.js", "[engine][foundation]")
{
    REQUIRE (Constants::normalizeRate ("16n") == "16n");
    REQUIRE (Constants::normalizeRate ("8nt") == "8nt");
    REQUIRE (Constants::normalizeRate ("invalid") == "16n");
}

TEST_CASE ("Playback mode normalization matches ksh_constants.js", "[engine][foundation]")
{
    REQUIRE (Constants::normalizeChannelPlaybackMode ("reverse") == "reverse");
    REQUIRE (Constants::normalizeChannelPlaybackMode ("rev") == "reverse");
    REQUIRE (Constants::normalizeChannelPlaybackMode ("ping_pong") == "ping_pong");
    REQUIRE (Constants::normalizeChannelPlaybackMode ("ping-pong") == "ping_pong");
    REQUIRE (Constants::normalizeChannelPlaybackMode ("boomerang") == "ping_pong");
    REQUIRE (Constants::normalizeChannelPlaybackMode ("boom") == "ping_pong");
    REQUIRE (Constants::normalizeChannelPlaybackMode ("unknown") == "normal");
}

TEST_CASE ("Generation mode normalization matches engine", "[engine][foundation]")
{
    REQUIRE (normalizeGenerationMode ("stack") == GenerationMode::stack);
    REQUIRE (normalizeGenerationMode ("per_channel") == GenerationMode::perChannel);
    REQUIRE (normalizeGenerationMode ("static") == GenerationMode::staticSource);
    REQUIRE (normalizeGenerationMode ("garbage") == GenerationMode::stack);
}

TEST_CASE ("Default cell matches ksh_constants.js DEFAULT_CELL", "[engine][foundation]")
{
    const auto cell = defaultCell();

    REQUIRE_FALSE (cell.enabled);
    REQUIRE (cell.velocity == 100);
    REQUIRE (cell.probability == 100);
    REQUIRE (cell.cycle == 1);
    REQUIRE (cell.cycleOffset == 0);
    REQUIRE_FALSE (cell.cycleInverted);
    REQUIRE (cell.roll == 1);
    REQUIRE (cell.source == -1);
}

TEST_CASE ("cloneCell clamps and normalizes like ksh_constants.js", "[engine][foundation]")
{
    Cell input;
    input.enabled = true;
    input.velocity = 200;
    input.probability = 150;
    input.cycle = 4;
    input.cycleOffset = 99;
    input.cycleInverted = true;
    input.roll = 20;
    input.source = 2;

    const auto cell = cloneCell (input);

    REQUIRE (cell.enabled);
    REQUIRE (cell.velocity == 127);
    REQUIRE (cell.probability == 100);
    REQUIRE (cell.cycle == 4);
    REQUIRE (cell.cycleOffset == 3);
    REQUIRE (cell.cycleInverted);
    REQUIRE (cell.roll == 8);
    REQUIRE (cell.source == 2);
}

TEST_CASE ("cloneCell preserves cycle inversion when cycle is one", "[engine][foundation]")
{
    Cell input;
    input.cycle = 1;
    input.cycleInverted = true;

    const auto cell = cloneCell (input);

    REQUIRE (cell.cycle == 1);
    REQUIRE (cell.cycleInverted);
}

TEST_CASE ("Default channel uses M4L note map", "[engine][foundation]")
{
    const auto kick = defaultChannel (0);
    const auto snare = defaultChannel (1);

    REQUIRE (kick.label == "1");
    REQUIRE (kick.note == 36);
    REQUIRE (kick.lock == -1);
    REQUIRE (snare.note == 37);
}

TEST_CASE ("makeEmptySourcePattern fills default cells", "[engine][foundation]")
{
    const auto pattern = makeEmptySourcePattern();

    REQUIRE (pattern[0][0] == defaultCell());
    REQUIRE (pattern[Constants::maxChannels - 1][Constants::maxSteps - 1] == defaultCell());
}
