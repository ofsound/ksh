#include "EngineTestHelpers.h"

#include <engine/KshJson.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ksh;
using ksh::test::EngineFixture;

TEST_CASE ("persistence payload restores source data", "[engine][persistence]")
{
    EngineFixture original;
    EngineFixture restored;

    original.clearAll();
    restored.clearAll();
    original.engine.setGenerationMode (GenerationMode::staticSource);
    original.engine.setStaticSource (2);
    original.engine.setStepCount (7);
    original.engine.setChannelCount (4);
    original.engine.setChannelLabel (0, "Sub");
    original.engine.setChannelNote (0, 35);
    original.engine.setChannelLock (0, 1);
    original.engine.setChannelLoopLength (0, 5, 2);
    original.engine.setDeviceActive (false);
    original.engine.setVelocityHumanize (12);
    original.engine.setTimingHumanize (8);
    original.engine.setCell (1, 0, 2, true, 77, 25, 1);
    original.engine.setSourceChannelMute (1, 0, true);

    const auto state = original.engine.serializeForPersistence();
    REQUIRE (restored.engine.deserializeForPersistence (state));

    REQUIRE (restored.engine.getStepCount() == 7);
    REQUIRE (restored.engine.getChannelCount() == 4);
    REQUIRE (restored.engine.channelAt (0).label == "Sub");
    REQUIRE (restored.engine.channelAt (0).note == 35);
    REQUIRE (restored.engine.channelAt (0).lock == 1);
    REQUIRE (restored.engine.channelAt (0).loopStart == 2);
    REQUIRE (restored.engine.channelAt (0).loopLength == 5);
    REQUIRE_FALSE (restored.engine.isDeviceActive());
    REQUIRE (restored.engine.getVelocityHumanize() == 12);
    REQUIRE (restored.engine.getTimingHumanize() == 8);
    REQUIRE (restored.engine.getGenerationMode() == GenerationMode::staticSource);
    REQUIRE (restored.engine.getStaticSource() == 2);
    REQUIRE (restored.engine.sourceCellAt (1, 0, 2).enabled);
    REQUIRE (restored.engine.sourceCellAt (1, 0, 2).velocity == 77);
    REQUIRE (restored.engine.sourceCellAt (1, 0, 2).probability == 25);
    REQUIRE (restored.engine.sourceCellAt (1, 0, 2).cycle == 1);
    REQUIRE (restored.engine.sourceChannelMutedAt (1, 0));
}

TEST_CASE ("persistence payload restores per-source resolution", "[engine][persistence]")
{
    EngineFixture original;
    EngineFixture restored;

    original.engine.setStepCount (16);
    original.engine.setRate ("16n");
    original.engine.setSourceStepCount (2, 11);
    original.engine.setSourceRate (2, "8n");
    original.engine.setStaticSource (2);

    const auto state = original.engine.serializeForPersistence();
    REQUIRE (restored.engine.deserializeForPersistence (state));

    REQUIRE (restored.engine.getStaticSource() == 2);
    REQUIRE (restored.engine.getSourceStepCount (0) == 16);
    REQUIRE (restored.engine.getSourceStepCount (2) == 11);
    REQUIRE (std::string { restored.engine.getSourceRate (0) } == "16n");
    REQUIRE (std::string { restored.engine.getSourceRate (2) } == "8n");
    REQUIRE (restored.engine.getStepCount() == 11);
    REQUIRE (std::string { restored.engine.getRate() } == "8n");
}

