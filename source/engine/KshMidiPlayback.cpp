#include "KshMidiPlayback.h"
#include "KshMath.h"

#include <cmath>
#include <limits>

namespace ksh
{
namespace
{
constexpr double kTimingHumanizeNativeScale = 0.2;
constexpr double kRollNoteDurationScale = 0.9;
constexpr int kPatternSelectionSampleTolerance = 2;

int mod (int value, int divisor)
{
    if (divisor <= 0)
        return 0;

    const auto result = value % divisor;
    return result < 0 ? result + divisor : result;
}

int globalStepForBeats (double songBeats, const PlaybackSnapshot& snapshot)
{
    if (std::isnan (songBeats))
        songBeats = 0.0;

    const double beatsPerStep = snapshot.beatsPerStep > 0.0 ? snapshot.beatsPerStep : 0.25;
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

double timingHumanizeRangeMs (const PlaybackSnapshot& snapshot)
{
    return snapshot.stepIntervalMs * kTimingHumanizeNativeScale
           * (static_cast<double> (snapshot.timingHumanize) / 100.0);
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
    reset();
}

void MidiPlaybackRunner::reset (bool clearAuditions)
{
    blockStartSample = 0;
    nextBlockStartSample = 0;
    wasPlaying = false;
    expectedNextPpq = 0.0;
    lastEmittedGlobalStep = std::nullopt;
    lastScheduledGlobalStep.reset();
    anticipatedLoopStep.reset();
    adoptedGenerationVersion = std::nullopt;
    pendingNoteOffCount = 0;
    pendingNoteOnCount = 0;
    pendingStaticSourceOverride = noStaticSourceOverride;
    clearRecordingMonitors();
    resetCycleCounters();
    rngState = 0x12345678u;

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
    pendingNoteOffCount = 0;
    pendingNoteOnCount = 0;
}

void MidiPlaybackRunner::releaseHeldNotes (juce::MidiBuffer& midiOut)
{
    for (size_t i = 0; i < pendingNoteOffCount; ++i)
        midiOut.addEvent (juce::MidiMessage::noteOff (pendingNoteOffs[i].midiChannel, pendingNoteOffs[i].pitch), 0);

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

void MidiPlaybackRunner::dropPendingNoteOnsAtOrAfter (int sampleOffset)
{
    if (pendingNoteOnCount == 0)
        return;

    sampleOffset = std::max (0, sampleOffset);
    size_t writeIndex = 0;

    for (size_t i = 0; i < pendingNoteOnCount; ++i)
    {
        const auto pending = pendingNoteOns[i];

        if (pending.samplePosition < blockStartSample + sampleOffset)
            pendingNoteOns[writeIndex++] = pending;
    }

    pendingNoteOnCount = writeIndex;
}

double MidiPlaybackRunner::nextRandomUnit()
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return static_cast<double> (rngState) / static_cast<double> (UINT32_MAX);
}

double MidiPlaybackRunner::nextTimingOffsetMs (double rangeMs)
{
    // Inverse CDF of a symmetric triangle, using one draw per timing offset.
    const double unit = nextRandomUnit();
    const double offset = unit < 0.5 ? std::sqrt (2.0 * unit) - 1.0
                                     : 1.0 - std::sqrt (2.0 * (1.0 - unit));
    return offset * rangeMs;
}

int MidiPlaybackRunner::staticSourceForStep (const PlaybackSnapshot& snapshot,
                                             const MidiPatternSelectionBlock& patternSelections,
                                             int stepSampleOffset)
{
    if (snapshot.generationMode != GenerationMode::staticSource)
        return noStaticSourceOverride;

    if (pendingStaticSourceOverride != noStaticSourceOverride
        && snapshot.staticSource == pendingStaticSourceOverride)
        pendingStaticSourceOverride = noStaticSourceOverride;

    int source = pendingStaticSourceOverride != noStaticSourceOverride
                     ? pendingStaticSourceOverride
                     : snapshot.staticSource;

    for (size_t i = 0; i < patternSelections.count; ++i)
    {
        const auto& selection = patternSelections.events[i];

        if (selection.samplePosition > stepSampleOffset + kPatternSelectionSampleTolerance)
            break;

        pendingStaticSourceOverride = clampInt (selection.source,
                                                Constants::mutedStaticSource,
                                                Constants::sourceCount - 1);
        source = pendingStaticSourceOverride;
    }

    return clampInt (source, Constants::mutedStaticSource, Constants::sourceCount - 1);
}

int MidiPlaybackRunner::sampleOffsetForGlobalStep (const PlaybackSnapshot& snapshot,
                                                   double blockStartPpq,
                                                   double bpm,
                                                   int globalStep,
                                                   int numSamples) const
{
    if (bpm <= 0.0 || numSamples <= 0)
        return 0;

    const double stepStartBeat = static_cast<double> (globalStep) * snapshot.beatsPerStep;
    const double samplesPerBeat = sampleRate * 60.0 / bpm;
    const int sampleOffset = static_cast<int> (std::llround ((stepStartBeat - blockStartPpq) * samplesPerBeat));

    if (numSamples <= 0)
        return 0;

    // Look-ahead steps may start in a later callback. Their signed nudge can
    // bring the note into this block; later hits retain an absolute deadline.
    return std::max (0, sampleOffset);
}

void MidiPlaybackRunner::emitHitAtSample (int onSample,
                                          int numSamples,
                                          const NativeHit& hit,
                                          juce::MidiBuffer& midiOut,
                                          MidiPlaybackResult& result)
{
    if (onSample < 0 || onSample >= numSamples)
        return;

    const int durationSamples =
        std::max (1, static_cast<int> (std::llround (static_cast<double> (hit.durationMs) * sampleRate / 1000.0)));
    const int offSample = onSample + durationSamples;

    midiOut.addEvent (juce::MidiMessage::noteOn (hit.midiChannel, hit.pitch,
                                                 static_cast<juce::uint8> (std::clamp (hit.velocity, 1, 127))),
                      onSample);
    result.addNoteHit (hit);

    if (offSample < numSamples)
    {
        midiOut.addEvent (juce::MidiMessage::noteOff (hit.midiChannel, hit.pitch), offSample);
    }
    else
    {
        addPendingNoteOff (offSample, hit.midiChannel, hit.pitch);
    }
}

void MidiPlaybackRunner::scheduleHit (int stepSampleOffset,
                                      int numSamples,
                                      const NativeHit& hit,
                                      juce::MidiBuffer& midiOut,
                                      MidiPlaybackResult& result,
                                      int earliestHitSample,
                                      int span)
{
    const int delaySamples = static_cast<int> (std::llround (hit.delayMs * sampleRate / 1000.0));
    if (wasRecordedHit (hit, blockStartSample + stepSampleOffset + delaySamples))
        return;
    const int onSample = std::max ({ 0, earliestHitSample, stepSampleOffset + delaySamples });

    if (onSample >= numSamples)
    {
        if (pendingNoteOnCount < pendingNoteOns.size())
            pendingNoteOns[pendingNoteOnCount++] = { blockStartSample + onSample, hit, span };
        return;
    }

    emitHitAtSample (onSample, numSamples, hit, midiOut, result);
}

void MidiPlaybackRunner::evaluateGlobalStep (const PlaybackSnapshot& snapshot,
                                             int globalStep,
                                             int stepSampleOffset,
                                             int numSamples,
                                             juce::MidiBuffer& midiOut,
                                             MidiPlaybackResult& result,
                                             int staticSourceOverride,
                                             double stepDurationMs,
                                             int earliestHitSample,
                                             int spanEndSample)
{
    if (! snapshot.deviceActive || snapshot.stepCount <= 0 || snapshot.channelCount <= 0)
        return;

    if (staticSourceOverride == Constants::mutedStaticSource)
        return;

    for (int channel = 0; channel < snapshot.channelCount; ++channel)
    {
        const auto position = playbackStepForChannel (snapshot, channel, globalStep, staticSourceOverride);
        const auto cellAtStep = [&] (int step)
        {
            auto cell = staticSourceOverride >= 0
                            ? staticSourceCellForStep (snapshot, staticSourceOverride, channel, step)
                            : generatedCellForStep (snapshot, channel, step);
            if (step + cell.span > position.loop.loopStart + position.loop.loopLength)
                cell.span = 1;
            return cell;
        };
        int playbackStep = position.step;
        auto cell = cellAtStep (playbackStep);
        if (snapshot.hasSpannedCells && position.reversed)
        {
            // Enter the group from its right edge on a reverse pass. Its three
            // (or more) attacks still run evenly forward in transport time.
            if (cell.enabled && cell.span > 1)
                continue;
            if (!cell.enabled)
                for (int anchor = playbackStep - 1; anchor >= position.loop.loopStart; --anchor)
                {
                    const auto candidate = cellAtStep (anchor);
                    if (candidate.enabled && anchor + candidate.span - 1 == playbackStep)
                    {
                        cell = candidate;
                        playbackStep = anchor;
                        break;
                    }
                }
        }

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
        const int roll = clampInt (cell.roll, 1, Constants::maxRoll);
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

        if (probability < 100 && ! (nextRandomUnit() * 100.0 < static_cast<double> (probability)))
            continue;

        const double velocityVariation = snapshot.velocityHumanize > 0 ? nextRandomUnit() * 2.0 - 1.0 : 0.0;

        const bool spanned = cell.span > 1;
        const double intervalMs = spanned ? stepDurationMs * cell.span : snapshot.stepIntervalMs;
        const int noteDurationMs = rollNoteDurationMs (intervalMs, roll);
        const double timingRange = timingHumanizeRangeMs (snapshot);
        const double groupTimingMs = spanned && timingRange > 0.0 ? nextTimingOffsetMs (timingRange) : 0.0;
        const int pitch = snapshot.channels[static_cast<size_t> (channel)].note;

        for (int rollIndex = 0; rollIndex < roll; ++rollIndex)
        {
            if (roll > 1)
            {
                const auto index = static_cast<size_t> (rollIndex);
                const int chance = cell.splitGates[index].probability();
                if (!splitCycleAllows[index] || chance == 0 || (chance < 100 && nextRandomUnit() * 100.0 >= chance))
                    continue;
            }
            const double part = static_cast<double> (rollIndex) / roll;
            const double rollDelayMs = part * intervalMs;
            const double swingDelayMs = spanned ? 0.0
                : swingDelayStepsForPosition (globalStep + part, snapshot.swing, snapshot.swingSubdivisionIndex) * intervalMs;
            const double timingDelayMs = !spanned && timingRange > 0.0 ? nextTimingOffsetMs (timingRange) : groupTimingMs;
            const double delayMs = rollDelayMs + swingDelayMs + timingDelayMs
                                   + static_cast<double> (cell.nudge) * stepDurationMs / 100.0;

            if (spanned && stepSampleOffset + std::llround (delayMs * sampleRate / 1000.0) >= spanEndSample)
                break;

            const int baseVelocity = roll > 1 ? cell.splitVelocity (rollIndex) : cell.velocity;
            const double range = static_cast<double> (baseVelocity) * (static_cast<double> (snapshot.velocityHumanize) / 100.0);
            const int velocity = clampInt (static_cast<int> (std::lround (baseVelocity + velocityVariation * range)), 1, 127);
            scheduleHit (stepSampleOffset,
                         numSamples,
                         NativeHit {
                             pitch,
                             velocity,
                             noteDurationMs,
                             Constants::defaultMidiChannel,
                             delayMs,
                             channel + 1,
                             playbackStep + 1,
                             cell.source + 1,
                             cell.sourceStep + 1,
                             cycleStep
                         },
                         midiOut,
                         result,
                         earliestHitSample,
                         cell.span);
        }
    }
}

void MidiPlaybackRunner::addPendingNoteOff (int sampleOffset, int midiChannel, int pitch)
{
    if (pendingNoteOffCount >= pendingNoteOffs.size())
        return;

    pendingNoteOffs[pendingNoteOffCount++] = { blockStartSample + sampleOffset, midiChannel, pitch };
}

void MidiPlaybackRunner::flushPendingNoteOns (const PlaybackSnapshot& snapshot, int activeSource, int numSamples, juce::MidiBuffer& midiOut, MidiPlaybackResult& result)
{
    if (pendingNoteOnCount == 0)
        return;

    size_t writeIndex = 0;

    for (size_t i = 0; i < pendingNoteOnCount; ++i)
    {
        const auto pending = pendingNoteOns[i];
        const auto offset = pending.samplePosition - blockStartSample;
        if (wasRecordedHit (pending.hit, pending.samplePosition))
            continue;
        if (pending.span > 1)
        {
            const int source = pending.hit.uiSource - 1;
            if (snapshot.generationMode == GenerationMode::staticSource && source != activeSource)
                continue;
            const auto anchor = staticSourceCellForStep (snapshot, source, pending.hit.uiChannel - 1, pending.hit.uiSourceStep - 1);
            if (!anchor.enabled || anchor.sourceStep != pending.hit.uiSourceStep - 1 || anchor.span != pending.span)
                continue;
        }

        if (offset < numSamples)
        {
            emitHitAtSample (static_cast<int> (std::max<int64_t> (0, offset)), numSamples, pending.hit, midiOut, result);
        }
        else
        {
            pendingNoteOns[writeIndex++] = pending;
        }
    }

    pendingNoteOnCount = writeIndex;
}

void MidiPlaybackRunner::flushAuditionNotes (int numSamples, juce::MidiBuffer& midi, double callbackTimeMs)
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
        const int durationSamples =
            std::max (1, static_cast<int> (std::llround (static_cast<double> (note.durationMs) * sampleRate / 1000.0)));
        const int offSample = onSample + durationSamples;
        const auto velocity = static_cast<juce::uint8> (std::clamp (note.velocity, 1, 127));

        midi.addEvent (juce::MidiMessage::noteOn (note.channel, note.pitch, velocity), onSample);

        if (offSample < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (note.channel, note.pitch), offSample);
        }
        else
        {
            addPendingNoteOff (offSample, note.channel, note.pitch);
        }
    }
}

