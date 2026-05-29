#include "KickSnareHatEngine.h"
#include "KshJson.h"

#include <algorithm>
#include <cmath>

namespace ksh
{
namespace
{
int clampSource (int source)
{
    return clampInt (source, 0, Constants::sourceCount - 1);
}

int clampChannel (int channel)
{
    return clampInt (channel, 0, Constants::maxChannels - 1);
}

int clampStep (int step)
{
    return clampInt (step, 0, Constants::maxSteps - 1);
}

int normalizeCycleOffset (int cycleOffset, int cycle)
{
    return clampInt (cycleOffset, 0, clampInt (cycle, 1, 64) - 1);
}

bool normalizeCycleInverted (bool cycleInverted, int cycle)
{
    return clampInt (cycle, 1, 64) > 1 && cycleInverted;
}

int normalizeRoll (int roll)
{
    return clampInt (roll, 1, Constants::maxRoll);
}

struct NormalizedCellParams
{
    int probability;
    int cycle;
    int cycleOffset;
    bool cycleInverted;
    int roll;
};

NormalizedCellParams normalizeCellParams (const std::optional<int>& probability,
                                          const std::optional<int>& cycle,
                                          const std::optional<int>& cycleOffset,
                                          const std::optional<bool>& cycleInverted,
                                          const std::optional<int>& roll,
                                          const Cell& currentCell)
{
    NormalizedCellParams params;
    params.probability = probability.value_or (currentCell.probability);
    params.cycle = cycle.value_or (currentCell.cycle);
    params.cycleOffset = cycleOffset.value_or (currentCell.cycleOffset);
    params.cycleInverted = cycleInverted.value_or (currentCell.cycleInverted);
    params.roll = roll.value_or (currentCell.roll);
    params.cycle = clampInt (params.cycle, 1, 64);

    return {
        clampInt (params.probability, 0, 100),
        params.cycle,
        normalizeCycleOffset (params.cycleOffset, params.cycle),
        normalizeCycleInverted (params.cycleInverted, params.cycle),
        normalizeRoll (params.roll)
    };
}

nlohmann::json cellToJson (const Cell& cell, bool oneBasedSource = false)
{
    return {
        { "enabled", cell.enabled ? 1 : 0 },
        { "velocity", cell.velocity },
        { "probability", cell.probability },
        { "cycle", cell.cycle },
        { "cycleOffset", cell.cycleOffset },
        { "cycleInverted", cell.cycleInverted ? 1 : 0 },
        { "roll", cell.roll },
        { "source", oneBasedSource ? cell.source + 1 : cell.source }
    };
}

GenerationMode generationModeFromJson (const nlohmann::json& state, GenerationMode fallback)
{
    if (! state.contains ("generationMode"))
        return fallback;

    if (state["generationMode"].is_string())
        return normalizeGenerationMode (state["generationMode"].get<std::string>());

    return fallback;
}
} // namespace

namespace
{
double defaultEngineRandom()
{
    static thread_local std::mt19937 generator { std::random_device{}() };
    static thread_local std::uniform_real_distribution<double> distribution (0.0, 1.0);
    return distribution (generator);
}
} // namespace

KickSnareHatEngine::KickSnareHatEngine (EngineCallbacks callbacksIn)
    : callbacks (std::move (callbacksIn))
{
    if (! callbacks.rng)
        callbacks.rng = defaultEngineRandom;

    initChannels();
    initSources();
    generated = makeEmptySourcePattern();
    generateWindow (0, stepCount, true);
}

EngineStateSnapshot KickSnareHatEngine::stateSnapshot() const
{
    EngineStateSnapshot snapshot;
    snapshot.stepCount = stepCount;
    snapshot.channelCount = channelCount;
    snapshot.refreshSteps = refreshSteps;
    snapshot.generationMode = generationMode;
    snapshot.staticSource = staticSource;
    snapshot.rate = rate;
    snapshot.tempo = tempo;
    snapshot.stepIntervalMs = stepIntervalMs;
    snapshot.swing = swing;
    snapshot.velocityHumanize = velocityHumanize;
    snapshot.timingHumanize = timingHumanize;
    snapshot.deviceActive = deviceActive;
    snapshot.currentStep = currentStep;
    snapshot.phaseOffsetBeats = phaseOffsetBeats;
    snapshot.playingStepOneBased = playingStepOneBased;
    snapshot.nativePlaybackStepCount = nativePlaybackStepCount;
    snapshot.transportPlaying = transportPlaying;
    snapshot.lastReportedGlobalStep = lastReportedGlobalStep;
    snapshot.nativePlaybackRows = nativePlaybackRows;
    snapshot.channels = channels;
    snapshot.sources = sources;
    snapshot.sourceChannelMutes = sourceChannelMutes;
    snapshot.generated = generated;
    snapshot.activeSourceIndicesCallCount = activeSourceIndicesCallCount;
    snapshot.randomCallCount = randomCallCount;
    return snapshot;
}

const Channel& KickSnareHatEngine::channelAt (int channel) const
{
    return channels[static_cast<size_t> (clampChannel (channel))];
}

const Cell& KickSnareHatEngine::sourceCellAt (int source, int channel, int step) const
{
    return sources[static_cast<size_t> (clampSource (source))][static_cast<size_t> (clampChannel (channel))]
                  [static_cast<size_t> (clampStep (step))];
}

const Cell& KickSnareHatEngine::generatedCellAt (int channel, int step) const
{
    return generated[static_cast<size_t> (clampChannel (channel))][static_cast<size_t> (clampStep (step))];
}

const NativePlaybackRow& KickSnareHatEngine::nativePlaybackRowAt (int step) const
{
    return nativePlaybackRows[static_cast<size_t> (clampInt (step, 0, nativePlaybackStepCount - 1))];
}

bool KickSnareHatEngine::sourceChannelMutedAt (int source, int channel) const
{
    return isSourceChannelMuted (source, channel);
}

void KickSnareHatEngine::initChannels()
{
    for (int i = 0; i < Constants::maxChannels; ++i)
        channels[static_cast<size_t> (i)] = defaultChannel (i);
}

void KickSnareHatEngine::initSources()
{
    for (auto& source : sources)
        source = makeEmptySourcePattern();

    for (auto& mutes : sourceChannelMutes)
        mutes.fill (false);
}

double KickSnareHatEngine::nextRandom() const
{
    ++randomCallCount;

    if (! testRandomValues.empty())
    {
        const auto value = testRandomValues[testRandomIndex % testRandomValues.size()];
        ++testRandomIndex;
        return value;
    }

    return callbacks.rng ? callbacks.rng() : defaultEngineRandom();
}

void KickSnareHatEngine::status (const std::string& message)
{
    if (callbacks.emitStatus)
        callbacks.emitStatus (message);
}

int KickSnareHatEngine::mod (int value, int divisor)
{
    const auto result = value % divisor;
    return result < 0 ? result + divisor : result;
}

double KickSnareHatEngine::beatsPerStep() const
{
    static constexpr std::array<std::pair<std::string_view, double>, 8> ratios {{
        { "4n", 1.0 },
        { "4nt", 2.0 / 3.0 },
        { "8n", 0.5 },
        { "8nt", 1.0 / 3.0 },
        { "16n", 0.25 },
        { "16nt", 1.0 / 6.0 },
        { "32n", 0.125 },
        { "32nt", 1.0 / 12.0 }
    }};

    for (const auto& [name, beats] : ratios)
    {
        if (name == rate)
            return beats;
    }

    return 0.25;
}

void KickSnareHatEngine::updateStepIntervalMs()
{
    const double quarterMs = 60000.0 / tempo;
    stepIntervalMs = quarterMs * beatsPerStep();
}

void KickSnareHatEngine::setStepCount (int count)
{
    stepCount = clampInt (count, 1, Constants::maxSteps);
    currentStep = mod (currentStep, stepCount);
    refreshSteps = clampInt (refreshSteps, 1, stepCount);

    for (auto& channel : channels)
        channel.loopLength = clampInt (channel.loopLength, 1, stepCount);

    recomposeWindow (0, stepCount, true);
    status ("steps " + std::to_string (stepCount));
}

void KickSnareHatEngine::setChannelCount (int count)
{
    channelCount = clampInt (count, 1, Constants::maxChannels);
    recomposeWindow (0, stepCount, true);
    status ("channels " + std::to_string (channelCount));
}

void KickSnareHatEngine::setRefreshSteps (int count)
{
    refreshSteps = clampInt (count, 1, stepCount);
    status ("refresh_steps " + std::to_string (refreshSteps));
}

void KickSnareHatEngine::setGenerationMode (GenerationMode mode)
{
    generationMode = mode;
    recomposeWindow (0, stepCount, true);
    status ("mode " + std::string { generationModeToString (generationMode) });
}

void KickSnareHatEngine::setStaticSource (int source)
{
    staticSource = clampSource (source);

    if (generationMode == GenerationMode::staticSource)
        recomposeWindow (0, stepCount, true);

    status ("static_source " + std::to_string (staticSource + 1));
}

void KickSnareHatEngine::setRate (std::string_view rateIn)
{
    rate = Constants::normalizeRate (rateIn);
    updateStepIntervalMs();
    ++playbackSnapshotVersion_;
    status ("rate " + rate);
}

void KickSnareHatEngine::setTempo (double bpm)
{
    if (std::isnan (bpm))
        bpm = 120.0;

    tempo = std::clamp (bpm, 20.0, 300.0);
    updateStepIntervalMs();
    ++playbackSnapshotVersion_;
    status ("tempo " + std::to_string (static_cast<int> (tempo)));
}

void KickSnareHatEngine::setSwing (int amount)
{
    swing = clampInt (amount, 0, 100);
    ++playbackSnapshotVersion_;
    status ("swing " + std::to_string (swing));
}

void KickSnareHatEngine::setVelocityHumanize (int amount)
{
    velocityHumanize = clampInt (amount, 0, 100);
    ++playbackSnapshotVersion_;
    status ("velocity_humanize " + std::to_string (velocityHumanize));
}

void KickSnareHatEngine::setTimingHumanize (int amount)
{
    timingHumanize = clampInt (amount, 0, 100);
    ++playbackSnapshotVersion_;
    status ("timing_humanize " + std::to_string (timingHumanize));
}

void KickSnareHatEngine::setDeviceActive (bool active)
{
    deviceActive = active;
    ++playbackSnapshotVersion_;
    status (std::string { "device_active " } + (deviceActive ? "1" : "0"));
}

void KickSnareHatEngine::setPhaseOffsetBeats (double beats)
{
    phaseOffsetBeats = std::isnan (beats) ? 0.0 : beats;
    ++playbackSnapshotVersion_;
    status ("phase_offset_beats " + std::to_string (phaseOffsetBeats));
}

void KickSnareHatEngine::setChannelLabel (int channel, std::string_view label)
{
    channel = clampChannel (channel);
    channels[static_cast<size_t> (channel)].label = std::string { label };
    status ("channel_label " + std::to_string (channel + 1) + " " + channels[static_cast<size_t> (channel)].label);
}

void KickSnareHatEngine::setChannelNote (int channel, int note)
{
    channel = clampChannel (channel);
    channels[static_cast<size_t> (channel)].note = clampInt (note, 0, 127);
    ++playbackSnapshotVersion_;
    status ("channel_note " + std::to_string (channel + 1) + " " + std::to_string (channels[static_cast<size_t> (channel)].note));
}

void KickSnareHatEngine::setChannelLock (int channel, int lock)
{
    channel = clampChannel (channel);
    channels[static_cast<size_t> (channel)].lock = clampInt (lock, -1, Constants::sourceCount - 1);
    recomposeWindow (0, stepCount, true);

    const auto& locked = channels[static_cast<size_t> (channel)].lock;
    status ("channel_lock " + std::to_string (channel + 1) + " "
            + (locked < 0 ? "random" : std::to_string (locked + 1)));
}

void KickSnareHatEngine::setChannelLoopLength (int channel, int loopLength)
{
    channel = clampChannel (channel);
    channels[static_cast<size_t> (channel)].loopLength = clampInt (loopLength, 1, stepCount);
    recomposeWindow (0, stepCount, true);
    status ("channel_loop_length " + std::to_string (channel + 1) + " "
            + std::to_string (channels[static_cast<size_t> (channel)].loopLength));
}

void KickSnareHatEngine::setChannelPlaybackMode (int channel, PlaybackMode mode)
{
    channel = clampChannel (channel);
    channels[static_cast<size_t> (channel)].playbackMode = mode;
    ++playbackSnapshotVersion_;
    status ("channel_playback_mode " + std::to_string (channel + 1) + " " + playbackModeToString (mode));
}

bool KickSnareHatEngine::isSourceChannelMuted (int source, int channel) const
{
    source = clampSource (source);
    channel = clampChannel (channel);
    return sourceChannelMutes[static_cast<size_t> (source)][static_cast<size_t> (channel)];
}

Cell KickSnareHatEngine::generatedCellFromSource (int source, int channel, int step) const
{
    source = clampSource (source);
    channel = clampChannel (channel);

    const int loopLength = clampInt (channels[static_cast<size_t> (channel)].loopLength, 1, stepCount);
    const int sourceStep = mod (step, loopLength);

    Cell cell;

    if (isSourceChannelMuted (source, channel))
    {
        cell = defaultCell();
    }
    else
    {
        cell = cloneCell (sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (sourceStep)]);
    }

