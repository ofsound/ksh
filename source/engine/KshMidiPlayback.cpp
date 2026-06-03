#include "KshMidiPlayback.h"

#include <cmath>

namespace ksh
{
namespace
{
constexpr double kTimingHumanizeNativeScale = 0.2;
constexpr double kRollNoteDurationScale = 0.9;

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

int stepOneBased (int globalStep, int stepCount)
{
    if (stepCount <= 0)
        return 0;

    return mod (globalStep, stepCount) + 1;
}

bool cycleGateMatches (int count, int cycle, int cycleOffset, bool cycleInverted)
{
    const bool matches = count % cycle == cycleOffset;
    return cycleInverted ? ! matches : matches;
}

size_t cycleCounterIndex (int source, int channel, int sourceStep)
{
    return static_cast<size_t> (source) * Constants::maxChannels * Constants::maxSteps
         + static_cast<size_t> (channel) * Constants::maxSteps
         + static_cast<size_t> (sourceStep);
}

int playbackStepForChannel (const PlaybackSnapshot& snapshot, int channel, int playbackIndex)
{
    channel = clampInt (channel, 0, Constants::maxChannels - 1);
    const int loopLength = clampInt (snapshot.channels[static_cast<size_t> (channel)].loopLength,
                                     1,
                                     snapshot.stepCount);
    const auto mode = snapshot.channels[static_cast<size_t> (channel)].playbackMode;
    playbackIndex = static_cast<int> (std::floor (static_cast<double> (playbackIndex)));

    if (mode == PlaybackMode::reverse)
    {
        const int activeIndex = mod (playbackIndex, loopLength);
        return loopLength - 1 - activeIndex;
    }

    if (mode == PlaybackMode::boomerang)
    {
        const int activeIndex = mod (playbackIndex, loopLength * 2);
        return activeIndex < loopLength ? activeIndex : loopLength * 2 - 1 - activeIndex;
    }

    return mod (playbackIndex, snapshot.stepCount);
}

double swingDelayMsForStep (const PlaybackSnapshot& snapshot, int step)
{
    return step % 2 == 1
               ? snapshot.stepIntervalMs * 0.5 * (static_cast<double> (snapshot.swing) / 100.0)
               : 0.0;
}

double timingHumanizeRangeMs (const PlaybackSnapshot& snapshot)
{
    return snapshot.stepIntervalMs * kTimingHumanizeNativeScale
           * (static_cast<double> (snapshot.timingHumanize) / 100.0);
}

int rollNoteDurationMs (const PlaybackSnapshot& snapshot, int roll)
{
    roll = clampInt (roll, 1, Constants::maxRoll);

    if (roll <= 1)
        return Constants::defaultNoteDurationMs;

    const double subdivisionMs = snapshot.stepIntervalMs / static_cast<double> (roll);
    return std::max (1,
                     std::min (Constants::defaultNoteDurationMs,
                               static_cast<int> (std::floor (subdivisionMs * kRollNoteDurationScale))));
}
} // namespace

void MidiPlaybackRunner::prepare (double sampleRateIn)
{
    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
    reset();
}

void MidiPlaybackRunner::reset (bool clearAuditions)
{
    wasPlaying = false;
    lastEmittedGlobalStep = std::nullopt;
    pendingNoteOffCount = 0;
    pendingNoteOnCount = 0;
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
}

void MidiPlaybackRunner::queueAuditionNote (const MidiNoteEvent& note)
{
    [[maybe_unused]] const bool queued = pendingAuditions.try_enqueue (note);
}

void MidiPlaybackRunner::clearPending()
{
    pendingNoteOffCount = 0;
    pendingNoteOnCount = 0;
}

double MidiPlaybackRunner::nextRandomUnit()
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return static_cast<double> (rngState) / static_cast<double> (UINT32_MAX);
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

