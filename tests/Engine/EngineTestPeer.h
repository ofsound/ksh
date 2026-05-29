#pragma once

#include <engine/KickSnareHatEngine.h>

#include <utility>

namespace ksh::test
{

struct EngineTestPeer
{
    static void clearAll (KickSnareHatEngine& engine)
    {
        for (auto& source : engine.sources)
            source = makeEmptySourcePattern();

        engine.reset();
    }

    static void setRandomValues (KickSnareHatEngine& engine, std::vector<double> values)
    {
        engine.testRandomValues = std::move (values);
        engine.testRandomIndex = 0;
    }

    static void resetActiveSourceIndicesCallCount (const KickSnareHatEngine& engine)
    {
        engine.activeSourceIndicesCallCount = 0;
    }

    [[nodiscard]] static int activeSourceIndicesCallCount (const KickSnareHatEngine& engine)
    {
        return engine.activeSourceIndicesCallCount;
    }

    [[nodiscard]] static bool activeSourceIndicesEmpty (const KickSnareHatEngine& engine)
    {
        return engine.activeSourceIndices().empty();
    }

    static void resetRandomCallCount (const KickSnareHatEngine& engine)
    {
        engine.randomCallCount = 0;
    }

    [[nodiscard]] static int randomCallCount (const KickSnareHatEngine& engine)
    {
        return engine.randomCallCount;
    }

    static void setTransportState (KickSnareHatEngine& engine,
                                   int transportPlayingIn,
                                   std::optional<int> lastReportedGlobalStepIn)
    {
        engine.transportPlaying = transportPlayingIn;
        engine.lastReportedGlobalStep = lastReportedGlobalStepIn;
    }

    static void setPlaybackState (KickSnareHatEngine& engine, int currentStepIn, int playingStepOneBasedIn)
    {
        engine.currentStep = clampInt (currentStepIn, 0, engine.stepCount - 1);
        engine.playingStepOneBased = clampInt (playingStepOneBasedIn, 0, engine.stepCount);
    }

    static void setGeneratedCellSourceStep (KickSnareHatEngine& engine, int channel, int step, int sourceStep)
    {
        engine.generated[static_cast<size_t> (clampInt (channel, 0, Constants::maxChannels - 1))]
                        [static_cast<size_t> (clampInt (step, 0, Constants::maxSteps - 1))]
                            .sourceStep = clampInt (sourceStep, 0, Constants::maxSteps - 1);
    }
};

} // namespace ksh::test