    cell.source = source;
    cell.sourceStep = sourceStep;
    return cell;
}

const Cell* KickSnareHatEngine::generatedCellForSourceEdit (int source, int channel, int step) const
{
    if (channel >= channelCount || step >= stepCount)
        return nullptr;

    const auto& generatedCell = generated[static_cast<size_t> (channel)][static_cast<size_t> (step)];

    if (generationMode == GenerationMode::staticSource)
        return staticSource == source ? &generatedCell : nullptr;

    if (channels[static_cast<size_t> (channel)].lock >= 0)
        return channels[static_cast<size_t> (channel)].lock == source ? &generatedCell : nullptr;

    return generatedCell.source == source ? &generatedCell : nullptr;
}

void KickSnareHatEngine::refreshGeneratedCellsForSourceEdit (int source, int channel, int sourceStep)
{
    if (channel >= channelCount)
        return;

    const int loopLength = clampInt (channels[static_cast<size_t> (channel)].loopLength, 1, stepCount);

    if (sourceStep >= loopLength)
        return;

    bool changed = false;

    for (int generatedStep = sourceStep; generatedStep < stepCount; generatedStep += loopLength)
    {
        if (generatedCellForSourceEdit (source, channel, generatedStep) != nullptr)
        {
            generated[static_cast<size_t> (channel)][static_cast<size_t> (generatedStep)] =
                generatedCellFromSource (source, channel, generatedStep);
            changed = true;
        }
    }

    if (changed)
        markPreviewDirty (false);
}

