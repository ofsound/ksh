#include "KickSnareHatEngine.h"
#include "KshMath.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace ksh
{
namespace
{
constexpr int kNativePlaybackMaxRows = 2048;
constexpr int kNativeProbabilityMultiplier = 16;
constexpr double kTimingHumanizeNativeScale = 0.2;
constexpr double kRollNoteDurationScale = 0.9;

bool cycleGateMatches (int count, int cycle, int cycleOffset, bool cycleInverted)
{
    const bool matches = count % cycle == cycleOffset;
    return cycleInverted ? ! matches : matches;
}
} // namespace

int KickSnareHatEngine::nativePlaybackPeriod() const
{
    int period = 1;
    int baseRows = stepCount;
    bool hasVariation = velocityHumanize > 0 || timingHumanize > 0;

    for (int channel = 0; channel < channelCount; ++channel)
    {
        const auto& channelState = channels[static_cast<size_t> (channel)];
        const int loopStart = clampInt (channelState.loopStart, 0, stepCount - 1);
        const int loopLength = clampInt (channelState.loopLength, 1, stepCount - loopStart);

        if (playbackModeIsPingPong (channelState.playbackMode))
            baseRows = lcmInt (baseRows, loopLength * 2);
        else
            baseRows = lcmInt (baseRows, loopLength);
    }

    period = baseRows / stepCount;

    for (int step = 0; step < stepCount; ++step)
    {
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto& cell = generated[static_cast<size_t> (channel)][static_cast<size_t> (step)];

            if (! cell.enabled)
                continue;

            const int cycle = clampInt (cell.cycle, 1, 64);
            const int probability = clampInt (cell.probability, 0, 100);
            period = lcmInt (period, cycle);

            if (probability < 100)
                hasVariation = true;

            if (period * stepCount > kNativePlaybackMaxRows)
                return 0;
        }
    }

    if (hasVariation)
    {
        const int maxPeriod = kNativePlaybackMaxRows / std::max (1, stepCount);
        period = std::min (period * kNativeProbabilityMultiplier, maxPeriod);
    }

    return period * stepCount > kNativePlaybackMaxRows ? 0 : period;
}

bool KickSnareHatEngine::nativePlaybackSupported() const
{
    return deviceActive;
}

int KickSnareHatEngine::playbackStepForChannel (int channel, int playbackIndex) const
{
    channel = clampInt (channel, 0, Constants::maxChannels - 1);
    const auto& channelState = channels[static_cast<size_t> (channel)];
    const int loopStart = clampInt (channelState.loopStart, 0, stepCount - 1);
    const int loopLength = clampInt (channelState.loopLength, 1, stepCount - loopStart);
    const auto mode = channelState.playbackMode;
    playbackIndex = static_cast<int> (std::floor (static_cast<double> (playbackIndex)));

    if (mode == PlaybackMode::reverse)
    {
        const int activeIndex = mod (playbackIndex, loopLength);
        return loopStart + loopLength - 1 - activeIndex;
    }

    if (mode == PlaybackMode::ping_pong || mode == PlaybackMode::reverse_ping_pong)
    {
        const int period = loopLength * 2;
        const int offset = mode == PlaybackMode::reverse_ping_pong ? loopLength : 0;
        const int activeIndex = mod (playbackIndex + offset, period);
        return loopStart + (activeIndex < loopLength ? activeIndex : period - 1 - activeIndex);
    }

    return loopStart + mod (playbackIndex, loopLength);
}

std::string KickSnareHatEngine::cycleKey (int source, int channel, int step) const
{
    return std::to_string (source) + ":" + std::to_string (channel) + ":" + std::to_string (step);
}

double KickSnareHatEngine::swingDelayMsForStep (int step) const
{
    return step % 2 == 1 ? stepIntervalMs * 0.5 * (static_cast<double> (swing) / 100.0) : 0.0;
}

double KickSnareHatEngine::playbackTimingHumanizeRangeMs() const
{
    return stepIntervalMs * kTimingHumanizeNativeScale * (static_cast<double> (timingHumanize) / 100.0);
}

double KickSnareHatEngine::playbackHumanizeTimingOffsetMs()
{
    const double range = playbackTimingHumanizeRangeMs();

    if (range <= 0.0)
        return 0.0;

    return (nextRandom() * 2.0 - 1.0) * range;
}