    return std::clamp (sampleOffset, 0, numSamples - 1);
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
                                      MidiPlaybackResult& result)
{
    const int delaySamples = static_cast<int> (std::llround (hit.delayMs * sampleRate / 1000.0));
    const int onSample = std::max (0, stepSampleOffset + delaySamples);

    if (onSample >= numSamples)
    {
        addPendingNoteOn (onSample, hit);
        return;
    }

    emitHitAtSample (onSample, numSamples, hit, midiOut, result);
}

void MidiPlaybackRunner::evaluateGlobalStep (const PlaybackSnapshot& snapshot,
                                             int globalStep,
                                             int stepSampleOffset,
                                             int numSamples,
                                             juce::MidiBuffer& midiOut,
                                             MidiPlaybackResult& result)
{
    if (! snapshot.deviceActive || snapshot.stepCount <= 0 || snapshot.channelCount <= 0)
        return;

    const int rowStep = mod (globalStep, snapshot.stepCount);

    for (int channel = 0; channel < snapshot.channelCount; ++channel)
    {
        const int playbackStep = playbackStepForChannel (snapshot, channel, globalStep);
        const auto& cell = snapshot.generated[static_cast<size_t> (channel)][static_cast<size_t> (playbackStep)];

        if (! cell.enabled)
            continue;

        const int cycle = clampInt (cell.cycle, 1, 64);
        const int cycleOffset = clampInt (cell.cycleOffset, 0, cycle - 1);
        const bool cycleInverted = cell.cycleInverted;

        if (cycle > 1 || cycleInverted)
        {
            const auto index = cycleCounterIndex (cell.source, channel, cell.sourceStep);

            if (index >= cycleCounters.size())
                continue;

            const int count = static_cast<int> (cycleCounters[index]++);
            if (! cycleGateMatches (count, cycle, cycleOffset, cycleInverted))
                continue;
        }

        const int probability = clampInt (cell.probability, 0, 100);

        if (probability <= 0)
            continue;

        if (probability < 100 && ! (nextRandomUnit() * 100.0 < static_cast<double> (probability)))
            continue;

        int velocity = clampInt (cell.velocity, 1, 127);

        if (snapshot.velocityHumanize > 0)
        {
            const double range = static_cast<double> (velocity)
                                 * (static_cast<double> (snapshot.velocityHumanize) / 100.0);
            velocity = clampInt (static_cast<int> (std::lround (static_cast<double> (velocity)
                                                                 + (nextRandomUnit() * 2.0 - 1.0) * range)),
                                 1,
                                 127);
        }

        const int roll = clampInt (cell.roll, 1, Constants::maxRoll);
        const int noteDurationMs = rollNoteDurationMs (snapshot, roll);
        const double baseDelayMs = swingDelayMsForStep (snapshot, rowStep);
        const double timingRange = timingHumanizeRangeMs (snapshot);
        const double timingOffsetMs = timingRange > 0.0 ? (nextRandomUnit() * 2.0 - 1.0) * timingRange : 0.0;
        const int pitch = snapshot.channels[static_cast<size_t> (channel)].note;

        for (int rollIndex = 0; rollIndex < roll; ++rollIndex)
        {
            double delayMs = 0.0;

            if (rollIndex == 0)
                delayMs = std::max (0.0, baseDelayMs + timingOffsetMs);
            else
                delayMs = (static_cast<double> (rollIndex) / static_cast<double> (roll)) * snapshot.stepIntervalMs;

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
                             cell.sourceStep + 1
                         },
                         midiOut,
                         result);
        }
    }
}

void MidiPlaybackRunner::addPendingNoteOff (int sampleOffset, int midiChannel, int pitch)
{
    if (pendingNoteOffCount >= pendingNoteOffs.size())
        return;

    pendingNoteOffs[pendingNoteOffCount++] = { sampleOffset, midiChannel, pitch };
}

void MidiPlaybackRunner::addPendingNoteOn (int sampleOffset, const NativeHit& hit)
{
    if (pendingNoteOnCount >= pendingNoteOns.size())
        return;

    pendingNoteOns[pendingNoteOnCount++] = { sampleOffset, hit };
}