void MidiPlaybackRunner::flushPendingNoteOffs (int numSamples, juce::MidiBuffer& midi)
{
    if (pendingNoteOffCount == 0)
        return;

    size_t writeIndex = 0;

    for (size_t i = 0; i < pendingNoteOffCount; ++i)
    {
        const auto pending = pendingNoteOffs[i];
        const auto offset = pending.samplePosition - blockStartSample;

        if (offset < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (pending.midiChannel, pending.pitch),
                           static_cast<int> (std::max<int64_t> (0, offset)));
        }
        else
        {
            pendingNoteOffs[writeIndex++] = pending;
        }
    }

    pendingNoteOffCount = writeIndex;
}

MidiPlaybackResult MidiPlaybackRunner::processBlock (const PlaybackSnapshot& snapshot,
                                                     double ppqPosition,
                                                     double bpm,
                                                     bool isPlaying,
                                                     int numSamples,
                                                     juce::MidiBuffer& midiOut,
                                                     const MidiPatternSelectionBlock& patternSelections,
                                                     const HostLoopRange& hostLoop,
                                                     double callbackTimeMs,
                                                     std::optional<int> endGlobalStepExclusive,
                                                     int firstPlaybackSample)
{
    MidiPlaybackResult result;

    if (numSamples <= 0)
        return result;

    if (std::isnan (ppqPosition))
        ppqPosition = 0.0;

    blockStartSample = nextBlockStartSample;
    nextBlockStartSample += numSamples;
    recordingSuppressionTolerance = 2 + static_cast<int> (std::ceil (timingHumanizeRangeMs (snapshot) * sampleRate / 1000.0));
    const bool hasEarlyHits = snapshot.hasEarlyCells || snapshot.timingHumanize > 0;

    if (! adoptedGenerationVersion.has_value() || *adoptedGenerationVersion != snapshot.generationVersion)
        adoptGeneratedSources (snapshot);

    const int sourceAtBlockStart = pendingStaticSourceOverride == noStaticSourceOverride
        ? snapshot.staticSource : pendingStaticSourceOverride;
    if (snapshot.generationMode == GenerationMode::staticSource && patternSelections.count > 0)
    {
        const auto& selection = patternSelections.events[patternSelections.count - 1];
        pendingStaticSourceOverride = clampInt (selection.source,
                                                Constants::mutedStaticSource,
                                                Constants::sourceCount - 1);
        dropPendingNoteOnsAtOrAfter (patternSelections.events[0].samplePosition);
        if (hasEarlyHits && bpm > 0.0)
        {
            const double selectionBeat = ppqPosition + static_cast<double> (patternSelections.events[0].samplePosition) * bpm / (60.0 * sampleRate);
            const double stepBeats = snapshot.beatsPerStep > 0.0 ? snapshot.beatsPerStep : 0.25;
            lastScheduledGlobalStep = firstGlobalStepAtOrAfter (selectionBeat, stepBeats) - 1;
            anticipatedLoopStep.reset();
        }
    }

    if (! isPlaying || ! snapshot.deviceActive || bpm <= 0.0)
    {
        if (wasPlaying)
        {
            releaseHeldNotes (midiOut);
            resetCycleCounters();
        }
        else
        {
            // Auditions may still have gates to finish while transport is stopped.
            flushPendingNoteOffs (numSamples, midiOut);
        }

        flushAuditionNotes (numSamples, midiOut, callbackTimeMs);
        wasPlaying = false;
        lastEmittedGlobalStep = std::nullopt;
        lastScheduledGlobalStep.reset();
        anticipatedLoopStep.reset();

        return result;
    }

    const double beatsPerSample = (bpm / 60.0) / sampleRate;
    const double beatsPerStep = snapshot.beatsPerStep > 0.0 ? snapshot.beatsPerStep : 0.25;
    const double edgeTolerance = 2.0 * beatsPerSample;
    const double blockLastSamplePpq = ppqPosition + static_cast<double> (numSamples - 1) * beatsPerSample;
    const bool hostLoopValid = hostLoop.active && hostLoop.endPpq > hostLoop.startPpq;

    const int globalStepStart = globalStepForBeats (ppqPosition + firstPlaybackSample * beatsPerSample, snapshot);
    int globalStepEnd = globalStepForBeats (blockLastSamplePpq, snapshot);

    // Steps at or after the host loop end belong to the next pass; the host wraps first.
    if (hostLoopValid && ppqPosition < hostLoop.endPpq)
        globalStepEnd = std::min (globalStepEnd, firstGlobalStepAtOrAfter (hostLoop.endPpq - edgeTolerance, beatsPerStep) - 1);

    result.playing = true;
    result.latestGlobalStep = globalStepStart;
    const int playbackLength = snapshot.generationMode == GenerationMode::staticSource ? snapshot.stepCount : snapshot.generatedStepCount;
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
        const double jitterTolerance = std::min (0.25 * beatsPerStep, 64.0 * beatsPerSample);
        const bool ppqContinues = std::abs (ppqPosition - expectedNextPpq) <= jitterTolerance;

        const bool loopRestart = hostLoopValid && expectedNextPpq >= hostLoop.endPpq - jitterTolerance
                                 && ppqPosition < expectedNextPpq - jitterTolerance;
        if ((! stepsContinue || loopRestart) && ! ppqContinues)
        {
            const bool loopWrapped = hostLoopValid
                                     && expectedNextPpq >= hostLoop.endPpq - jitterTolerance
                                     && ppqPosition >= hostLoop.startPpq - edgeTolerance
                                     && ppqPosition < hostLoop.endPpq;
            double landingPpq = ppqPosition;

            if (loopWrapped)
            {
                // Hosts that do not split the block at the wrap still owe the steps from loop start.
                landingPpq = std::min (ppqPosition, hostLoop.startPpq);
            }
            else
            {
                releaseHeldNotes (midiOut);
                resetCycleCounters();
            }

            // Steps that start at the landing position play; a mid-step landing waits for the next boundary.
            lastEmittedGlobalStep = firstGlobalStepAtOrAfter (landingPpq - edgeTolerance, beatsPerStep) - 1;
            lastScheduledGlobalStep = lastEmittedGlobalStep;
            if (!loopWrapped)
                anticipatedLoopStep.reset();
        }
    }

    // Stop/device-off and seek cancellation must run before a deferred hit can
    // emit. Otherwise its new note-off can be cleared after the note-on is sent.
    flushPendingNoteOns (snapshot, sourceAtBlockStart, numSamples, midiOut, result);
    flushPendingNoteOffs (numSamples, midiOut);
    flushAuditionNotes (numSamples, midiOut, callbackTimeMs);

    expectedNextPpq = ppqPosition + static_cast<double> (numSamples) * beatsPerSample;
    const int firstStep = lastScheduledGlobalStep.has_value() ? *lastScheduledGlobalStep + 1 : globalStepStart;
    const double stepDurationMs = beatsPerStep * 60000.0 / bpm;
    const auto sampleForBeat = [&] (double beat)
    {
        return static_cast<int> (std::clamp (std::round ((beat - ppqPosition) / beatsPerSample), 0.0,
                                            static_cast<double> (std::numeric_limits<int>::max())));
    };
    int spanEndSample = std::numeric_limits<int>::max();
    if (hostLoopValid && ppqPosition < hostLoop.endPpq)
        spanEndSample = sampleForBeat (hostLoop.endPpq);
    if (endGlobalStepExclusive)
        spanEndSample = std::min (spanEndSample, sampleForBeat (*endGlobalStepExclusive * beatsPerStep));
    // A full-step negative nudge plus negative humanize can cross two steps.
    const int lookaheadSteps = (snapshot.hasEarlyCells ? 1 : 0)
                               + static_cast<int> (std::ceil (timingHumanizeRangeMs (snapshot) / stepDurationMs));
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
        const int stepSample = sampleOffsetForGlobalStep (snapshot, ppqPosition, bpm, globalStep, numSamples);

        if (regenerateWindowIfDue (snapshot, globalStep))
            result.generatedSourcesChanged = true;

        const int staticSourceOverride = staticSourceForStep (snapshot, patternSelections, stepSample);
        int earliestHitSample = firstPlaybackSample;
        if (hostLoopValid && ppqPosition < hostLoop.startPpq
            && globalStep >= firstGlobalStepAtOrAfter (hostLoop.startPpq - edgeTolerance, beatsPerStep))
        {
            // The first pass needs the same protection when playback approaches
            // the loop start from an earlier song position or count-in.
            earliestHitSample = std::max (0, static_cast<int> (std::llround ((hostLoop.startPpq - ppqPosition) / beatsPerSample)));
        }
        int stepSpanEnd = spanEndSample;
        if (snapshot.hasSpannedCells && snapshot.generationMode == GenerationMode::staticSource)
            for (size_t i = 0; i < patternSelections.count; ++i)
                if (patternSelections.events[i].samplePosition > stepSample + kPatternSelectionSampleTolerance)
                {
                    stepSpanEnd = std::min (stepSpanEnd, patternSelections.events[i].samplePosition);
                    break;
                }
        evaluateGlobalStep (snapshot, globalStep, stepSample, numSamples, midiOut, result, staticSourceOverride, stepDurationMs, earliestHitSample, stepSpanEnd);
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
        const int lastLoopStep = std::min (globalStepForBeats (lookaheadPpq, snapshot),
                                           firstGlobalStepAtOrAfter (hostLoop.endPpq - edgeTolerance, beatsPerStep) - 1);
        for (int loopStep = firstLoopStep; loopStep <= lastLoopStep; ++loopStep)
        {
            const double loopBeat = hostLoop.endPpq + static_cast<double> (loopStep) * beatsPerStep - hostLoop.startPpq;
            const int stepSample = std::max (0, static_cast<int> (std::llround ((loopBeat - ppqPosition) / beatsPerSample)));
            if (regenerateWindowIfDue (snapshot, loopStep))
                result.generatedSourcesChanged = true;
            const int source = staticSourceForStep (snapshot, patternSelections, stepSample);
            // A host may reset downstream instruments at its loop boundary. An
            // early attack from the next pass would then be cut off, while our
            // anticipated-step marker prevents it from playing again. Keep all
            // attacks belonging to that pass on or after the actual wrap.
            const int loopStartSample = std::max (0, static_cast<int> (std::llround ((hostLoop.endPpq - ppqPosition) / beatsPerSample)));
            const int nextLoopEnd = sampleForBeat (2.0 * hostLoop.endPpq - hostLoop.startPpq);
            evaluateGlobalStep (snapshot, loopStep, stepSample, numSamples, midiOut, result, source, stepDurationMs, loopStartSample, nextLoopEnd);
            anticipatedLoopStep = loopStep;
        }
    }

    return result;
}

} // namespace ksh