void KickSnareHatEngine::setCell (int source,
                                  int channel,
                                  int step,
                                  bool enabled,
                                  int velocity,
                                  std::optional<int> probability,
                                  std::optional<int> cycle,
                                  std::optional<int> cycleOffset,
                                  std::optional<bool> cycleInverted,
                                  std::optional<int> roll)
{
    source = clampSource (source);
    channel = clampChannel (channel);
    step = clampStep (step);

    auto& cell = sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)];
    const auto params = normalizeCellParams (probability, cycle, cycleOffset, cycleInverted, roll, cell);

    cell.enabled = enabled;
    cell.velocity = clampInt (velocity, 1, 127);
    cell.probability = params.probability;
    cell.cycle = params.cycle;
    cell.cycleOffset = params.cycleOffset;
    cell.cycleInverted = params.cycleInverted;
    cell.roll = params.roll;

    refreshGeneratedCellsForSourceEdit (source, channel, step);
}

void KickSnareHatEngine::setCellEnabled (int source, int channel, int step, bool enabled)
{
    source = clampSource (source);
    channel = clampChannel (channel);
    step = clampStep (step);
    sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)].enabled = enabled;
    refreshGeneratedCellsForSourceEdit (source, channel, step);
}

void KickSnareHatEngine::setCellVelocity (int source, int channel, int step, int velocity)
{
    source = clampSource (source);
    channel = clampChannel (channel);
    step = clampStep (step);
    sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)].velocity =
        clampInt (velocity, 1, 127);
    refreshGeneratedCellsForSourceEdit (source, channel, step);
}

