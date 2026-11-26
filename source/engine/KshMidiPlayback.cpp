#include "KshMidiPlayback.h"
#include "KshControllerCurve.h"

#include <cmath>
#include <limits>

namespace ksh
{
namespace
{
constexpr double kTimingHumanizeNativeScale = 0.2;
constexpr double kRollNoteDurationScale = 0.9;
constexpr int kPatternSelectionSampleTolerance = 2;

double randomUnit (uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<double> (state) / static_cast<double> (UINT32_MAX);
}

int mod (int value, int divisor)
{
    if (divisor <= 0)
        return 0;

    const auto result = value % divisor;
    return result < 0 ? result + divisor : result;
}

int globalStepForBeats (double songBeats, double beatsPerStep)
{
    if (std::isnan (songBeats))
        songBeats = 0.0;

    return static_cast<int> (std::floor ((songBeats + 0.000000001) / beatsPerStep));
}

int firstGlobalStepAtOrAfter (double songBeats, double beatsPerStep)
{
    return static_cast<int> (std::ceil ((songBeats - 0.000000001) / beatsPerStep));
}

int stepOneBased (int globalStep, int stepCount)
{
    if (stepCount <= 0)
        return 0;

    return mod (globalStep, stepCount) + 1;
}

size_t cycleCounterIndex (int source, int channel, int sourceStep)
{
    return static_cast<size_t> (source) * Constants::maxChannels * Constants::maxSteps
         + static_cast<size_t> (channel) * Constants::maxSteps
         + static_cast<size_t> (sourceStep);
}

struct RowPlaybackStep
{
    int step;
    bool reversed;
    LoopRange loop;
};

RowPlaybackStep playbackStepForChannel (const PlaybackSnapshot& snapshot, int channel, int playbackIndex, int staticSource)
{
    const auto index = static_cast<size_t> (channel);
    auto mode = snapshot.channels[index].playbackMode;
    LoopRange range { 0, snapshot.generatedStepCount };
    if (staticSource >= 0)
    {
        const auto& settings = snapshot.sourceSettings[static_cast<size_t> (staticSource)];
        const int steps = clampInt (staticSource == snapshot.staticSource ? snapshot.stepCount : settings.stepCount, 1, Constants::maxSteps);
        range = settings.loopRanges[index];
        range.loopStart = clampInt (range.loopStart, 0, steps - 1);
        range.loopLength = clampInt (range.loopLength, 1, steps - range.loopStart);
        mode = settings.playbackModes[index];
    }
    const int loopStart = range.loopStart;
    const int loopLength = range.loopLength;
    if (mode == PlaybackMode::reverse)
    {
        const int activeIndex = mod (playbackIndex, loopLength);
        return { loopStart + loopLength - 1 - activeIndex, true, range };
    }

    if (mode == PlaybackMode::ping_pong || mode == PlaybackMode::reverse_ping_pong)
    {
        const int period = loopLength * 2;
        const int offset = mode == PlaybackMode::reverse_ping_pong ? loopLength : 0;
        const int activeIndex = mod (playbackIndex + offset, period);
        const bool reversed = activeIndex >= loopLength;
        return { loopStart + (reversed ? period - 1 - activeIndex : activeIndex), reversed, range };
    }

    return { loopStart + mod (playbackIndex, loopLength), false, range };
}

double timingHumanizeInSteps (const PlaybackMacros& macros)
{
    return kTimingHumanizeNativeScale * (static_cast<double> (macros.timingHumanize) / 100.0);
}

int rollNoteDurationMs (double intervalMs, int roll)
{
    roll = clampInt (roll, 1, Constants::maxRoll);

    if (roll <= 1)
        return Constants::defaultNoteDurationMs;

    const double subdivisionMs = intervalMs / static_cast<double> (roll);
    return std::max (1,
                     std::min (Constants::defaultNoteDurationMs,
                               static_cast<int> (std::floor (subdivisionMs * kRollNoteDurationScale))));
}

Cell staticSourceCellForStep (const PlaybackSnapshot& snapshot, int source, int channel, int playbackStep)
{
    source = clampInt (source, 0, Constants::sourceCount - 1);
    channel = clampInt (channel, 0, Constants::maxChannels - 1);

    const int sourceStepCount =
        clampInt (snapshot.sourceSettings[static_cast<size_t> (source)].stepCount, 1, Constants::maxSteps);
    auto range = snapshot.sourceSettings[static_cast<size_t> (source)].loopRanges[static_cast<size_t> (channel)];
    range.loopStart = clampInt (range.loopStart, 0, sourceStepCount - 1);
    range.loopLength = clampInt (range.loopLength, 1, sourceStepCount - range.loopStart);
    const int loopStart = range.loopStart;
    const int loopLength = range.loopLength;
    const int sourceStep = loopStart + mod (playbackStep - loopStart, loopLength);

    const auto& row = snapshot.sources[static_cast<size_t> (source)][static_cast<size_t> (channel)];
    Cell cell = snapshot.sourceChannelMutes[static_cast<size_t> (source)][static_cast<size_t> (channel)]
                    ? defaultCell()
                    : cloneCell (row[static_cast<size_t> (sourceStep)]);
    if (cell.span > 1 && !rollSpanFits (row, range, sourceStep, cell.span))
        cell.span = 1;
    cell.source = source;
    cell.sourceStep = sourceStep;
    return cell;
}

bool sourceIsEmpty (const PlaybackSnapshot& snapshot, int source)
{
    const auto sourceIndex = static_cast<size_t> (source);
    const int sourceStepCount = clampInt (snapshot.sourceSettings[sourceIndex].stepCount, 1, Constants::maxSteps);
    const int channelCount = clampInt (snapshot.channelCount, 0, Constants::maxChannels);

    for (int channel = 0; channel < channelCount; ++channel)
    {
        const auto channelIndex = static_cast<size_t> (channel);

        if (snapshot.sourceChannelMutes[sourceIndex][channelIndex])
            continue;

        auto range = snapshot.sourceSettings[sourceIndex].loopRanges[channelIndex];
        range.loopStart = clampInt (range.loopStart, 0, sourceStepCount - 1);
        range.loopLength = clampInt (range.loopLength, 1, sourceStepCount - range.loopStart);

        for (int step = range.loopStart; step < range.loopStart + range.loopLength; ++step)
        {
            if (snapshot.sources[sourceIndex][channelIndex][static_cast<size_t> (step)].enabled)
                return false;
        }
    }

    return true;
}
} // namespace

void MidiPlaybackRunner::prepare (double sampleRateIn)
{
    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
    resetForAudioDeviceRestart();
}

void MidiPlaybackRunner::resetForAudioDeviceRestart()
{
    // The host gives us no MIDI buffer during release/prepare. Keep the keys
    // until a callback can release them, even through consecutive resets.
    if (activeKeyCount != 0)
    {
        std::copy_n (activeKeys.begin(), activeKeyCount, restartNoteOffKeys.begin());
        restartNoteOffCount = activeKeyCount;
    }
    reset();
}

void MidiPlaybackRunner::reset (bool clearAuditions)
{
    blockStartSample = 0;
    nextBlockStartSample = 0;
    wasPlaying = false;
    expectedNextPpq = 0.0;
    loopBeatOffset = 0.0;
    lastEmittedGlobalStep = std::nullopt;
    lastScheduledGlobalStep.reset();
    anticipatedLoopStep.reset();
    adoptedGenerationVersion = std::nullopt;
    clearPending();
    pendingStaticSourceOverride = noStaticSourceOverride;
    previousHostPattern.reset();
    clearRecordingMonitors();
    resetCycleCounters();
    rngState = 0x12345678u;
    controllerRngState = 0x12345678u;
    controllerPointOrder = 0;

    if (! clearAuditions)
        return;

    MidiNoteEvent drained;

    while (pendingAuditions.try_dequeue (drained))
    {
    }
}

void MidiPlaybackRunner::resetCycleCounters()
{
    cycleCounters.fill (0);
    splitCycles->fill ({});
}

void MidiPlaybackRunner::queueAuditionNote (const MidiNoteEvent& note)
{
    [[maybe_unused]] const bool queued = pendingAuditions.try_enqueue (note);
}

void MidiPlaybackRunner::suppressRecordedHit (int source, int channel, int step, int64_t sampleOffset)
{
    const auto index = cycleCounterIndex (source, channel, step);
    if (index < recordedHitSamples.size())
        recordedHitSamples[index] = nextBlockStartSample + sampleOffset;
}

void MidiPlaybackRunner::clearRecordingMonitors()
{
    recordedHitSamples.fill (std::numeric_limits<int64_t>::min());
}

bool MidiPlaybackRunner::wasRecordedHit (const NativeHit& hit, int64_t sample) const
{
    const auto index = cycleCounterIndex (hit.uiSource - 1, hit.uiChannel - 1, hit.uiSourceStep - 1);
    if (index >= recordedHitSamples.size() || recordedHitSamples[index] == std::numeric_limits<int64_t>::min())
        return false;
    return std::abs (sample - recordedHitSamples[index]) <= recordingSuppressionTolerance;
}

void MidiPlaybackRunner::clearPending()
{
    for (size_t i = 0; i < activeKeyCount; ++i)
        gateDeadlines[activeKeys[i]] = 0;
    activeKeyCount = 0;
    pendingNoteOnCount = 0;
    blockAttackCount = 0;
    controllerPointCount = 0;
    controllerRamps.fill ({});
}

void MidiPlaybackRunner::emitRestartNoteOffs (juce::MidiBuffer& midiOut)
{
    for (size_t i = 0; i < restartNoteOffCount; ++i)
    {
        const auto key = restartNoteOffKeys[i];
        midiOut.addEvent (juce::MidiMessage::noteOff (static_cast<int> (key / 128) + 1, static_cast<int> (key % 128)), 0);
    }
    restartNoteOffCount = 0;
}

void MidiPlaybackRunner::releaseHeldNotes (juce::MidiBuffer& midiOut)
{
    emitRestartNoteOffs (midiOut);
    for (size_t i = 0; i < activeKeyCount; ++i)
    {
        const auto key = activeKeys[i];
        midiOut.addEvent (juce::MidiMessage::noteOff (static_cast<int> (key / 128) + 1, static_cast<int> (key % 128)), 0);
    }
    clearPending();
}

void MidiPlaybackRunner::suspend (juce::MidiBuffer& midiOut)
{
    releaseHeldNotes (midiOut);
    resetCycleCounters();
    wasPlaying = false;
    lastEmittedGlobalStep = std::nullopt;
}

GeneratedSourcesFrame MidiPlaybackRunner::generatedSourcesFrame() const
{
    return { generatedSources, adoptedGenerationVersion.value_or (0) };
}

void MidiPlaybackRunner::adoptGeneratedSources (const PlaybackSnapshot& snapshot)
{
    for (size_t channel = 0; channel < generatedSources.size(); ++channel)
    {
        for (size_t step = 0; step < generatedSources[channel].size(); ++step)
        {
            const int source = clampInt (snapshot.generated[channel][step].source,
                                         Constants::mutedStaticSource,
                                         Constants::sourceCount - 1);
            generatedSources[channel][step] = static_cast<int8_t> (source);
        }
    }

    adoptedGenerationVersion = snapshot.generationVersion;
}

bool MidiPlaybackRunner::regenerateWindowIfDue (const PlaybackSnapshot& snapshot, int globalStep)
{
    if (snapshot.generationMode == GenerationMode::staticSource)
        return false;

    const int stepCount = snapshot.generatedStepCount;
    const int refreshSteps = clampInt (snapshot.refreshSteps, 1, stepCount);
    const int windowStart = mod (globalStep, stepCount);

    if (windowStart % refreshSteps != 0)
        return false;

    std::array<int, Constants::sourceCount> activeSources {};
    int activeCount = 0;

    for (int source = 0; source < Constants::sourceCount; ++source)
    {
        if (! sourceIsEmpty (snapshot, source))
            activeSources[static_cast<size_t> (activeCount++)] = source;
    }

    auto pickSource = [this, &activeSources, activeCount]
    {
        if (activeCount == 0)
            return 0;

        const int pick = clampInt (static_cast<int> (std::floor (nextRandomUnit() * static_cast<double> (activeCount))),
                                   0,
                                   activeCount - 1);
        return activeSources[static_cast<size_t> (pick)];
    };

    const int stackSource = snapshot.generationMode == GenerationMode::stack ? pickSource() : 0;
    const int channelCount = clampInt (snapshot.channelCount, 0, Constants::maxChannels);

    for (int offset = 0; offset < refreshSteps; ++offset)
    {
        const auto step = static_cast<size_t> (mod (windowStart + offset, stepCount));

        for (int channel = 0; channel < channelCount; ++channel)
        {
            const int lock = snapshot.channels[static_cast<size_t> (channel)].lock;
            int source = stackSource;

            if (lock >= 0)
                source = lock;
            else if (snapshot.generationMode == GenerationMode::perChannel)
                source = pickSource();

            generatedSources[static_cast<size_t> (channel)][step] =
                static_cast<int8_t> (clampInt (source, Constants::mutedStaticSource, Constants::sourceCount - 1));
        }
    }

    return true;
}

Cell MidiPlaybackRunner::generatedCellForStep (const PlaybackSnapshot& snapshot, int channel, int playbackStep) const
{
    const int source = generatedSources[static_cast<size_t> (channel)][static_cast<size_t> (playbackStep)];

    if (source < 0 || source >= Constants::sourceCount)
        return defaultCell();

    return staticSourceCellForStep (snapshot, source, channel, playbackStep);
}

std::pair<int, Cell> MidiPlaybackRunner::playbackCell (const PlaybackSnapshot& snapshot, int channel, int source, int globalStep) const
{
    const auto position = playbackStepForChannel (snapshot, channel, globalStep, source);
    const auto cellAtStep = [&] (int step)
    {
        auto cell = source >= 0 ? staticSourceCellForStep (snapshot, source, channel, step)
                                : generatedCellForStep (snapshot, channel, step);
        if (step + cell.span > position.loop.loopStart + position.loop.loopLength)
            cell.span = 1;
        return cell;
    };
    auto cell = cellAtStep (position.step);
    if (snapshot.hasSpannedCells && position.reversed)
    {
        // On a reverse pass, the right edge triggers the split group.
        if (cell.enabled && cell.span > 1)
            return { position.step, {} };
        if (!cell.enabled)
            for (int anchor = position.step - 1; anchor >= position.loop.loopStart; --anchor)
            {
                const auto candidate = cellAtStep (anchor);
                if (candidate.enabled && anchor + candidate.span - 1 == position.step)
                    return { anchor, candidate };
            }
    }
    return { position.step, cell };
}

void MidiPlaybackRunner::dropPendingNoteOnsAtOrAfter (int sampleOffset)
{
    if (pendingNoteOnCount == 0)
        return;

    sampleOffset = std::max (0, sampleOffset);
    size_t writeIndex = 0;

    for (size_t i = 0; i < pendingNoteOnCount; ++i)
    {
        const auto pending = pendingNoteOns[i];

        if (sampleOffsetForBeat (pending.beat) < sampleOffset)
            pendingNoteOns[writeIndex++] = pending;
    }

    pendingNoteOnCount = writeIndex;
}

double MidiPlaybackRunner::nextRandomUnit()
{
    return randomUnit (rngState);
}

double MidiPlaybackRunner::nextTimingOffset (double range)
{
    // Inverse CDF of a symmetric triangle, using one draw per timing offset.
    const double unit = nextRandomUnit();
    const double offset = unit < 0.5 ? std::sqrt (2.0 * unit) - 1.0
                                     : 1.0 - std::sqrt (2.0 * (1.0 - unit));
    return offset * range;
}

int MidiPlaybackRunner::sampleOffsetForBeat (double beat) const
{
    return static_cast<int> (std::clamp (std::round ((beat - blockStartBeat) / beatsPerSample),
                                        static_cast<double> (std::numeric_limits<int>::min()),
                                        static_cast<double> (std::numeric_limits<int>::max())));
}

bool MidiPlaybackRunner::collectAttack (int onSample, int midiChannel, int pitch, int velocity, int durationMs)
{
    if (blockAttackCount >= maxBlockAttacks)
        return false;

    const auto key = static_cast<size_t> (clampInt (midiChannel, 1, 16) - 1) * 128
                   + static_cast<size_t> (clampInt (pitch, 0, 127));
    const int durationSamples = std::max (1, static_cast<int> (std::llround (durationMs * sampleRate / 1000.0)));
    blockAttacks[blockAttackCount] = { onSample, durationSamples, blockAttackCount, key,
                                      static_cast<juce::uint8> (clampInt (velocity, 1, 127)) };
    ++blockAttackCount;
    return true;
}

void MidiPlaybackRunner::scheduleHit (double beat,
                                      int numSamples,
                                      const NativeHit& hit,
                                      MidiPlaybackResult& result,
                                      int earliestHitSample,
                                      int span,
                                      int roll,
                                      bool rowMuted)
{
    const int offset = sampleOffsetForBeat (beat);
    const int onSample = std::max ({ 0, earliestHitSample, offset });

    if (onSample >= numSamples)
    {
        if (pendingNoteOnCount < pendingNoteOns.size())
            pendingNoteOns[pendingNoteOnCount++] = { std::max (beat, blockStartBeat + earliestHitSample * beatsPerSample), hit, span, roll };
        return;
    }

    if (!rowMuted && !wasRecordedHit (hit, blockStartSample + offset)
        && collectAttack (onSample, hit.midiChannel, hit.pitch, hit.velocity, hit.durationMs))
        result.addNoteHit (hit);
}

void MidiPlaybackRunner::evaluateGlobalStep (const PlaybackSnapshot& snapshot,
                                             const PlaybackMacros& macros,
                                             int globalStep,
                                             double stepBeat,
                                             int numSamples,
                                             MidiPlaybackResult& result,
                                             MidiPatternSelectionEvent selection,
                                             double beatsPerStep,
                                             int earliestHitSample,
                                             int spanEndSample,
                                             bool skipElapsedHits)
{
    const double stepDurationMs = beatsPerStep * 1000.0 / (sampleRate * beatsPerSample);
    if (! macros.deviceActive || snapshot.stepCount <= 0 || snapshot.channelCount <= 0)
        return;

    const int source = selection.source;
    if (source == Constants::mutedStaticSource)
        return;

    earliestHitSample = std::max (earliestHitSample, selection.samplePosition);
    for (int channel = 0; channel < snapshot.channelCount; ++channel)
    {
        const auto& row = snapshot.channels[static_cast<size_t> (channel)];
        const auto [playbackStep, cell] = playbackCell (snapshot, channel, source, globalStep);

        if (! cell.enabled)
            continue;

        const int cycle = clampInt (cell.cycle, 1, 64);
        const auto cycleIndex = cycleCounterIndex (cell.source, channel, cell.sourceStep);

        // A cell's cycle phase restarts only when its own cycle definition changes.
        if (cycleIndex < cycleDefinitions.size())
        {
            const CycleDefinition definition { cycle, cell.cycleMask };

            if (cycleDefinitions[cycleIndex] != definition)
            {
                cycleDefinitions[cycleIndex] = definition;
                cycleCounters[cycleIndex] = 0;
            }
        }

        int cycleStep = 1;
        if (cycle > 1 || cell.cycleMask != 1)
        {
            if (cycleIndex >= cycleCounters.size())
                continue;

            const int count = static_cast<int> (cycleCounters[cycleIndex]++);
            cycleStep = count % cycle + 1;
        }

        NativeCycleVisit visit { cell.source + 1, channel + 1, cell.sourceStep + 1, cycleStep, {} };
        const int roll = cell.roll;
        std::array<bool, Constants::maxRoll> splitCycleAllows {};
        if (roll > 1)
            for (int part = 0; part < roll; ++part)
            {
                const auto index = static_cast<size_t> (part);
                const auto& gate = cell.splitGates[index];
                int phase = 0;
                if (cycleIndex < splitCycles->size())
                {
                    auto& state = (*splitCycles)[cycleIndex][index];
                    if (state.definition != gate.cycleDefinition())
                        state = { gate.cycleDefinition(), 0 };
                    phase = state.nextPhase;
                    state.nextPhase = static_cast<uint8_t> ((phase + 1) % gate.cycle());
                }
                splitCycleAllows[index] = (gate.cycleMask() & (1 << phase)) != 0;
                visit.splitSteps[index] = static_cast<uint8_t> (phase + 1);
            }
        if (result.cycleVisitCount < result.cycleVisits.size())
            result.cycleVisits[result.cycleVisitCount++] = visit;

        if (! cycleGateMatches (cycleStep - 1, cycle, cell.cycleMask))
            continue;

        const int probability = clampInt (cell.probability, 0, 100);

        if (probability <= 0)
            continue;

        auto& rowRng = row.controller ? controllerRngState : rngState;
        if (probability < 100 && ! (randomUnit (rowRng) * 100.0 < static_cast<double> (probability)))
            continue;

        const double velocityVariation = !row.controller && macros.velocityHumanize > 0 ? nextRandomUnit() * 2.0 - 1.0 : 0.0;

        const bool spanned = cell.span > 1;
        const double intervalSteps = spanned ? cell.span : 1;
        const double intervalMs = stepDurationMs * intervalSteps;
        const int noteDurationMs = rollNoteDurationMs (intervalMs, roll);
        const double timingRange = row.controller ? 0.0 : timingHumanizeInSteps (macros);
        const double groupTiming = spanned && timingRange > 0.0 ? nextTimingOffset (timingRange) : 0.0;
        const int pitch = row.note;

        for (int rollIndex = 0; rollIndex < roll; ++rollIndex)
        {
            if (roll > 1)
            {
                const auto index = static_cast<size_t> (rollIndex);
                const int chance = cell.splitGates[index].probability();
                if (!splitCycleAllows[index] || chance == 0 || (chance < 100 && randomUnit (rowRng) * 100.0 >= chance))
                    continue;
            }
            const double timingSteps = !spanned && timingRange > 0.0 ? nextTimingOffset (timingRange) : groupTiming;
            const double delaySteps = splitOffsetInSteps (cell, rollIndex, globalStep, macros.swing, snapshot.swingSubdivisionIndex, timingSteps);
            const double beat = stepBeat + delaySteps * beatsPerStep;

            if (spanned || skipElapsedHits)
            {
                const int offset = sampleOffsetForBeat (beat);
                if (spanned && offset >= spanEndSample)
                    break;
                if (skipElapsedHits && offset < earliestHitSample)
                    continue;
            }

            if (row.controller)
            {
                scheduleController (snapshot, macros, cell, rollIndex, channel, source, globalStep, stepBeat,
                                    beatsPerStep, beat, earliestHitSample, spanEndSample);
                continue;
            }

            const int baseVelocity = roll > 1 ? cell.splitVelocity (rollIndex) : cell.velocity;
            const double range = static_cast<double> (baseVelocity) * (static_cast<double> (macros.velocityHumanize) / 100.0);
            const int velocity = clampInt (static_cast<int> (std::lround (baseVelocity + velocityVariation * range)), 1, 127);
            scheduleHit (beat,
                         numSamples,
                         NativeHit {
                             pitch,
                             velocity,
                             noteDurationMs,
                             Constants::defaultMidiChannel,
                             delaySteps * stepDurationMs,
                             channel + 1,
                             playbackStep + 1,
                             cell.source + 1,
                             cell.sourceStep + 1,
                             cycleStep
                         },
                         result,
                         earliestHitSample,
                         cell.span,
                         roll, (macros.mutedRows & (1u << channel)) != 0);
        }
    }
}

void MidiPlaybackRunner::scheduleController (const PlaybackSnapshot& snapshot, const PlaybackMacros& macros,
                                             const Cell& cell, int part, int rowIndex, int source, int globalStep,
                                             double stepBeat, double beatsPerStep, double beat, int earliestSample, int endSample)
{
    const auto& row = snapshot.channels[static_cast<size_t> (rowIndex)];
    beat = std::max (beat, blockStartBeat + earliestSample * beatsPerSample);
    if (controllerPointCount == controllerPoints->size() || beat >= blockStartBeat + endSample * beatsPerSample)
        return;

    auto& point = (*controllerPoints)[controllerPointCount++];
    point = { beat, beat, rowIndex, cell.source, cell.sourceStep, row.ccNumber, row.ccChannel,
              cell.roll == 1 ? cell.controllerValue() : cell.splitControllerValue (part), part, cell.roll, cell.span };
    point.order = controllerPointOrder++;
    if (cell.ccGlide == 0 || !cell.controllerPointUnconditional (part))
        return;

    const auto connect = [&] (const Cell& next, int nextPart, int distance)
    {
        // A skipped point must never be approached by a ramp. Conditional points
        // use Hold so that they can change the destination only when they fire.
        if (next.source != cell.source || !next.controllerPointUnconditional (nextPart))
            return;
        const auto offset = splitOffsetInSteps (next, nextPart, globalStep + distance, macros.swing, snapshot.swingSubdivisionIndex);
        const double endBeat = stepBeat + (distance + offset) * beatsPerStep;
        if (endBeat <= beat)
            return;
        point.endBeat = endBeat;
        point.endStep = next.sourceStep;
        point.endPart = nextPart;
        point.endRoll = next.roll;
        point.endValue = next.roll == 1 ? next.controllerValue() : next.splitControllerValue (nextPart);
        point.intervals = controllerGlideIntervals (endBeat - beat, point.value, point.endValue);
    };
    if (part + 1 < cell.roll)
    {
        connect (cell, part + 1, 0);
    }
    else
    {
        const auto loop = playbackStepForChannel (snapshot, rowIndex, globalStep, source).loop;
        for (int distance = 1; distance < 2 * loop.loopLength; ++distance)
        {
            const int nextStep = globalStep + distance;
            if (source < 0 && mod (nextStep, snapshot.generatedStepCount) % std::max (1, snapshot.refreshSteps) == 0)
                break;
            const auto next = playbackCell (snapshot, rowIndex, source, nextStep).second;
            if (!next.enabled)
                continue;
            connect (next, 0, distance);
            break;
        }
    }
}

void MidiPlaybackRunner::renderControllers (const PlaybackSnapshot& snapshot, const PlaybackMacros& macros,
                                            const MidiPatternSelectionBlock& selections,
                                            int sourceAtStart, int numSamples, int endSample, juce::MidiBuffer& midi)
{
    const auto matches = [&] (const ControllerPoint& point, int source)
    {
        if (point.row >= snapshot.channelCount || point.source < 0 || (macros.mutedRows & (1u << point.row)) != 0
            || ((macros.patternSource || snapshot.generationMode == GenerationMode::staticSource) && point.source != source))
            return false;
        const auto rowIndex = static_cast<size_t> (point.row);
        const auto sourceIndex = static_cast<size_t> (point.source);
        const auto& row = snapshot.channels[rowIndex];
        const auto& cells = snapshot.sources[sourceIndex][rowIndex];
        const auto& cell = cells[static_cast<size_t> (point.step)];
        const auto loop = snapshot.sourceSettings[sourceIndex].loopRanges[rowIndex];
        return row.controller && row.ccNumber == point.number && row.ccChannel == point.channel
            && !snapshot.sourceChannelMutes[sourceIndex][rowIndex] && cell.enabled && cell.roll == point.roll
            && (point.span == 1 || (cell.span == point.span && rollSpanFits (cells, loop, point.step, point.span)))
            && (cell.roll == 1 ? cell.controllerValue() : cell.splitControllerValue (point.part)) == point.value;
    };
    const auto send = [&midi] (const ControllerPoint& point, int value, int sample)
    {
        midi.addEvent (juce::MidiMessage::controllerEvent (point.channel, point.number, value), sample);
    };
    const auto canGlide = [&] (const ControllerPoint& point)
    {
        const auto rowIndex = static_cast<size_t> (point.row);
        const auto& row = snapshot.sources[static_cast<size_t> (point.source)][rowIndex];
        const auto& start = row[static_cast<size_t> (point.step)];
        const auto& end = row[static_cast<size_t> (point.endStep)];
        return start.ccGlide != 0 && end.enabled && end.roll == point.endRoll
            && start.controllerPointUnconditional (point.part) && end.controllerPointUnconditional (point.endPart)
            && (end.roll == 1 ? end.controllerValue() : end.splitControllerValue (point.endPart)) == point.endValue;
    };
    for (auto& ramp : controllerRamps)
        if (ramp.next != 0 && (!matches (ramp.point, sourceAtStart) || !canGlide (ramp.point)))
            ramp.next = 0;
    const int renderEnd = std::min (numSamples, endSample);
    const auto advance = [&] (ControllerRamp& ramp, int until)
    {
        const auto& point = ramp.point;
        while (ramp.next > 0 && ramp.next < point.intervals)
        {
            const double fraction = static_cast<double> (ramp.next) / point.intervals;
            const int sample = sampleOffsetForBeat (point.beat + (point.endBeat - point.beat) * fraction);
            if (sample >= until)
                return;
            if (sample >= 0)
                send (point, controllerGlideValue (point.value, point.endValue, fraction), sample);
            ++ramp.next;
        }
        ramp.next = 0;
    };

    auto& points = *controllerPoints;
    std::sort (points.begin(), points.begin() + static_cast<ptrdiff_t> (controllerPointCount),
               [] (const auto& left, const auto& right)
               {
                   return std::tie (left.beat, left.order) < std::tie (right.beat, right.order);
               });
    int source = sourceAtStart;
    size_t selectionIndex = 0;
    const auto applySelections = [&] (int until)
    {
        if (snapshot.generationMode != GenerationMode::staticSource)
            return;
        while (selectionIndex < selections.count && selections.events[selectionIndex].samplePosition <= until)
        {
            const auto& selection = selections.events[selectionIndex++];
            if (source == selection.source)
                continue;
            for (auto& ramp : controllerRamps)
            {
                advance (ramp, selection.samplePosition);
                ramp.next = 0;
            }
            source = selection.source;
        }
    };
    size_t pending = 0;
    for (size_t i = 0; i < controllerPointCount; ++i)
    {
        const auto point = points[i];
        const int sample = std::max (0, sampleOffsetForBeat (point.beat));
        if (sample >= renderEnd)
        {
            points[pending++] = point;
            continue;
        }
        applySelections (sample);
        if (!matches (point, source))
            continue;
        auto& ramp = controllerRamps[static_cast<size_t> (point.row)];
        advance (ramp, sample);
        send (point, point.value, sample);
        ramp = { point, point.intervals > 1 && canGlide (point) ? 1 : 0 };
    }
    controllerPointCount = pending;
    applySelections (renderEnd - 1);
    for (auto& ramp : controllerRamps)
        advance (ramp, renderEnd);
}

void MidiPlaybackRunner::collectPendingNoteOns (const PlaybackSnapshot& snapshot, const PlaybackMacros& macros,
                                                int activeSource, int numSamples,
                                                double stepDurationMs, MidiPlaybackResult& result)
{
    if (pendingNoteOnCount == 0)
        return;

    size_t writeIndex = 0;

    for (size_t i = 0; i < pendingNoteOnCount; ++i)
    {
        const auto pending = pendingNoteOns[i];
        if (snapshot.channels[static_cast<size_t> (pending.hit.uiChannel - 1)].controller)
            continue;
        const int offset = sampleOffsetForBeat (pending.beat);
        if (pending.span > 1)
        {
            const int source = pending.hit.uiSource - 1;
            if ((macros.patternSource || snapshot.generationMode == GenerationMode::staticSource) && source != activeSource)
                continue;
            const auto anchor = staticSourceCellForStep (snapshot, source, pending.hit.uiChannel - 1, pending.hit.uiSourceStep - 1);
            if (!anchor.enabled || anchor.sourceStep != pending.hit.uiSourceStep - 1 || anchor.span != pending.span)
                continue;
        }

        if (offset < numSamples)
        {
            if ((macros.mutedRows & (1u << (pending.hit.uiChannel - 1))) != 0)
                continue;
            auto hit = pending.hit;
            hit.durationMs = rollNoteDurationMs (stepDurationMs * pending.span, pending.roll);
            if (!wasRecordedHit (hit, blockStartSample + offset)
                && collectAttack (std::max (0, offset), hit.midiChannel, hit.pitch, hit.velocity, hit.durationMs))
                result.addNoteHit (hit);
        }
        else
        {
            pendingNoteOns[writeIndex++] = pending;
        }
    }

    pendingNoteOnCount = writeIndex;
}

void MidiPlaybackRunner::collectAuditionNotes (int numSamples, double callbackTimeMs)
{
    MidiNoteEvent note;

    while (pendingAuditions.try_dequeue (note))
    {
        const int delaySamples = static_cast<int> (std::llround (note.delayMs * sampleRate / 1000.0));
        // Match MIDI playthrough's block phasing: a stable one-block scheduling
        // delay rather than moving every keypress to sample zero of the next callback.
        const int phaseSamples = note.timestampMs > 0.0 && callbackTimeMs > 0.0
                                     ? static_cast<int> (std::llround (numSamples + (note.timestampMs - callbackTimeMs) * sampleRate / 1000.0))
                                     : 0;
        const int onSample = std::clamp (phaseSamples + delaySamples, 0, std::max (0, numSamples - 1));
        collectAttack (onSample, note.channel, note.pitch, note.velocity, note.durationMs);
    }
}

void MidiPlaybackRunner::renderAttacksAndGates (int numSamples, juce::MidiBuffer& midi)
{
    auto* const begin = blockAttacks.get();
    auto* const end = begin + blockAttackCount;
    // Nudges, swing, splits and auditions can all reorder attacks within a block.
    std::sort (begin, end, [] (const BlockAttack& a, const BlockAttack& b)
    {
        return a.sampleOffset < b.sampleOffset || (a.sampleOffset == b.sampleOffset && a.order < b.order);
    });

    const auto emitOff = [&] (size_t key, int64_t deadline)
    {
        midi.addEvent (juce::MidiMessage::noteOff (static_cast<int> (key / 128) + 1, static_cast<int> (key % 128)),
                       static_cast<int> (std::max<int64_t> (0, deadline - blockStartSample)));
    };

    for (auto* attack = begin; attack != end; ++attack)
    {
        const auto key = attack->key;
        const auto onset = blockStartSample + attack->sampleOffset;
        auto& deadline = gateDeadlines[key];
        if (deadline != 0)
            emitOff (key, std::min (deadline, onset));
        else
            activeKeys[activeKeyCount++] = key;

        midi.addEvent (juce::MidiMessage::noteOn (static_cast<int> (key / 128) + 1, static_cast<int> (key % 128), attack->velocity),
                       attack->sampleOffset);
        deadline = onset + attack->durationSamples;
    }

    size_t remaining = 0;
    const auto blockEnd = blockStartSample + numSamples;
    for (size_t i = 0; i < activeKeyCount; ++i)
    {
        const auto key = activeKeys[i];
        auto& deadline = gateDeadlines[key];
        if (deadline < blockEnd)
        {
            emitOff (key, deadline);
            deadline = 0;
        }
        else
            activeKeys[remaining++] = key;
    }
    activeKeyCount = remaining;
    blockAttackCount = 0;
}

MidiPlaybackResult MidiPlaybackRunner::processBlock (const PlaybackSnapshot& snapshot,
                                                     double ppqPosition,
                                                     double bpm,
                                                     bool isPlaying,
                                                     int numSamples,
                                                     juce::MidiBuffer& midiOut,
                                                     const MidiPatternSelectionBlock& liveSelections,
                                                     const HostLoopRange& hostLoop,
                                                     double callbackTimeMs,
                                                     std::optional<int> endGlobalStepExclusive,
                                                     int firstPlaybackSample,
                                                     const PlaybackMacros* blockMacros)
{
    const auto& macros = blockMacros != nullptr ? *blockMacros : static_cast<const PlaybackMacros&> (snapshot);
    const bool useStaticSource = macros.patternSource || snapshot.generationMode == GenerationMode::staticSource;
    const MidiPatternSelectionBlock noSelections;
    const auto& patternSelections = macros.patternSource ? noSelections : liveSelections;
    MidiPlaybackResult result;

    if (numSamples <= 0)
        return result;

    emitRestartNoteOffs (midiOut);
    if (!std::isfinite (ppqPosition))
        ppqPosition = 0.0;

    blockStartSample = nextBlockStartSample;
    nextBlockStartSample += numSamples;
    const bool validTempo = std::isfinite (bpm) && bpm > 0.0;
    beatsPerSample = ((validTempo ? bpm : 120.0) / 60.0) / sampleRate;
    const double beatsPerStep = macros.beatsPerStep > 0.0 ? macros.beatsPerStep : 0.25;
    const double edgeTolerance = 2.0 * beatsPerSample;
    const double jitterTolerance = std::min (0.25 * beatsPerStep, 64.0 * beatsPerSample);
    const bool ppqContinues = std::abs (ppqPosition - expectedNextPpq) <= jitterTolerance;
    const bool hostLoopValid = hostLoop.active && hostLoop.endPpq > hostLoop.startPpq;
    const bool loopRestart = hostLoopValid && expectedNextPpq >= hostLoop.endPpq - jitterTolerance
                             && ppqPosition < expectedNextPpq - jitterTolerance;
    const bool loopWrapped = wasPlaying && loopRestart && ppqPosition >= hostLoop.startPpq - edgeTolerance
                             && ppqPosition < hostLoop.endPpq;
    // Keep deferred attacks on a continuous beat timeline through host loop wraps.
    // Each callback maps their beats back to samples using that callback's tempo.
    if (!wasPlaying)
        loopBeatOffset = 0.0;
    if (loopWrapped)
    {
        loopBeatOffset += hostLoop.endPpq - hostLoop.startPpq;
        controllerRamps.fill ({});
    }
    blockStartBeat = ppqPosition + loopBeatOffset;
    recordingSuppressionTolerance = 2 + static_cast<int> (std::ceil (timingHumanizeInSteps (macros) * beatsPerStep / beatsPerSample));
    const bool hasEarlyHits = snapshot.hasEarlyCells || macros.timingHumanize > 0;

    if (! adoptedGenerationVersion.has_value() || *adoptedGenerationVersion != snapshot.generationVersion)
        adoptGeneratedSources (snapshot);

    const bool hostPatternChanged = previousHostPattern != macros.patternSource;
    const bool overrideSuperseded = adoptedPatternSelectionRevision != snapshot.patternSelectionRevision
        && pendingStaticSourceOverride != noStaticSourceOverride && pendingStaticSourceOverride != snapshot.staticSource;
    adoptedPatternSelectionRevision = snapshot.patternSelectionRevision;
    if (hostPatternChanged || overrideSuperseded)
    {
        previousHostPattern = macros.patternSource;
        pendingStaticSourceOverride = noStaticSourceOverride;
        pendingNoteOnCount = 0;
        controllerPointCount = 0;
        controllerRamps.fill ({});
        anticipatedLoopStep.reset();
        if (macros.patternSource == Constants::mutedStaticSource)
            releaseHeldNotes (midiOut);
    }

    if (pendingStaticSourceOverride == snapshot.staticSource)
        pendingStaticSourceOverride = noStaticSourceOverride;

    const int sourceAtBlockStart = macros.patternSource.value_or (pendingStaticSourceOverride == noStaticSourceOverride
        ? snapshot.staticSource : pendingStaticSourceOverride);
    if (snapshot.generationMode == GenerationMode::staticSource && patternSelections.count > 0)
    {
        const auto& selection = patternSelections.events[patternSelections.count - 1];
        pendingStaticSourceOverride = clampInt (selection.source,
                                                Constants::mutedStaticSource,
                                                Constants::sourceCount - 1);
        const auto selectionsEnd = patternSelections.events.begin() + static_cast<ptrdiff_t> (patternSelections.count);
        const auto changed = std::find_if (patternSelections.events.begin(), selectionsEnd,
                                           [sourceAtBlockStart] (const auto& event) { return event.source != sourceAtBlockStart; });
        if (changed != selectionsEnd)
        {
            dropPendingNoteOnsAtOrAfter (changed->samplePosition);
            const auto end = controllerPoints->begin() + static_cast<ptrdiff_t> (controllerPointCount);
            const auto keep = std::remove_if (controllerPoints->begin(), end, [&] (const ControllerPoint& point)
            {
                return sampleOffsetForBeat (point.beat) >= changed->samplePosition;
            });
            controllerPointCount = static_cast<size_t> (keep - controllerPoints->begin());
            if (hasEarlyHits && validTempo)
            {
                const double selectionBeat = ppqPosition + changed->samplePosition * beatsPerSample;
                const int stepBeforeSelection = firstGlobalStepAtOrAfter (selectionBeat - edgeTolerance, beatsPerStep) - 1;
                if (lastScheduledGlobalStep && *lastScheduledGlobalStep > stepBeforeSelection)
                    lastScheduledGlobalStep = stepBeforeSelection;
                anticipatedLoopStep.reset();
            }
        }
    }

    if (! isPlaying || ! macros.deviceActive || !validTempo)
    {
        if (wasPlaying)
        {
            releaseHeldNotes (midiOut);
            resetCycleCounters();
        }
        collectAuditionNotes (numSamples, callbackTimeMs);
        renderAttacksAndGates (numSamples, midiOut);
        wasPlaying = false;
        lastEmittedGlobalStep = std::nullopt;
        lastScheduledGlobalStep.reset();
        anticipatedLoopStep.reset();

        return result;
    }

    const double blockLastSamplePpq = ppqPosition + static_cast<double> (numSamples - 1) * beatsPerSample;
    const bool rateChanged = wasPlaying && std::abs (beatsPerStep - previousBeatsPerStep) > 1.0e-9;
    previousBeatsPerStep = beatsPerStep;

    const double playbackStartPpq = ppqPosition + firstPlaybackSample * beatsPerSample;
    const int globalStepStart = globalStepForBeats (playbackStartPpq, beatsPerStep);
    const bool startingOffGrid = !wasPlaying
        && sampleOffsetForBeat (globalStepStart * beatsPerStep + loopBeatOffset) < firstPlaybackSample;
    int globalStepEnd = globalStepForBeats (blockLastSamplePpq, beatsPerStep);

    // Steps at or after the host loop end belong to the next pass; the host wraps first.
    if (hostLoopValid && ppqPosition < hostLoop.endPpq)
        globalStepEnd = std::min (globalStepEnd, firstGlobalStepAtOrAfter (hostLoop.endPpq - edgeTolerance, beatsPerStep) - 1);

    result.playing = true;
    result.latestGlobalStep = globalStepStart;
    const int playbackLength = macros.patternSource && sourceAtBlockStart >= 0
        ? snapshot.sourceSettings[static_cast<size_t> (sourceAtBlockStart)].stepCount
        : useStaticSource ? snapshot.stepCount : snapshot.generatedStepCount;
    result.currentStepOneBased = stepOneBased (globalStepStart, playbackLength);

    if (! wasPlaying)
    {
        lastEmittedGlobalStep = globalStepStart - 1;
        lastScheduledGlobalStep = globalStepStart - 1;
        anticipatedLoopStep.reset();
        wasPlaying = true;
    }
    else if (lastEmittedGlobalStep.has_value())
    {
        const int previous = *lastEmittedGlobalStep;
        const bool stepsContinue = globalStepStart == previous || globalStepStart == previous + 1;
        if (hostPatternChanged || overrideSuperseded || rateChanged || ((! stepsContinue || loopRestart) && ! ppqContinues))
        {
            double landingPpq = ppqPosition;

            if (loopWrapped)
            {
                // Hosts that do not split the block at the wrap still owe the steps from loop start.
                landingPpq = std::min (ppqPosition, hostLoop.startPpq);
            }
            if (rateChanged || (!loopWrapped && (!(hostPatternChanged || overrideSuperseded) || !ppqContinues)))
                releaseHeldNotes (midiOut);
            if (! ppqContinues && ! loopWrapped)
                resetCycleCounters();

            // A grid change invalidates step indices even when the host PPQ is continuous.
            if (rateChanged && firstPlaybackSample > 0)
                landingPpq = ppqPosition + firstPlaybackSample * beatsPerSample;
            lastEmittedGlobalStep = firstGlobalStepAtOrAfter (landingPpq - edgeTolerance, beatsPerStep) - 1;
            lastScheduledGlobalStep = lastEmittedGlobalStep;
            if (rateChanged || ! loopWrapped)
                anticipatedLoopStep.reset();
        }
    }

    // Stop/device-off and seek cancellation must run before a deferred hit can
    // emit. Otherwise its new note-off can be cleared after the note-on is sent.
    const double stepDurationMs = beatsPerStep * 1000.0 / (sampleRate * beatsPerSample);
    collectPendingNoteOns (snapshot, macros, sourceAtBlockStart, numSamples, stepDurationMs, result);
    collectAuditionNotes (numSamples, callbackTimeMs);

    expectedNextPpq = ppqPosition + static_cast<double> (numSamples) * beatsPerSample;
    const int firstStep = lastScheduledGlobalStep.has_value() ? *lastScheduledGlobalStep + 1 : globalStepStart;
    const auto sampleForBeat = [&] (double beat)
    {
        return std::max (0, sampleOffsetForBeat (beat + loopBeatOffset));
    };
    const auto selectionAt = [&] (int stepSample)
    {
        if (macros.patternSource)
            return MidiPatternSelectionEvent { 0, *macros.patternSource };
        if (snapshot.generationMode != GenerationMode::staticSource)
            return MidiPatternSelectionEvent { 0, noStaticSourceOverride };
        for (size_t i = patternSelections.count; i > 0; --i)
            if (patternSelections.events[i - 1].samplePosition <= stepSample + kPatternSelectionSampleTolerance)
            {
                auto selection = patternSelections.events[i - 1];
                selection.source = clampInt (selection.source, Constants::mutedStaticSource, Constants::sourceCount - 1);
                return selection;
            }
        return MidiPatternSelectionEvent { 0, sourceAtBlockStart };
    };
    int spanEndSample = std::numeric_limits<int>::max();
    if (hostLoopValid && ppqPosition < hostLoop.endPpq)
        spanEndSample = sampleForBeat (hostLoop.endPpq);
    if (endGlobalStepExclusive)
        spanEndSample = std::min (spanEndSample, sampleForBeat (*endGlobalStepExclusive * beatsPerStep));
    // A full-step negative nudge plus negative humanize can cross two steps.
    const int lookaheadSteps = (snapshot.hasEarlyCells ? 1 : 0)
                               + (macros.timingHumanize > 0 ? 1 : 0);
    int scheduledStepEnd = globalStepEnd + lookaheadSteps;
    if (endGlobalStepExclusive.has_value())
        scheduledStepEnd = std::min (scheduledStepEnd, *endGlobalStepExclusive - 1);
    if (hostLoopValid && ppqPosition < hostLoop.endPpq)
        scheduledStepEnd = std::min (scheduledStepEnd, firstGlobalStepAtOrAfter (hostLoop.endPpq - edgeTolerance, beatsPerStep) - 1);

    for (int globalStep = firstStep; globalStep <= scheduledStepEnd; ++globalStep)
    {
        if (anticipatedLoopStep.has_value() && globalStep <= *anticipatedLoopStep)
        {
            if (globalStep == *anticipatedLoopStep)
                anticipatedLoopStep.reset();
            lastScheduledGlobalStep = globalStep;
            continue;
        }
        const double stepPpq = globalStep * beatsPerStep;
        const int stepSample = sampleForBeat (stepPpq);

        if (!useStaticSource && regenerateWindowIfDue (snapshot, globalStep))
            result.generatedSourcesChanged = true;

        const auto selection = selectionAt (stepSample);
        int earliestHitSample = firstPlaybackSample;
        if (hostLoopValid && ppqPosition < hostLoop.startPpq
            && globalStep >= firstGlobalStepAtOrAfter (hostLoop.startPpq - edgeTolerance, beatsPerStep))
        {
            // The first pass needs the same protection when playback approaches
            // the loop start from an earlier song position or count-in.
            earliestHitSample = std::max (earliestHitSample, sampleForBeat (hostLoop.startPpq));
        }
        int stepSpanEnd = spanEndSample;
        if (snapshot.hasSpannedCells && snapshot.generationMode == GenerationMode::staticSource)
            for (size_t i = 0; i < patternSelections.count; ++i)
                if (patternSelections.events[i].samplePosition > stepSample + kPatternSelectionSampleTolerance)
                {
                    stepSpanEnd = std::min (stepSpanEnd, patternSelections.events[i].samplePosition);
                    break;
                }
        // Mid-step entry can still play the remaining splits and delayed hits using their original anchor.
        const double stepBeat = startingOffGrid ? stepPpq : std::max (stepPpq, ppqPosition);
        evaluateGlobalStep (snapshot, macros, globalStep, stepBeat + loopBeatOffset,
                            numSamples, result, selection,
                            beatsPerStep, earliestHitSample, stepSpanEnd, startingOffGrid);
        lastScheduledGlobalStep = globalStep;
    }

    // The logical playhead follows the host, even when notes have been scheduled ahead.
    lastEmittedGlobalStep = std::max (lastEmittedGlobalStep.value_or (globalStepStart - 1), globalStepEnd);
    result.latestGlobalStep = std::max (globalStepStart, globalStepEnd);

    if (hasEarlyHits && hostLoopValid && ppqPosition < hostLoop.endPpq
        && blockLastSamplePpq + lookaheadSteps * beatsPerStep >= hostLoop.endPpq)
    {
        const int firstLoopStep = anticipatedLoopStep.has_value() ? *anticipatedLoopStep + 1
                                                                 : firstGlobalStepAtOrAfter (hostLoop.startPpq - edgeTolerance, beatsPerStep);
        const double lookaheadPpq = hostLoop.startPpq + blockLastSamplePpq + lookaheadSteps * beatsPerStep - hostLoop.endPpq;
        const int lastLoopStep = std::min (globalStepForBeats (lookaheadPpq, beatsPerStep),
                                           firstGlobalStepAtOrAfter (hostLoop.endPpq - edgeTolerance, beatsPerStep) - 1);
        for (int loopStep = firstLoopStep; loopStep <= lastLoopStep; ++loopStep)
        {
            const double loopBeat = hostLoop.endPpq + static_cast<double> (loopStep) * beatsPerStep - hostLoop.startPpq;
            const int stepSample = sampleForBeat (loopBeat);
            if (!useStaticSource && regenerateWindowIfDue (snapshot, loopStep))
                result.generatedSourcesChanged = true;
            const auto selection = selectionAt (stepSample);
            // A host may reset downstream instruments at its loop boundary. An
            // early attack from the next pass would then be cut off, while our
            // anticipated-step marker prevents it from playing again. Keep all
            // attacks belonging to that pass on or after the actual wrap.
            const int loopStartSample = sampleForBeat (hostLoop.endPpq);
            const int nextLoopEnd = sampleForBeat (2.0 * hostLoop.endPpq - hostLoop.startPpq);
            evaluateGlobalStep (snapshot, macros, loopStep, loopBeat + loopBeatOffset, numSamples, result,
                                selection, beatsPerStep, loopStartSample, nextLoopEnd);
            anticipatedLoopStep = loopStep;
        }
    }

    renderControllers (snapshot, macros, patternSelections, sourceAtBlockStart, numSamples, spanEndSample, midiOut);
    renderAttacksAndGates (numSamples, midiOut);
    return result;
}

} // namespace ksh