TEST_CASE ("serializeForPersistence roundtrips sparse pattern", "[engine][persistence]")
{
    EngineFixture original;
    original.engine.setStepCount (8);
    original.engine.setChannelCount (2);
    original.engine.setSwing (25);
    original.engine.setCell (0, 0, 0, true, 64, 30, 1, 0, false, 3);
    original.engine.setChannelLabel (0, "Sub");
    original.engine.setChannelNote (1, 50);

    const auto payload = original.engine.serializeForPersistence();
    REQUIRE (payload["v"] == 1);
    REQUIRE (payload["stepCount"] == 8);
    REQUIRE (payload["channelCount"] == 2);
    REQUIRE (payload["swing"] == 25);
    REQUIRE (payload["sourceSettings"].size() == Constants::sourceCount);
    REQUIRE_FALSE (payload.contains ("nativeTiming"));
    REQUIRE (payload["cells"].size() >= 1);

    const auto json = payload.dump();
    REQUIRE (json.size() < 5000);

    EngineFixture restored;
    REQUIRE (restored.engine.deserializeForPersistence (nlohmann::json::parse (json)));

    REQUIRE (restored.engine.getStepCount() == 8);
    REQUIRE (restored.engine.getChannelCount() == 2);
    REQUIRE (restored.engine.getSwing() == 25);
    REQUIRE (restored.engine.sourceCellAt (0, 0, 0).enabled);
    REQUIRE (restored.engine.sourceCellAt (0, 0, 0).velocity == 64);
    REQUIRE (restored.engine.sourceCellAt (0, 0, 0).roll == 3);
    REQUIRE (restored.engine.channelAt (0).label == "Sub");
    REQUIRE (restored.engine.channelAt (1).note == 50);
}

TEST_CASE ("serializeForPersistence includes channel settings", "[engine][persistence]")
{
    EngineFixture original;
    original.engine.setStepCount (16);
    original.engine.setChannelCount (8);
    original.engine.setChannelNote (0, 36);
    original.engine.setChannelNote (1, 38);
    original.engine.setChannelLoopLength (0, 16);
    original.engine.setChannelLock (1, 2);
    original.engine.setChannelPlaybackMode (1, PlaybackMode::ping_pong);
    original.engine.setGenerationMode (GenerationMode::staticSource);
    original.engine.setCell (0, 0, 0, true, 100, 100, 1);
    original.engine.setCell (1, 1, 4, true, 80, 50, 4, 3, true, 6);

    const auto payload = original.engine.serializeForPersistence();

    EngineFixture restored;
    REQUIRE (restored.engine.deserializeForPersistence (payload));

    REQUIRE (restored.engine.channelAt (0).note == 36);
    REQUIRE (restored.engine.channelAt (1).note == 38);
    REQUIRE (restored.engine.channelAt (0).loopLength == 16);
    REQUIRE (restored.engine.channelAt (1).lock == 2);
    REQUIRE (restored.engine.channelAt (1).playbackMode == PlaybackMode::ping_pong);
    REQUIRE (restored.engine.getGenerationMode() == GenerationMode::staticSource);
    REQUIRE (restored.engine.sourceCellAt (0, 0, 0).enabled);
    REQUIRE (restored.engine.sourceCellAt (1, 1, 4).enabled);
    REQUIRE (restored.engine.sourceCellAt (1, 1, 4).velocity == 80);
    REQUIRE (restored.engine.sourceCellAt (1, 1, 4).probability == 50);
    REQUIRE (restored.engine.sourceCellAt (1, 1, 4).cycle == 4);
    REQUIRE (restored.engine.sourceCellAt (1, 1, 4).cycleOffset == 3);
    REQUIRE (restored.engine.sourceCellAt (1, 1, 4).cycleInverted);
    REQUIRE (restored.engine.sourceCellAt (1, 1, 4).roll == 6);
}

TEST_CASE ("channel audition emits configured note", "[engine][persistence]")
{
    EngineFixture fixture;
    fixture.clearAll();
    fixture.engine.setChannelCount (3);
    fixture.engine.setChannelNote (1, 42);

    const auto note = fixture.engine.auditionChannel (1);

    REQUIRE (note.has_value());
    REQUIRE (note->pitch == 42);
    REQUIRE (note->velocity == 100);
    REQUIRE (note->channel == Constants::defaultMidiChannel);
    REQUIRE (note->durationMs == Constants::defaultNoteDurationMs);
    REQUIRE (note->delayMs == 0.0);
}