void MidiPlaybackRunner::flushPendingNoteOns (int numSamples, juce::MidiBuffer& midiOut, MidiPlaybackResult& result)
{
    if (pendingNoteOnCount == 0)
        return;

    size_t writeIndex = 0;

    for (size_t i = 0; i < pendingNoteOnCount; ++i)
    {
        const auto pending = pendingNoteOns[i];

        if (pending.sampleOffset < numSamples)
        {
            emitHitAtSample (pending.sampleOffset, numSamples, pending.hit, midiOut, result);
        }
        else
        {
            pendingNoteOns[writeIndex++] = { pending.sampleOffset - numSamples, pending.hit };
        }
    }

    pendingNoteOnCount = writeIndex;
}

void MidiPlaybackRunner::flushAuditionNotes (int numSamples, juce::MidiBuffer& midi)
{
    MidiNoteEvent note;

    while (pendingAuditions.try_dequeue (note))
    {
        const int delaySamples = static_cast<int> (std::llround (note.delayMs * sampleRate / 1000.0));
        const int onSample = std::clamp (delaySamples, 0, std::max (0, numSamples - 1));
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

        if (pending.sampleOffset < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (pending.midiChannel, pending.pitch),
                           std::clamp (pending.sampleOffset, 0, std::max (0, numSamples - 1)));
        }
        else
        {
            pendingNoteOffs[writeIndex++] = { pending.sampleOffset - numSamples, pending.midiChannel, pending.pitch };
        }
    }

    pendingNoteOffCount = writeIndex;
}

MidiPlaybackResult MidiPlaybackRunner::processBlock (const PlaybackSnapshot& snapshot,
                                                     double ppqPosition,
                                                     double bpm,
                                                     bool isPlaying,
                                                     int numSamples,
                                                     juce::MidiBuffer& midiOut)
{
    MidiPlaybackResult result;

    if (numSamples <= 0)
        return result;

    flushPendingNoteOns (numSamples, midiOut, result);
    flushPendingNoteOffs (numSamples, midiOut);
    flushAuditionNotes (numSamples, midiOut);

    if (! isPlaying || ! snapshot.deviceActive || bpm <= 0.0)
    {
        if (wasPlaying)
        {
            clearPending();
            resetCycleCounters();
        }

        wasPlaying = false;
        lastEmittedGlobalStep = std::nullopt;
        return result;
    }

    const double blockEndPpq = ppqPosition + (static_cast<double> (numSamples) / sampleRate) * (bpm / 60.0);

    const int globalStepStart = globalStepForBeats (ppqPosition, snapshot);
    const int globalStepEnd = globalStepForBeats (blockEndPpq, snapshot);

    result.playing = true;
    result.latestGlobalStep = globalStepStart;
    result.currentStepOneBased = stepOneBased (globalStepStart, snapshot.stepCount);

    if (! wasPlaying)
    {
        lastEmittedGlobalStep = globalStepStart - 1;
        wasPlaying = true;
    }

    if (lastEmittedGlobalStep.has_value())
    {
        const int previous = *lastEmittedGlobalStep;
        const bool discontinuity = globalStepStart != previous + 1 && globalStepStart != previous;

        if (discontinuity)
        {
            clearPending();
            resetCycleCounters();
            lastEmittedGlobalStep = globalStepStart;
            return result;
        }
    }

    const int firstStep = lastEmittedGlobalStep.has_value() ? *lastEmittedGlobalStep + 1 : globalStepStart;

    for (int globalStep = firstStep; globalStep <= globalStepEnd; ++globalStep)
    {
        const int stepSample = sampleOffsetForGlobalStep (snapshot, ppqPosition, bpm, globalStep, numSamples);
        evaluateGlobalStep (snapshot, globalStep, stepSample, numSamples, midiOut, result);
        lastEmittedGlobalStep = globalStep;
        result.latestGlobalStep = globalStep;
    }

    return result;
}

} // namespace ksh