void KickSnareHatEngine::setCellProbability (int source, int channel, int step, int probability)
{
    source = clampSource (source);
    channel = clampChannel (channel);
    step = clampStep (step);
    sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)].probability =
        clampInt (probability, 0, 100);
    refreshGeneratedCellsForSourceEdit (source, channel, step);
}

void KickSnareHatEngine::setCellCycle (int source, int channel, int step, int cycleIn)
{
    source = clampSource (source);
    channel = clampChannel (channel);
    step = clampStep (step);

    auto& cell = sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)];
    cell.cycle = clampInt (cycleIn, 1, 64);
    cell.cycleOffset = normalizeCycleOffset (cell.cycleOffset, cell.cycle);
    cell.cycleInverted = normalizeCycleInverted (cell.cycleInverted, cell.cycle);
    refreshGeneratedCellsForSourceEdit (source, channel, step);
}

void KickSnareHatEngine::setCellCycleOffset (int source, int channel, int step, int cycleOffset)
{
    source = clampSource (source);
    channel = clampChannel (channel);
    step = clampStep (step);

    auto& cell = sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)];
    cell.cycleOffset = normalizeCycleOffset (cycleOffset, cell.cycle);
    refreshGeneratedCellsForSourceEdit (source, channel, step);
}

void KickSnareHatEngine::setCellCycleInverted (int source, int channel, int step, bool cycleInverted)
{
    source = clampSource (source);
    channel = clampChannel (channel);
    step = clampStep (step);

    auto& cell = sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)];
    cell.cycleInverted = normalizeCycleInverted (cycleInverted, cell.cycle);
    refreshGeneratedCellsForSourceEdit (source, channel, step);
}

