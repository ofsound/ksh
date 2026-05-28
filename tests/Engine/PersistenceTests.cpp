#include "EngineTestHelpers.h"

#include <engine/KshJson.h>

#include <catch2/catch_test_macros.hpp>

using namespace ksh;
using ksh::test::EngineFixture;

TEST_CASE ("serialize deserialize restores source data", "[engine][persistence]")
{
    EngineFixture original;
    EngineFixture restored;

    original.clearAll();
    restored.clearAll();
    original.engine.setStepCount (7);
    original.engine.setChannelCount (4);
    original.engine.setChannelLabel (0, "Sub");
    original.engine.setChannelNote (0, 35);
    original.engine.setChannelLock (0, 1);
    original.engine.setChannelLoopLength (0, 5);
    original.engine.setDeviceActive (false);
    original.engine.setVelocityHumanize (12);
    original.engine.setTimingHumanize (8);
    original.engine.setGenerationMode (GenerationMode::staticSource);
    original.engine.setStaticSource (2);
    original.engine.setCell (1, 0, 2, true, 77, 25, 1);
    original.engine.setSourceChannelMute (1, 0, true);

    const auto state = original.engine.serialize();
    restored.engine.deserialize (state);

    REQUIRE (restored.engine.stepCount == 7);
    REQUIRE (restored.engine.channelCount == 4);
    REQUIRE (restored.engine.channels[0].label == "Sub");
    REQUIRE (restored.engine.channels[0].note == 35);
    REQUIRE (restored.engine.channels[0].lock == 1);
    REQUIRE (restored.engine.channels[0].loopLength == 5);
    REQUIRE_FALSE (restored.engine.deviceActive);
    REQUIRE (restored.engine.velocityHumanize == 12);
    REQUIRE (restored.engine.timingHumanize == 8);
    REQUIRE (restored.engine.generationMode == GenerationMode::staticSource);
    REQUIRE (restored.engine.staticSource == 2);
    REQUIRE (restored.engine.sources[1][0][2].enabled);
    REQUIRE (restored.engine.sources[1][0][2].velocity == 77);
    REQUIRE (restored.engine.sources[1][0][2].probability == 25);
    REQUIRE (restored.engine.sources[1][0][2].cycle == 1);
    REQUIRE_FALSE (state["sources"][1][0][2].contains ("gateMode"));
    REQUIRE_FALSE (state["sources"][1][0][2].contains ("random"));
    REQUIRE (restored.engine.sourceChannelMutes[1][0]);
}

TEST_CASE ("deserialize accepts UI lane schema", "[engine][persistence]")
{
    EngineFixture original;
    EngineFixture restored;

    original.clearAll();
    restored.clearAll();
    original.engine.setChannelCount (4);
    original.engine.setChannelLabel (0, "Sub");
    original.engine.setChannelNote (0, 35);
    original.engine.setChannelLock (0, 1);
    original.engine.setCell (1, 0, 2, true, 77, 25, 1, 0, false, 5);

    auto uiState = original.engine.serialize();
    uiState["laneCount"] = uiState["channelCount"];
    uiState["lanes"] = uiState["channels"];
    uiState.erase ("channelCount");
    uiState.erase ("channels");

    restored.engine.deserialize (uiState);

    REQUIRE (restored.engine.channelCount == 4);
    REQUIRE (restored.engine.channels[0].label == "Sub");
    REQUIRE (restored.engine.channels[0].note == 35);
    REQUIRE (restored.engine.channels[0].lock == 1);
    REQUIRE (restored.engine.sources[1][0][2].enabled);
    REQUIRE (restored.engine.sources[1][0][2].velocity == 77);
    REQUIRE (restored.engine.sources[1][0][2].probability == 25);
    REQUIRE (restored.engine.sources[1][0][2].cycle == 1);
    REQUIRE (restored.engine.sources[1][0][2].roll == 5);
}

TEST_CASE ("deserialize restores zero swing", "[engine][persistence]")
{
    EngineFixture restored;
    restored.engine.setSwing (50);
    restored.engine.deserialize ({ { "swing", 0 } });
    REQUIRE (restored.engine.swing == 0);
}

TEST_CASE ("deserialize preserves missing channel keys", "[engine][persistence]")
{
    EngineFixture restored;
    restored.engine.setChannelLabel (0, "");
    restored.engine.setChannelNote (0, 35);
    restored.engine.setChannelLock (0, 2);
    restored.engine.deserialize ({ { "channels", nlohmann::json::array ({ nlohmann::json::object() }) } });

    REQUIRE (restored.engine.channels[0].label == "");
    REQUIRE (restored.engine.channels[0].note == 35);
    REQUIRE (restored.engine.channels[0].lock == 2);
}

