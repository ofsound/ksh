#include "KickSnareHatEngine.h"
#include "KshJson.h"
#include "KshMath.h"

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

void clampChannelLoopRange (Channel& channel, int stepCount)
{
    stepCount = clampInt (stepCount, 1, Constants::maxSteps);
    channel.loopStart = clampInt (channel.loopStart, 0, stepCount - 1);
    channel.loopLength = clampInt (channel.loopLength, 1, stepCount - channel.loopStart);
}

void clampLoopRange (LoopRange& range, int stepCount)
{
    stepCount = clampInt (stepCount, 1, Constants::maxSteps);
    range.loopStart = clampInt (range.loopStart, 0, stepCount - 1);
    range.loopLength = clampInt (range.loopLength, 1, stepCount - range.loopStart);
}

void copyLoopRange (Channel& channel, const LoopRange& range)
{
    channel.loopStart = range.loopStart;
    channel.loopLength = range.loopLength;
}

int normalizeRoll (int roll)
{
    return clampInt (roll, 1, Constants::maxRoll);
}

struct NormalizedCellParams
{
    int probability;
    int cycle;
    int cycleMask;
    int roll;
};

NormalizedCellParams normalizeCellParams (const std::optional<int>& probability,
                                          const std::optional<int>& cycle,
                                          const std::optional<int>& cycleMask,
                                          const std::optional<int>& roll,
                                          const Cell& currentCell)
{
    NormalizedCellParams params;
    params.probability = probability.value_or (currentCell.probability);
    params.cycle = cycle.value_or (currentCell.cycle);
    params.cycleMask = cycleMask.value_or (currentCell.cycleMask);
    params.roll = roll.value_or (currentCell.roll);
    params.cycle = clampInt (params.cycle, 1, 64);

    return {
        clampInt (params.probability, 0, 100),
        params.cycle,
        normalizeCycleMask (params.cycleMask, params.cycle),
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
        { "cycleMask", cell.cycleMask },
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
    snapshot.swingSubdivisionIndex = swingSubdivisionIndex;
    snapshot.velocityHumanize = velocityHumanize;
    snapshot.timingHumanize = timingHumanize;
    snapshot.deviceActive = deviceActive;
    snapshot.currentStep = currentStep;
    snapshot.playingStepOneBased = playingStepOneBased;
    snapshot.nativePlaybackStepCount = nativePlaybackStepCount;
    snapshot.transportPlaying = transportPlaying;
    snapshot.lastReportedGlobalStep = lastReportedGlobalStep;
    snapshot.nativePlaybackRows = nativePlaybackRows;
    snapshot.channels = channels;
    snapshot.sources = sources;
    snapshot.sourceSettings = sourceSettings;
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

int KickSnareHatEngine::getSourceStepCount (int source) const
{
    return sourceSettings[static_cast<size_t> (clampSource (source))].stepCount;
}

std::string_view KickSnareHatEngine::getSourceRate (int source) const
{
    return sourceSettings[static_cast<size_t> (clampSource (source))].rate;
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

    for (auto& settings : sourceSettings)
        settings = defaultSourceSettings();

    for (auto& mutes : sourceChannelMutes)
        mutes.fill (false);
}

int KickSnareHatEngine::activeSettingsSource() const
{
    return staticSource >= 0 ? clampSource (staticSource) : 0;
}

void KickSnareHatEngine::applyActiveSourceSettings()
{
    const auto activeSource = activeSettingsSource();
    const auto& settings = sourceSettings[static_cast<size_t> (activeSource)];
    stepCount = clampInt (settings.stepCount, 1, Constants::maxSteps);
    rate = Constants::normalizeRate (settings.rate);
    currentStep = mod (currentStep, stepCount);
    refreshSteps = clampInt (refreshSteps, 1, stepCount);

    for (int channel = 0; channel < Constants::maxChannels; ++channel)
    {
        auto& range = sourceSettings[static_cast<size_t> (activeSource)].loopRanges[static_cast<size_t> (channel)];
        clampLoopRange (range, stepCount);
        copyLoopRange (channels[static_cast<size_t> (channel)], range);
    }

    updateStepIntervalMs();
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
    const int previousStepCount = stepCount;
    stepCount = clampInt (count, 1, Constants::maxSteps);
    const auto activeSource = activeSettingsSource();
    auto& settings = sourceSettings[static_cast<size_t> (activeSource)];
    settings.stepCount = stepCount;
    currentStep = mod (currentStep, stepCount);
    refreshSteps = clampInt (refreshSteps, 1, stepCount);

    for (int channelIndex = 0; channelIndex < Constants::maxChannels; ++channelIndex)
    {
        auto& range = settings.loopRanges[static_cast<size_t> (channelIndex)];
        if (range.loopStart == 0 && range.loopLength == previousStepCount)
            range.loopLength = stepCount;

        clampLoopRange (range, stepCount);
        copyLoopRange (channels[static_cast<size_t> (channelIndex)], range);
    }

    recomposeWindow (0, stepCount, true);
    status ("steps " + std::to_string (stepCount));
}

void KickSnareHatEngine::setSourceStepCount (int source, int count)
{
    source = clampSource (source);

    if (source == activeSettingsSource())
    {
        setStepCount (count);
        status ("source_steps " + std::to_string (source + 1) + " " + std::to_string (stepCount));
        return;
    }

    auto& settings = sourceSettings[static_cast<size_t> (source)];
    settings.stepCount = clampInt (count, 1, Constants::maxSteps);
    for (auto& range : settings.loopRanges)
        clampLoopRange (range, settings.stepCount);
    status ("source_steps " + std::to_string (source + 1) + " "
            + std::to_string (settings.stepCount));
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
    staticSource = source < 0 ? Constants::mutedStaticSource : clampSource (source);

    if (staticSource >= 0)
        applyActiveSourceSettings();

    if (generationMode == GenerationMode::staticSource)
        recomposeWindow (0, stepCount, true);

    status (staticSource < 0 ? "static_source M" : "static_source " + std::to_string (staticSource + 1));
}

void KickSnareHatEngine::setRate (std::string_view rateIn)
{
    rate = Constants::normalizeRate (rateIn);
    sourceSettings[static_cast<size_t> (activeSettingsSource())].rate = rate;
    updateStepIntervalMs();
    ++playbackSnapshotVersion_;
    status ("rate " + rate);
}

void KickSnareHatEngine::setSourceRate (int source, std::string_view rateIn)
{
    source = clampSource (source);
    sourceSettings[static_cast<size_t> (source)].rate = Constants::normalizeRate (rateIn);

    if (source == activeSettingsSource())
    {
        rate = sourceSettings[static_cast<size_t> (source)].rate;
        updateStepIntervalMs();
        ++playbackSnapshotVersion_;
    }

    status ("source_rate " + std::to_string (source + 1) + " "
            + sourceSettings[static_cast<size_t> (source)].rate);
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

void KickSnareHatEngine::setSwingSubdivisionIndex (int subdivisionIndex)
{
    swingSubdivisionIndex = clampSwingSubdivisionIndex (subdivisionIndex);
    ++playbackSnapshotVersion_;
    status ("swing_subdivision " + std::to_string (swingSubdivisionIndex));
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

void KickSnareHatEngine::setChannelLoopLength (int channel, int loopLength, int loopStart)
{
    channel = clampChannel (channel);
    auto& range = sourceSettings[static_cast<size_t> (activeSettingsSource())]
                    .loopRanges[static_cast<size_t> (channel)];
    range.loopStart = clampInt (loopStart, 0, stepCount - 1);
    range.loopLength = clampInt (loopLength, 1, stepCount - range.loopStart);
    auto& channelState = channels[static_cast<size_t> (channel)];
    copyLoopRange (channelState, range);
    recomposeWindow (0, stepCount, true);
    status ("channel_loop_length " + std::to_string (channel + 1) + " "
            + std::to_string (channelState.loopLength) + " " + std::to_string (channelState.loopStart + 1));
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
    if (source < 0)
    {
        Cell cell = defaultCell();
        cell.source = Constants::mutedStaticSource;
        cell.sourceStep = mod (step, stepCount);
        return cell;
    }

    source = clampSource (source);
    channel = clampChannel (channel);

    const int sourceStepCount = clampInt (sourceSettings[static_cast<size_t> (source)].stepCount, 1, Constants::maxSteps);
    auto range = sourceSettings[static_cast<size_t> (source)].loopRanges[static_cast<size_t> (channel)];
    clampLoopRange (range, sourceStepCount);
    const int loopStart = range.loopStart;
    const int loopLength = range.loopLength;
    const int sourceStep = loopStart + mod (step - loopStart, loopLength);

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

    source = clampSource (source);
    const int sourceStepCount = clampInt (sourceSettings[static_cast<size_t> (source)].stepCount, 1, Constants::maxSteps);
    auto range = sourceSettings[static_cast<size_t> (source)].loopRanges[static_cast<size_t> (channel)];
    clampLoopRange (range, sourceStepCount);
    const int loopStart = range.loopStart;
    const int loopLength = range.loopLength;

    if (sourceStep < loopStart || sourceStep >= loopStart + loopLength)
        return;

    bool changed = false;

    for (int generatedStep = 0; generatedStep < stepCount; ++generatedStep)
    {
        if (loopStart + mod (generatedStep - loopStart, loopLength) != sourceStep)
            continue;

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
                                  std::optional<int> cycleMask,
                                  std::optional<int> roll)
{
    source = clampSource (source);
    channel = clampChannel (channel);
    step = clampStep (step);

    auto& cell = sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)];
    const auto params = normalizeCellParams (probability, cycle, cycleMask, roll, cell);

    cell.enabled = enabled;
    cell.velocity = clampInt (velocity, 1, 127);
    cell.probability = params.probability;
    cell.cycle = params.cycle;
    cell.cycleMask = params.cycleMask;
    cell.roll = params.roll;

    refreshGeneratedCellsForSourceEdit (source, channel, step);
}

void KickSnareHatEngine::setCell (int source,
                                  int channel,
                                  int step,
                                  bool enabled,
                                  int velocity,
                                  std::optional<int> probability,
                                  std::optional<int> cycle,
                                  std::optional<int> legacyOffset,
                                  std::optional<bool> legacyInverted,
                                  std::optional<int> roll)
{
    const auto cycleLength = cycle.value_or (1);
    const auto cycleMask = cycleMaskFromLegacyOffset (legacyOffset.value_or (0),
                                                       cycleLength,
                                                       legacyInverted.value_or (false));
    setCell (source, channel, step, enabled, velocity, probability, cycle, cycleMask, roll);
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
    cell.cycleMask = normalizeCycleMask (cell.cycleMask, cell.cycle);
    refreshGeneratedCellsForSourceEdit (source, channel, step);
}

void KickSnareHatEngine::setCellCycleMask (int source, int channel, int step, int cycleMask)
{
    source = clampSource (source);
    channel = clampChannel (channel);
    step = clampStep (step);

    auto& cell = sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)];
    cell.cycleMask = normalizeCycleMask (cycleMask, cell.cycle);
    refreshGeneratedCellsForSourceEdit (source, channel, step);
}

void KickSnareHatEngine::setCellCycleOffset (int source, int channel, int step, int cycleMask)
{
    setCellCycleMask (source, channel, step, cycleMask);
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

    auto& range = sourceSettings[static_cast<size_t> (source)].loopRanges[static_cast<size_t> (channel)];
    range.loopStart = 0;
    range.loopLength = sourceSettings[static_cast<size_t> (source)].stepCount;
    clampLoopRange (range, sourceSettings[static_cast<size_t> (source)].stepCount);
    if (source == activeSettingsSource())
        copyLoopRange (channels[static_cast<size_t> (channel)], range);
    recomposeWindow (0, stepCount, true);
    status ("channel_loop_length " + std::to_string (channel + 1) + " "
            + std::to_string (range.loopLength) + " 1");
    status ("source_channel_reset " + std::to_string (source + 1) + " " + std::to_string (channel + 1));
}

void KickSnareHatEngine::copySourcePattern (int source, int destination)
{
    source = clampSource (source);
    destination = clampSource (destination);

    if (source == destination)
        return;

    sources[static_cast<size_t> (destination)] = sources[static_cast<size_t> (source)];
    sourceSettings[static_cast<size_t> (destination)] = sourceSettings[static_cast<size_t> (source)];
    sourceChannelMutes[static_cast<size_t> (destination)] = sourceChannelMutes[static_cast<size_t> (source)];

    recomposeWindow (0, stepCount, true);
    status ("source_pattern_copy " + std::to_string (source + 1) + " " + std::to_string (destination + 1));
}

bool KickSnareHatEngine::isSourceEmpty (int sourceIndex) const
{
    sourceIndex = clampSource (sourceIndex);

    for (int channel = 0; channel < channelCount; ++channel)
    {
        if (isSourceChannelMuted (sourceIndex, channel))
            continue;

        const int sourceStepCount = clampInt (sourceSettings[static_cast<size_t> (sourceIndex)].stepCount, 1, Constants::maxSteps);
        auto range = sourceSettings[static_cast<size_t> (sourceIndex)].loopRanges[static_cast<size_t> (channel)];
        clampLoopRange (range, sourceStepCount);
        const int loopStart = range.loopStart;
        const int loopLength = range.loopLength;

        for (int step = loopStart; step < loopStart + loopLength; ++step)
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
    nlohmann::json sourceSettingsOut = nlohmann::json::array();

    for (int source = 0; source < Constants::sourceCount; ++source)
    {
        const auto& settings = sourceSettings[static_cast<size_t> (source)];
        nlohmann::json loopRanges = nlohmann::json::array();
        for (const auto& range : settings.loopRanges)
            loopRanges.push_back ({ range.loopStart, range.loopLength });

        sourceSettingsOut.push_back ({
            { "stepCount", settings.stepCount },
            { "rate", settings.rate },
            { "loopRanges", loopRanges }
        });
    }

    for (int channel = 0; channel < channelCount; ++channel)
    {
        channelsOut.push_back ({
            { "label", channels[static_cast<size_t> (channel)].label },
            { "note", channels[static_cast<size_t> (channel)].note },
            { "lock", channels[static_cast<size_t> (channel)].lock },
            { "loopStart", channels[static_cast<size_t> (channel)].loopStart },
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
        { "swingSubdivisionIndex", swingSubdivisionIndex },
        { "velocityHumanize", velocityHumanize },
        { "timingHumanize", timingHumanize },
        { "currentStep", currentStep + 1 },
        { "channels", channelsOut },
        { "sourceSettings", sourceSettingsOut },
        { "sourceChannelMutes", sourceChannelMutes },
        { "generated", generatedOut }
    };
}

nlohmann::json KickSnareHatEngine::serializeForPersistence() const
{
    nlohmann::json cells = nlohmann::json::array();
    nlohmann::json channelsOut = nlohmann::json::array();
    nlohmann::json sourceSettingsOut = nlohmann::json::array();
    nlohmann::json mutes = nlohmann::json::array();

    for (int source = 0; source < Constants::sourceCount; ++source)
    {
        const int sourceSteps = sourceSettings[static_cast<size_t> (source)].stepCount;

        for (int channel = 0; channel < channelCount; ++channel)
        {
            for (int step = 0; step < sourceSteps; ++step)
            {
                const auto& cell = sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)];

                if (! cell.enabled
                    && cell.velocity == 100
                    && cell.probability == 100
                    && cell.cycle == 1
                    && cell.cycleMask == 1
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
                    cell.cycleMask,
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
            playbackModeToString (channelState.playbackMode),
            channelState.loopStart
        }));
    }

    for (int source = 0; source < Constants::sourceCount; ++source)
    {
        const auto& settings = sourceSettings[static_cast<size_t> (source)];
        nlohmann::json loopRanges = nlohmann::json::array();
        for (const auto& range : settings.loopRanges)
            loopRanges.push_back ({ range.loopStart, range.loopLength });

        sourceSettingsOut.push_back (nlohmann::json::array ({
            settings.stepCount,
            settings.rate,
            loopRanges
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
        { "swingSubdivisionIndex", swingSubdivisionIndex },
        { "velocityHumanize", velocityHumanize },
        { "timingHumanize", timingHumanize },
        { "deviceActive", deviceActive ? 1 : 0 },
        { "channels", channelsOut },
        { "sourceSettings", sourceSettingsOut },
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
    staticSource = clampInt (state["staticSource"].get<int>(), Constants::mutedStaticSource, Constants::sourceCount - 1);
    rate = Constants::normalizeRate (state["rate"].get<std::string>());
    tempo = std::clamp (state.value ("tempo", tempo), 20.0, 300.0);
    swing = clampInt (state["swing"].get<int>(), 0, 100);
    swingSubdivisionIndex = clampSwingSubdivisionIndex (
        state.value ("swingSubdivisionIndex", Constants::defaultSwingSubdivisionIndex));
    velocityHumanize = clampInt (state["velocityHumanize"].get<int>(), 0, 100);
    timingHumanize = clampInt (state["timingHumanize"].get<int>(), 0, 100);

    if (state["deviceActive"].is_boolean())
        deviceActive = state["deviceActive"].get<bool>();
    else
        deviceActive = state["deviceActive"].get<int>() != 0;

    updateStepIntervalMs();

    for (auto& settings : sourceSettings)
    {
        settings = defaultSourceSettings();
        settings.stepCount = stepCount;
        settings.rate = rate;
    }

    if (state.contains ("sourceSettings") && state["sourceSettings"].is_array())
    {
        const auto& settingsIn = state["sourceSettings"];

        for (int source = 0; source < Constants::sourceCount; ++source)
        {
            if (source >= static_cast<int> (settingsIn.size()))
                continue;

            const auto& row = settingsIn[static_cast<size_t> (source)];
            auto& settings = sourceSettings[static_cast<size_t> (source)];

            if (row.is_array())
            {
                if (! row.empty())
                    settings.stepCount = clampInt (row[0].get<int>(), 1, Constants::maxSteps);

                if (row.size() > 1)
                    settings.rate = Constants::normalizeRate (row[1].get<std::string>());

                if (row.size() > 2 && row[2].is_array())
                {
                    const auto& rangesIn = row[2];
                    for (int channel = 0; channel < Constants::maxChannels; ++channel)
                    {
                        if (channel >= static_cast<int> (rangesIn.size()) || ! rangesIn[static_cast<size_t> (channel)].is_array())
                            continue;

                        const auto& rangeIn = rangesIn[static_cast<size_t> (channel)];
                        auto& range = settings.loopRanges[static_cast<size_t> (channel)];
                        if (! rangeIn.empty())
                            range.loopStart = rangeIn[0].get<int>();
                        if (rangeIn.size() > 1)
                            range.loopLength = rangeIn[1].get<int>();
                        clampLoopRange (range, settings.stepCount);
                    }
                }
            }
            else if (row.is_object())
            {
                settings.stepCount = clampInt (row.value ("stepCount", settings.stepCount), 1, Constants::maxSteps);
                settings.rate = Constants::normalizeRate (row.value ("rate", settings.rate));

                if (row.contains ("loopRanges") && row["loopRanges"].is_array())
                {
                    const auto& rangesIn = row["loopRanges"];
                    for (int channel = 0; channel < Constants::maxChannels; ++channel)
                    {
                        if (channel >= static_cast<int> (rangesIn.size()) || ! rangesIn[static_cast<size_t> (channel)].is_array())
                            continue;

                        const auto& rangeIn = rangesIn[static_cast<size_t> (channel)];
                        auto& range = settings.loopRanges[static_cast<size_t> (channel)];
                        if (! rangeIn.empty())
                            range.loopStart = rangeIn[0].get<int>();
                        if (rangeIn.size() > 1)
                            range.loopLength = rangeIn[1].get<int>();
                        clampLoopRange (range, settings.stepCount);
                    }
                }
            }
        }
    }

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

            if (row.size() > 5)
                channels[static_cast<size_t> (channel)].loopStart = clampInt (row[5].get<int>(), 0, stepCount - 1);
        }

        clampChannelLoopRange (channels[static_cast<size_t> (channel)], stepCount);
    }

    for (int source = 0; source < Constants::sourceCount; ++source)
    {
        auto& settings = sourceSettings[static_cast<size_t> (source)];
        for (int channel = 0; channel < Constants::maxChannels; ++channel)
        {
            auto& range = settings.loopRanges[static_cast<size_t> (channel)];

            // Older v1 payloads stored one shared range on each channel. Keep
            // those projects working by using that range for every pattern.
            const bool hasSourceRanges = state.contains ("sourceSettings")
                                       && state["sourceSettings"].is_array()
                                       && source < static_cast<int> (state["sourceSettings"].size())
                                       && ((state["sourceSettings"][static_cast<size_t> (source)].is_array()
                                            && state["sourceSettings"][static_cast<size_t> (source)].size() > 2)
                                           || (state["sourceSettings"][static_cast<size_t> (source)].is_object()
                                               && state["sourceSettings"][static_cast<size_t> (source)].contains ("loopRanges")));
            if (! hasSourceRanges)
            {
                range.loopStart = channels[static_cast<size_t> (channel)].loopStart;
                range.loopLength = channels[static_cast<size_t> (channel)].loopLength;
            }
            clampLoopRange (range, settings.stepCount);
        }
    }

    if (staticSource >= 0)
        applyActiveSourceSettings();

    for (auto& mutes : sourceChannelMutes)
        for (auto& muted : mutes)
            muted = false;

    const auto& mutesIn = state["sourceChannelMutes"];

    for (int source = 0; source < Constants::sourceCount; ++source)
    {
        if (source >= static_cast<int> (mutesIn.size()))
            continue;

        const auto& muteRow = mutesIn[static_cast<size_t> (source)];

        for (int channel = 0; channel < channelCount; ++channel)
        {
            if (channel >= static_cast<int> (muteRow.size()))
                continue;

            sourceChannelMutes[static_cast<size_t> (source)][static_cast<size_t> (channel)] =
                muteRow[static_cast<size_t> (channel)].get<int>() != 0;
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

        const auto cycle = entry[6].get<int>();
        const bool legacyLayout = entry.size() >= 10;
        const auto cycleMask = legacyLayout
                                 ? cycleMaskFromLegacyOffset (entry[7].get<int>(), cycle, entry[8].get<int>() != 0)
                                 : entry.size() > 7 ? entry[7].get<int>() : 1;
        const auto roll = legacyLayout
                              ? entry[9].get<int>()
                              : entry.size() > 8 ? entry[8].get<int>() : 1;

        sources[static_cast<size_t> (source)][static_cast<size_t> (channel)][static_cast<size_t> (step)] =
            cloneCell ({
                .enabled = entry[3].get<int>() != 0,
                .velocity = entry[4].get<int>(),
                .probability = entry[5].get<int>(),
                .cycle = cycle,
                .cycleMask = cycleMask,
                .roll = roll
            });
    }

    resetPlayback (false);
    return true;
}

} // namespace ksh