void KickSnareHatEngine::setCellRoll (int source, int channel, int step, int roll)
{
    source = clampSource (source);
    channel = clampChannel (channel);
    step = clampStep (step);
    sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)].roll =
        normalizeRoll (roll);
    refreshGeneratedCellsForSourceEdit (source, channel, step);
}

void KickSnareHatEngine::setSourceChannelMute (int source, int channel, bool muted)
{
    source = clampSource (source);
    channel = clampChannel (channel);
    sourceChannelMutes[static_cast<size_t> (source)][static_cast<size_t> (channel)] = muted;
    recomposeWindow (0, stepCount, true);
    status ("source_channel_mute " + std::to_string (source + 1) + " "
            + std::to_string (channel + 1) + " " + (muted ? "1" : "0"));
}

void KickSnareHatEngine::resetSourceChannel (int source, int channel)
{
    source = clampSource (source);
    channel = clampChannel (channel);

    sourceChannelMutes[static_cast<size_t> (source)][static_cast<size_t> (channel)] = false;

    for (int step = 0; step < Constants::maxSteps; ++step)
        sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)] = defaultCell();

    channels[static_cast<size_t> (channel)].loopLength = stepCount;
    recomposeWindow (0, stepCount, true);
    status ("channel_loop_length " + std::to_string (channel + 1) + " "
            + std::to_string (channels[static_cast<size_t> (channel)].loopLength));
    status ("source_channel_reset " + std::to_string (source + 1) + " " + std::to_string (channel + 1));
}

bool KickSnareHatEngine::isSourceEmpty (int sourceIndex) const
{
    sourceIndex = clampSource (sourceIndex);

    for (int channel = 0; channel < channelCount; ++channel)
    {
        if (isSourceChannelMuted (sourceIndex, channel))
            continue;

        const int loopLength = clampInt (channels[static_cast<size_t> (channel)].loopLength, 1, stepCount);

        for (int step = 0; step < std::min (stepCount, loopLength); ++step)
        {
            if (sources[static_cast<size_t> (sourceIndex)][static_cast<size_t> (channel)][static_cast<size_t> (step)].enabled)
                return false;
        }
    }

    return true;
}

KickSnareHatEngine::ActiveSourceList KickSnareHatEngine::activeSourceIndices() const
{
    ++activeSourceIndicesCallCount;

    ActiveSourceList indices;

    for (int source = 0; source < Constants::sourceCount; ++source)
    {
        if (! isSourceEmpty (source))
            indices.add (source);
    }

    return indices;
}

