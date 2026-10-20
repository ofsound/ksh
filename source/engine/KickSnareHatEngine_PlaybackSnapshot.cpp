#include "KickSnareHatEngine.h"

namespace ksh
{
PlaybackSnapshot KickSnareHatEngine::makePlaybackSnapshot() const
{
    PlaybackSnapshot snapshot;
    fillPlaybackSnapshot (snapshot);
    return snapshot;
}

void KickSnareHatEngine::fillPlaybackSnapshot (PlaybackSnapshot& snapshot) const
{
    snapshot.generationVersion = generationVersion_;
    snapshot.generated = generated;
    snapshot.sources = sources;
    snapshot.sourceSettings = sourceSettings;
    snapshot.sourceChannelMutes = sourceChannelMutes;
    snapshot.channels = channels;
    snapshot.generationMode = generationMode;
    snapshot.staticSource = staticSource;
    snapshot.stepCount = stepCount;
    snapshot.channelCount = channelCount;
    snapshot.refreshSteps = refreshSteps;
    snapshot.beatsPerStep = beatsPerStep();
    snapshot.tempo = tempo;
    snapshot.stepIntervalMs = stepIntervalMs;
    snapshot.swing = swing;
    snapshot.swingSubdivisionIndex = swingSubdivisionIndex;
    snapshot.velocityHumanize = velocityHumanize;
    snapshot.timingHumanize = timingHumanize;
    snapshot.deviceActive = deviceActive;
    snapshot.hasEarlyCells = false;
    for (const auto& pattern : sources)
        for (const auto& channel : pattern)
            for (const auto& cell : channel)
                snapshot.hasEarlyCells = snapshot.hasEarlyCells || (cell.enabled && cell.nudge < 0);
}

} // namespace ksh