TEST_CASE ("deserialize does not emit intermediate statuses", "[engine][persistence]")
{
    EngineFixture fixture;
    fixture.statuses.clear();

    fixture.engine.deserialize ({
        { "stepCount", 8 },
        { "channelCount", 2 },
        { "refreshSteps", 4 },
        { "generationMode", "per_channel" },
        { "rate", "8n" },
        { "tempo", 100 },
        { "swing", 20 },
        { "velocityHumanize", 12 },
        { "timingHumanize", 8 },
        { "channels", nlohmann::json::array ({
              { { "label", "Sub" }, { "note", 35 }, { "lock", 1 } }
          }) },
        { "sources", nlohmann::json::array ({
              nlohmann::json::array ({
                  nlohmann::json::array ({
                      { { "enabled", 1 }, { "velocity", 64 }, { "probability", 100 }, { "cycle", 1 } }
                  })
              })
          }) }
    });

    REQUIRE (fixture.statuses.empty());
    REQUIRE (fixture.engine.stepCount == 8);
    REQUIRE (fixture.engine.channelCount == 2);
    REQUIRE (fixture.engine.refreshSteps == 4);
    REQUIRE (fixture.engine.generationMode == GenerationMode::perChannel);
    REQUIRE (fixture.engine.rate == "8n");
    REQUIRE (fixture.engine.tempo == 100.0);
    REQUIRE (fixture.engine.swing == 20);
    REQUIRE (fixture.engine.velocityHumanize == 12);
    REQUIRE (fixture.engine.timingHumanize == 8);
    REQUIRE (fixture.engine.channels[0].label == "Sub");
    REQUIRE (fixture.engine.channels[0].note == 35);
    REQUIRE (fixture.engine.channels[0].lock == 1);
    REQUIRE (fixture.engine.sources[0][0][0].enabled);
    REQUIRE (fixture.engine.sources[0][0][0].probability == 100);
    REQUIRE (fixture.engine.sources[0][0][0].cycle == 1);
}

TEST_CASE ("serializeForPersistence roundtrips sparse pattern", "[engine][persistence]")
{
    EngineFixture original;
    original.engine.setStepCount (8);
    original.engine.setChannelCount (2);
    original.engine.swing = 25;
    original.engine.setCell (0, 0, 0, true, 64, 30, 1, 0, false, 3);
    original.engine.channels[0].label = "Sub";
    original.engine.channels[1].note = 50;

    const auto payload = original.engine.serializeForPersistence();
    REQUIRE (payload["v"] == 1);
    REQUIRE (payload["stepCount"] == 8);
    REQUIRE (payload["channelCount"] == 2);
    REQUIRE (payload["swing"] == 25);
    REQUIRE_FALSE (payload.contains ("nativeTiming"));
    REQUIRE (payload["cells"].size() >= 1);

    const auto json = payload.dump();
    REQUIRE (json.size() < 5000);

    EngineFixture restored;
    REQUIRE (restored.engine.deserializeForPersistence (nlohmann::json::parse (json)));

    REQUIRE (restored.engine.stepCount == 8);
    REQUIRE (restored.engine.channelCount == 2);
    REQUIRE (restored.engine.swing == 25);
    REQUIRE (restored.engine.sources[0][0][0].enabled);
    REQUIRE (restored.engine.sources[0][0][0].velocity == 64);
    REQUIRE (restored.engine.sources[0][0][0].roll == 3);
    REQUIRE (restored.engine.channels[0].label == "Sub");
    REQUIRE (restored.engine.channels[1].note == 50);
}

TEST_CASE ("serializeForPersistence includes channel settings", "[engine][persistence]")
{
    EngineFixture original;
    original.engine.setStepCount (16);
    original.engine.setChannelCount (8);
    original.engine.channels[0].note = 36;
    original.engine.channels[1].note = 38;
    original.engine.channels[0].loopLength = 16;
    original.engine.channels[1].lock = 2;
    original.engine.setChannelPlaybackMode (1, PlaybackMode::boomerang);
    original.engine.setGenerationMode (GenerationMode::staticSource);
    original.engine.setCell (0, 0, 0, true, 100, 100, 1);
    original.engine.setCell (1, 1, 4, true, 80, 50, 4, 3, true, 6);

    const auto payload = original.engine.serializeForPersistence();

    EngineFixture restored;
    REQUIRE (restored.engine.deserializeForPersistence (payload));

    REQUIRE (restored.engine.channels[0].note == 36);
    REQUIRE (restored.engine.channels[1].note == 38);
    REQUIRE (restored.engine.channels[0].loopLength == 16);
    REQUIRE (restored.engine.channels[1].lock == 2);
    REQUIRE (restored.engine.channels[1].playbackMode == PlaybackMode::boomerang);
    REQUIRE (restored.engine.generationMode == GenerationMode::staticSource);
    REQUIRE (restored.engine.sources[0][0][0].enabled);
    REQUIRE (restored.engine.sources[1][1][4].enabled);
    REQUIRE (restored.engine.sources[1][1][4].velocity == 80);
    REQUIRE (restored.engine.sources[1][1][4].probability == 50);
    REQUIRE (restored.engine.sources[1][1][4].cycle == 4);
    REQUIRE (restored.engine.sources[1][1][4].cycleOffset == 3);
    REQUIRE (restored.engine.sources[1][1][4].cycleInverted);
    REQUIRE (restored.engine.sources[1][1][4].roll == 6);
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