int KickSnareHatEngine::pickRandomSource (const ActiveSourceList* active)
{
    ActiveSourceList computedActiveSources;
    const ActiveSourceList& activeSources = active != nullptr ? *active : (computedActiveSources = activeSourceIndices());

    if (activeSources.empty())
        return 0;

    const auto pick = clampInt (static_cast<int> (std::floor (nextRandom() * static_cast<double> (activeSources.size()))),
                              0,
                              static_cast<int> (activeSources.size()) - 1);
    return activeSources[static_cast<size_t> (pick)];
}

void KickSnareHatEngine::generateWindow (int startStep, int length, bool forceEmit)
{
    startStep = clampInt (startStep, 0, stepCount - 1);
    length = clampInt (length, 1, stepCount);

    ActiveSourceList activeSources;
    int stackSource = -1;

    if (generationMode == GenerationMode::perChannel)
        activeSources = activeSourceIndices();
    else if (generationMode == GenerationMode::stack)
    {
        activeSources = activeSourceIndices();
        stackSource = pickRandomSource (&activeSources);
    }

    for (int offset = 0; offset < length; ++offset)
    {
        const int step = mod (startStep + offset, stepCount);

        for (int channel = 0; channel < channelCount; ++channel)
        {
            int source = 0;

            if (generationMode == GenerationMode::staticSource)
            {
                source = staticSource;
            }
            else if (channels[static_cast<size_t> (channel)].lock >= 0)
            {
                source = channels[static_cast<size_t> (channel)].lock;
            }
            else if (generationMode == GenerationMode::perChannel)
            {
                source = pickRandomSource (&activeSources);
            }
            else
            {
                source = stackSource;
            }

            generated[static_cast<size_t> (channel)][static_cast<size_t> (step)] =
                generatedCellFromSource (source, channel, step);
        }
    }

    markPreviewDirty (forceEmit);
}

void KickSnareHatEngine::recomposeWindow (int startStep, int length, bool forceEmit)
{
    startStep = clampInt (startStep, 0, stepCount - 1);
    length = clampInt (length, 1, stepCount);

    ActiveSourceList activeSources;
    int initialStackSource = -1;

    if (generationMode != GenerationMode::staticSource)
        activeSources = activeSourceIndices();

    for (int offset = 0; offset < length; ++offset)
    {
        const int step = mod (startStep + offset, stepCount);

        for (int channel = 0; channel < channelCount; ++channel)
        {
            int source = 0;

            if (generationMode == GenerationMode::staticSource)
            {
                source = staticSource;
            }
            else if (channels[static_cast<size_t> (channel)].lock >= 0)
            {
                source = channels[static_cast<size_t> (channel)].lock;
            }
            else
            {
                const int existing = generated[static_cast<size_t> (channel)][static_cast<size_t> (step)].source;

                if (existing >= 0 && existing < Constants::sourceCount)
                {
                    source = existing;
                }
                else if (generationMode == GenerationMode::perChannel)
                {
                    source = pickRandomSource (&activeSources);
                }
                else
                {
                    if (initialStackSource < 0)
                        initialStackSource = pickRandomSource (&activeSources);

                    source = initialStackSource;
                }
            }

            generated[static_cast<size_t> (channel)][static_cast<size_t> (step)] =
                generatedCellFromSource (source, channel, step);
        }
    }

    markPreviewDirty (forceEmit);
}

void KickSnareHatEngine::markPreviewDirty (bool forceEmit)
{
    previewDirty = true;
    ++playbackSnapshotVersion_;

    if (forceEmit)
        flushPreview();
}

void KickSnareHatEngine::flushPreview()
{
    if (! previewDirty)
        return;

    previewDirty = false;

    if (callbacks.emitPreview)
        callbacks.emitPreview (snapshot());
}

void KickSnareHatEngine::resetPlayback (bool emitStatus)
{
    currentStep = 0;
    playingStepOneBased = 0;
    generateWindow (0, stepCount, true);

    if (emitStatus)
        status ("reset");
}

void KickSnareHatEngine::reset()
{
    resetPlayback (true);
}