int KickSnareHatEngine::humanizeVelocity (int velocity)
{
    velocity = clampInt (velocity, 1, 127);

    if (velocityHumanize <= 0)
        return velocity;

    const double range = static_cast<double> (velocity) * (static_cast<double> (velocityHumanize) / 100.0);
    const double humanized = static_cast<double> (velocity) + (nextRandom() * 2.0 - 1.0) * range;
    return clampInt (static_cast<int> (std::lround (humanized)), 1, 127);
}

int KickSnareHatEngine::rollNoteDurationMs (int roll) const
{
    roll = clampInt (roll, 1, Constants::maxRoll);

    if (roll <= 1)
        return Constants::defaultNoteDurationMs;

    const double subdivisionMs = stepIntervalMs / static_cast<double> (roll);
    return std::max (1,
                     std::min (Constants::defaultNoteDurationMs,
                               static_cast<int> (std::floor (subdivisionMs * kRollNoteDurationScale))));
}

void KickSnareHatEngine::appendNativeHit (NativePlaybackRow& row,
                                          int channel,
                                          int rowStep,
                                          const Cell& cell,
                                          int velocity,
                                          double delayMs,
                                          int durationMs) const
{
    const int sourceStep = cell.sourceStep;

    row.push_back ({
        channels[static_cast<size_t> (channel)].note,
        velocity,
        durationMs <= 0 ? Constants::defaultNoteDurationMs : std::max (1, durationMs),
        Constants::defaultMidiChannel,
        std::max (0.0, delayMs),
        channel + 1,
        rowStep + 1,
        cell.source + 1,
        sourceStep + 1
    });
}

NativePlaybackBuild KickSnareHatEngine::buildNativePlaybackRows (
    const std::optional<TransportProtection>& transportProtection)
{
    NativePlaybackTable rows;
    std::map<std::string, int> cycleCounters;

    int cyclePeriod = nativePlaybackPeriod();

    if (cyclePeriod < 1)
        cyclePeriod = 1;

    const int playbackStepCount = stepCount * cyclePeriod;

    std::optional<int> minimumTargetPosition;

    if (transportProtection.has_value())
    {
        minimumTargetPosition = mod (transportProtection->globalStep, playbackStepCount);

        if (! transportProtection->includeCurrent)
            minimumTargetPosition = *minimumTargetPosition + 1;
    }

    rows.resize (static_cast<size_t> (playbackStepCount));

    for (int step = 0; step < playbackStepCount; ++step)
    {
        const int rowStep = mod (step, stepCount);

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const int playbackStep = playbackStepForChannel (channel, step);

            if (playbackStep < 0)
                continue;

            const auto& cell = generated[static_cast<size_t> (channel)][static_cast<size_t> (playbackStep)];

            if (! cell.enabled)
                continue;

            const int cycle = clampInt (cell.cycle, 1, 64);
            const int cycleOffset = clampInt (cell.cycleOffset, 0, cycle - 1);
            const bool cycleInverted = cell.cycleInverted;

            if (cycle > 1 || cycleInverted)
            {
                const int sourceStep = cell.sourceStep;
                const auto key = cycleKey (cell.source, channel, sourceStep);
                const int count = cycleCounters[key]++;
                if (! cycleGateMatches (count, cycle, cycleOffset, cycleInverted))
                    continue;
            }

            const int probability = clampInt (cell.probability, 0, 100);

            if (probability <= 0)
                continue;

            if (probability < 100 && ! (nextRandom() * 100.0 < static_cast<double> (probability)))
                continue;

            const int velocity = humanizeVelocity (cell.velocity);
            const int roll = clampInt (cell.roll, 1, Constants::maxRoll);
            const int noteDurationMs = rollNoteDurationMs (roll);
            const double baseDelayMs = swingDelayMsForStep (rowStep);
            const double timingOffsetMs = playbackHumanizeTimingOffsetMs();

            for (int rollIndex = 0; rollIndex < roll; ++rollIndex)
            {
                double targetPosition = 0.0;

                if (rollIndex == 0)
                    targetPosition = static_cast<double> (step) + (baseDelayMs + timingOffsetMs) / stepIntervalMs;
                else
                    targetPosition = static_cast<double> (step) + static_cast<double> (rollIndex) / static_cast<double> (roll);

                if (targetPosition < 0.0)
                    targetPosition = 0.0;

                if (transportProtection.has_value() && targetPosition < static_cast<double> (step))
                    targetPosition = static_cast<double> (step);

                if (minimumTargetPosition.has_value()
                    && step >= *minimumTargetPosition
                    && targetPosition < static_cast<double> (*minimumTargetPosition))
                {
                    targetPosition = static_cast<double> (*minimumTargetPosition);
                }

                const int targetRowFloat = static_cast<int> (std::floor (targetPosition));
                const double targetDelayMs = (targetPosition - static_cast<double> (targetRowFloat)) * stepIntervalMs;
                const int targetRow = mod (targetRowFloat, playbackStepCount);

                appendNativeHit (rows[static_cast<size_t> (targetRow)],
                                 channel,
                                 playbackStep,
                                 cell,
                                 velocity,
                                 targetDelayMs,
                                 noteDurationMs);
            }
        }
    }

    return { std::move (rows), playbackStepCount };
}

