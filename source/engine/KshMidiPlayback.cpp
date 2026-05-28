#include "KshMidiPlayback.h"

#include <cmath>

namespace ksh
{
namespace
{
int clampSample (int sample, int numSamples)
{
    if (numSamples <= 0)
        return 0;

    return std::clamp (sample, 0, numSamples - 1);
}
} // namespace

void MidiPlaybackRunner::prepare (double sampleRateIn)
{
    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
    reset();
}

void MidiPlaybackRunner::reset()
{
    wasPlaying = false;
    lastEmittedGlobalStep = std::nullopt;
    pendingNoteOffs.clear();
}

void MidiPlaybackRunner::clearPending()
{
    pendingNoteOffs.clear();
}

int MidiPlaybackRunner::sampleOffsetForGlobalStep (const KickSnareHatEngine& engine,
                                                   double blockStartPpq,
                                                   double bpm,
                                                   int globalStep,
                                                   int numSamples) const
{
    if (bpm <= 0.0 || numSamples <= 0)
        return 0;

    const double stepStartBeat = engine.phaseOffsetBeats + static_cast<double> (globalStep) * engine.beatsPerStep();
    const double samplesPerBeat = sampleRate * 60.0 / bpm;
    const int sampleOffset = static_cast<int> (std::llround ((stepStartBeat - blockStartPpq) * samplesPerBeat));

    return clampSample (sampleOffset, numSamples);
}

void MidiPlaybackRunner::flushPendingNoteOffs (int numSamples, juce::MidiBuffer& midi)
{
    if (pendingNoteOffs.empty())
        return;

    std::vector<PendingNoteOff> remaining;
    remaining.reserve (pendingNoteOffs.size());

    for (const auto& pending : pendingNoteOffs)
    {
        if (pending.sampleOffset < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (pending.midiChannel, pending.pitch),
                           clampSample (pending.sampleOffset, numSamples));
        }
        else
        {
            remaining.push_back ({ pending.sampleOffset - numSamples, pending.midiChannel, pending.pitch });
        }
    }

    pendingNoteOffs.swap (remaining);
}

void MidiPlaybackRunner::emitNativeRow (KickSnareHatEngine& engine,
                                        int globalStep,
                                        int stepSampleOffset,
                                        int numSamples,
                                        MidiPlaybackResult& result)
{
    juce::ignoreUnused (globalStep);

    if (! engine.nativePlaybackActive())
        return;

    const int nativeLength = engine.nativePlaybackStepCount > 0 ? engine.nativePlaybackStepCount : engine.stepCount;
    const int nativeStep = KickSnareHatEngine::mod (globalStep, nativeLength);

    if (nativeStep < 0 || nativeStep >= static_cast<int> (engine.nativePlaybackRows.size()))
        return;

    const auto& row = engine.nativePlaybackRows[static_cast<size_t> (nativeStep)];

    for (const auto& hit : row)
    {
        const int delaySamples = static_cast<int> (std::llround (hit.delayMs * sampleRate / 1000.0));
        const int onSample = clampSample (stepSampleOffset + delaySamples, numSamples);
        const int durationSamples =
            std::max (1, static_cast<int> (std::llround (static_cast<double> (hit.durationMs) * sampleRate / 1000.0)));
        const int offSample = onSample + durationSamples;

        result.midi.addEvent (juce::MidiMessage::noteOn (hit.midiChannel, hit.pitch,
                                                         static_cast<juce::uint8> (std::clamp (hit.velocity, 1, 127))),
                              onSample);
        result.noteHits.push_back (hit);

        if (offSample < numSamples)
        {
            result.midi.addEvent (juce::MidiMessage::noteOff (hit.midiChannel, hit.pitch), offSample);
        }
        else
        {
            pendingNoteOffs.push_back ({ offSample, hit.midiChannel, hit.pitch });
        }
    }
}

MidiPlaybackResult MidiPlaybackRunner::processBlock (KickSnareHatEngine& engine,
                                                     double ppqPosition,
                                                     double bpm,
                                                     bool isPlaying,
                                                     int numSamples)
{
    MidiPlaybackResult result;

    if (numSamples <= 0)
        return result;

    flushPendingNoteOffs (numSamples, result.midi);

    if (! isPlaying || ! engine.deviceActive || bpm <= 0.0)
    {
        if (wasPlaying)
            clearPending();

        wasPlaying = false;
        lastEmittedGlobalStep = std::nullopt;
        engine.transportPosition (ppqPosition, false);
        return result;
    }

    if (std::abs (engine.tempo - bpm) > 0.01)
        engine.setTempo (bpm);

    const double blockEndPpq = ppqPosition + (static_cast<double> (numSamples) / sampleRate) * (bpm / 60.0);

    engine.transportPosition (ppqPosition, true);

    const int globalStepStart = engine.globalStepForBeats (ppqPosition);
    const int globalStepEnd = engine.globalStepForBeats (blockEndPpq);

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
            lastEmittedGlobalStep = globalStepStart;
            return result;
        }
    }

    const int firstStep = lastEmittedGlobalStep.has_value() ? *lastEmittedGlobalStep + 1 : globalStepStart;

    for (int globalStep = firstStep; globalStep <= globalStepEnd; ++globalStep)
    {
        const int stepSample = sampleOffsetForGlobalStep (engine, ppqPosition, bpm, globalStep, numSamples);
        emitNativeRow (engine, globalStep, stepSample, numSamples, result);
        lastEmittedGlobalStep = globalStep;
    }

    return result;
}

} // namespace ksh