std::optional<MidiNoteEvent> KickSnareHatEngine::auditionChannel (int channel)
{
    if (! deviceActive)
        return std::nullopt;

    channel = clampInt (channel, 0, channelCount - 1);

    const MidiNoteEvent note {
        channels[static_cast<size_t> (channel)].note,
        CellDefaults::velocity,
        Constants::defaultMidiChannel,
        Constants::defaultNoteDurationMs,
        0.0
    };

    if (callbacks.emitNote)
        callbacks.emitNote (note);

    return note;
}

nlohmann::json KickSnareHatEngine::snapshot() const
{
    nlohmann::json channelsOut = nlohmann::json::array();
    nlohmann::json generatedOut = nlohmann::json::array();

    for (int channel = 0; channel < channelCount; ++channel)
    {
        channelsOut.push_back ({
            { "label", channels[static_cast<size_t> (channel)].label },
            { "note", channels[static_cast<size_t> (channel)].note },
            { "lock", channels[static_cast<size_t> (channel)].lock },
            { "loopLength", channels[static_cast<size_t> (channel)].loopLength },
            { "playbackMode", playbackModeToString (channels[static_cast<size_t> (channel)].playbackMode) }
        });

        nlohmann::json row = nlohmann::json::array();

        for (int step = 0; step < stepCount; ++step)
            row.push_back (cellToJson (generated[static_cast<size_t> (channel)][static_cast<size_t> (step)], true));

        generatedOut.push_back (row);
    }

    return {
        { "stepCount", stepCount },
        { "channelCount", channelCount },
        { "refreshSteps", refreshSteps },
        { "generationMode", std::string { generationModeToString (generationMode) } },
        { "staticSource", staticSource },
        { "rate", rate },
        { "tempo", tempo },
        { "swing", swing },
        { "velocityHumanize", velocityHumanize },
        { "timingHumanize", timingHumanize },
        { "phaseOffsetBeats", phaseOffsetBeats },
        { "currentStep", currentStep + 1 },
        { "channels", channelsOut },
        { "sourceChannelMutes", sourceChannelMutes },
        { "generated", generatedOut }
    };
}

nlohmann::json KickSnareHatEngine::serializeForPersistence() const
{
    nlohmann::json cells = nlohmann::json::array();
    nlohmann::json channelsOut = nlohmann::json::array();
    nlohmann::json mutes = nlohmann::json::array();

    for (int source = 0; source < Constants::sourceCount; ++source)
    {
        for (int channel = 0; channel < channelCount; ++channel)
        {
            for (int step = 0; step < stepCount; ++step)
            {
                const auto& cell = sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)];

                if (! cell.enabled
                    && cell.velocity == 100
                    && cell.probability == 100
                    && cell.cycle == 1
                    && cell.cycleOffset == 0
                    && ! cell.cycleInverted
                    && cell.roll == 1)
                {
                    continue;
                }

                cells.push_back (nlohmann::json::array ({
                    source,
                    channel,
                    step,
                    cell.enabled ? 1 : 0,
                    cell.velocity,
                    cell.probability,
                    cell.cycle,
                    cell.cycleOffset,
                    cell.cycleInverted ? 1 : 0,
                    cell.roll
                }));
            }
        }
    }

    for (int channel = 0; channel < channelCount; ++channel)
    {
        const auto& channelState = channels[static_cast<size_t> (channel)];
        channelsOut.push_back (nlohmann::json::array ({
            channelState.label,
            channelState.note,
            channelState.lock,
            channelState.loopLength,
            playbackModeToString (channelState.playbackMode)
        }));
    }

    for (int source = 0; source < Constants::sourceCount; ++source)
    {
        nlohmann::json row = nlohmann::json::array();

        for (int channel = 0; channel < channelCount; ++channel)
            row.push_back (sourceChannelMutes[static_cast<size_t> (source)][static_cast<size_t> (channel)] ? 1 : 0);

        mutes.push_back (row);
    }

    return {
        { "v", 1 },
        { "stepCount", stepCount },
        { "channelCount", channelCount },
        { "refreshSteps", refreshSteps },
        { "generationMode", std::string { generationModeToString (generationMode) } },
        { "staticSource", staticSource },
        { "rate", rate },
        { "tempo", tempo },
        { "swing", swing },
        { "velocityHumanize", velocityHumanize },
        { "timingHumanize", timingHumanize },
        { "deviceActive", deviceActive ? 1 : 0 },
        { "phaseOffsetBeats", phaseOffsetBeats },
        { "channels", channelsOut },
        { "sourceChannelMutes", mutes },
        { "cells", cells }
    };
}