void KickSnareHatEngine::commitNativePlaybackBuild (NativePlaybackBuild build)
{
    nativePlaybackStepCount = build.stepCount;
    nativePlaybackRows = std::move (build.rows);
}

PlaybackSnapshot KickSnareHatEngine::makePlaybackSnapshot() const
{
    PlaybackSnapshot snapshot;
    snapshot.generated = generated;
    snapshot.sources = sources;
    snapshot.sourceSettings = sourceSettings;
    snapshot.sourceChannelMutes = sourceChannelMutes;
    snapshot.channels = channels;
    snapshot.generationMode = generationMode;
    snapshot.staticSource = staticSource;
    snapshot.stepCount = stepCount;
    snapshot.channelCount = channelCount;
    snapshot.beatsPerStep = beatsPerStep();
    snapshot.tempo = tempo;
    snapshot.stepIntervalMs = stepIntervalMs;
    snapshot.swing = swing;
    snapshot.velocityHumanize = velocityHumanize;
    snapshot.timingHumanize = timingHumanize;
    snapshot.deviceActive = deviceActive;
    return snapshot;
}

int KickSnareHatEngine::currentNativePlaybackStep() const
{
    if (! transportPlaying || ! lastReportedGlobalStep.has_value())
        return -1;

    return mod (*lastReportedGlobalStep, nativePlaybackStepCount != 0 ? nativePlaybackStepCount : stepCount);
}

void KickSnareHatEngine::syncNativePlaybackTable()
{
    const bool suppressCurrentStep = ! nativeTransportRefreshInProgress;

    std::optional<TransportProtection> transportProtection;

    if (transportPlaying && lastReportedGlobalStep.has_value())
    {
        transportProtection = TransportProtection {
            *lastReportedGlobalStep,
            ! suppressCurrentStep
        };
    }

    commitNativePlaybackBuild (buildNativePlaybackRows (transportProtection));
}

int KickSnareHatEngine::globalStepForBeats (double songBeats) const
{
    if (std::isnan (songBeats))
        songBeats = 0.0;

    return static_cast<int> (std::floor ((songBeats + 0.000000001) / beatsPerStep()));
}

void KickSnareHatEngine::reportTransportStep (int globalStep)
{
    const int step = mod (globalStep, stepCount);

    if (lastReportedGlobalStep.has_value() && *lastReportedGlobalStep == globalStep)
        return;

    lastReportedGlobalStep = globalStep;
    currentStep = step;
    playingStepOneBased = step + 1;
}

void KickSnareHatEngine::prepareStepForPlayback (int step)
{
    if (step % refreshSteps != 0)
        return;

    nativeTransportRefreshInProgress = true;

    try
    {
        generateWindow (step, refreshSteps, false);
    }
    catch (...)
    {
        nativeTransportRefreshInProgress = false;
        throw;
    }

    nativeTransportRefreshInProgress = false;
}

void KickSnareHatEngine::transportPosition (double songBeats, bool isPlaying)
{
    if (std::isnan (songBeats))
        return;

    if (! deviceActive)
    {
        transportPlaying = 0;
        lastReportedGlobalStep = std::nullopt;

        if (playingStepOneBased != 0)
            playingStepOneBased = 0;

        return;
    }

    if (! isPlaying)
    {
        transportPlaying = 0;
        lastReportedGlobalStep = std::nullopt;
        return;
    }

    transportPlaying = 1;

    const int globalStep = globalStepForBeats (songBeats);
    const int step = mod (globalStep, stepCount);
    const bool stepChanged = ! lastReportedGlobalStep.has_value() || *lastReportedGlobalStep != globalStep;

    reportTransportStep (globalStep);

    if (stepChanged)
        prepareStepForPlayback (step);
}

} // namespace ksh