bool KickSnareHatEngine::deserializeForPersistence (const nlohmann::json& state)
{
    if (! state.contains ("v") || state["v"].get<int>() != 1)
        return false;

    stepCount = clampInt (state["stepCount"].get<int>(), 1, Constants::maxSteps);
    channelCount = clampInt (state["channelCount"].get<int>(), 1, Constants::maxChannels);
    refreshSteps = clampInt (state["refreshSteps"].get<int>(), 1, stepCount);
    generationMode = generationModeFromJson (state, GenerationMode::stack);
    staticSource = clampInt (state["staticSource"].get<int>(), 0, Constants::sourceCount - 1);
    rate = Constants::normalizeRate (state["rate"].get<std::string>());
    tempo = std::clamp (state.value ("tempo", tempo), 20.0, 300.0);
    swing = clampInt (state["swing"].get<int>(), 0, 100);
    velocityHumanize = clampInt (state["velocityHumanize"].get<int>(), 0, 100);
    timingHumanize = clampInt (state["timingHumanize"].get<int>(), 0, 100);

    if (state["deviceActive"].is_boolean())
        deviceActive = state["deviceActive"].get<bool>();
    else
        deviceActive = state["deviceActive"].get<int>() != 0;

    phaseOffsetBeats = state.value ("phaseOffsetBeats", 0.0);
    updateStepIntervalMs();

    for (auto& source : sources)
        source = makeEmptySourcePattern();

    const auto& channelsIn = state["channels"];

    for (int channel = 0; channel < channelCount; ++channel)
    {
        if (channel < static_cast<int> (channelsIn.size()))
        {
            const auto& row = channelsIn[static_cast<size_t> (channel)];
            channels[static_cast<size_t> (channel)].label = row[0].get<std::string>();
            channels[static_cast<size_t> (channel)].note = clampInt (row[1].get<int>(), 0, 127);
            channels[static_cast<size_t> (channel)].lock = clampInt (row[2].get<int>(), -1, Constants::sourceCount - 1);
            channels[static_cast<size_t> (channel)].loopLength = clampInt (row[3].get<int>(), 1, stepCount);

            if (row.size() > 4)
                channels[static_cast<size_t> (channel)].playbackMode = normalizePlaybackMode (row[4].get<std::string>());
        }

        channels[static_cast<size_t> (channel)].loopLength =
            clampInt (channels[static_cast<size_t> (channel)].loopLength, 1, stepCount);
    }

    const auto& mutesIn = state["sourceChannelMutes"];

    for (int source = 0; source < Constants::sourceCount; ++source)
    {
        for (int channel = 0; channel < channelCount; ++channel)
        {
            sourceChannelMutes[static_cast<size_t> (source)][static_cast<size_t> (channel)] =
                mutesIn[static_cast<size_t> (source)][static_cast<size_t> (channel)].get<int>() != 0;
        }
    }

    for (const auto& entry : state["cells"])
    {
        if (! entry.is_array() || entry.size() < 7)
            continue;

        const int source = entry[0].get<int>();
        const int channel = entry[1].get<int>();
        const int step = entry[2].get<int>();

        if (source < 0 || source >= Constants::sourceCount || channel < 0 || channel >= Constants::maxChannels)
            continue;

        if (step < 0 || step >= Constants::maxSteps)
            continue;

        sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)] =
            cloneCell ({
                .enabled = entry[3].get<int>() != 0,
                .velocity = entry[4].get<int>(),
                .probability = entry[5].get<int>(),
                .cycle = entry[6].get<int>(),
                .cycleOffset = entry.size() > 7 ? entry[7].get<int>() : 0,
                .cycleInverted = entry.size() > 8 ? entry[8].get<int>() != 0 : false,
                .roll = entry.size() > 9 ? entry[9].get<int>() : 1
            });
    }

    resetPlayback (false);
    return true;
}

} // namespace ksh
