#pragma once

#include "KickSnareHatEngine.h"
#include "KshNativePlayback.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <optional>
#include <vector>

namespace ksh
{

struct MidiPlaybackResult
{
    juce::MidiBuffer midi;
    std::vector<NativeHit> noteHits;
};

/** Audio-thread playback: transport sync, native step edges, delayed note scheduling. */
class MidiPlaybackRunner
{
public:
    void prepare (double sampleRateIn);
    void reset();
    void queueAuditionNote (const MidiNoteEvent& note);

    MidiPlaybackResult processBlock (KickSnareHatEngine& engine,
                                     double ppqPosition,
                                     double bpm,
                                     bool isPlaying,
                                     int numSamples);

private:
    struct PendingNoteOff
    {
        int sampleOffset = 0;
        int midiChannel = 1;
        int pitch = 0;
    };

    double sampleRate = 44100.0;
    bool wasPlaying = false;
    std::optional<int> lastEmittedGlobalStep;
    std::vector<PendingNoteOff> pendingNoteOffs;

    [[nodiscard]] int sampleOffsetForGlobalStep (const KickSnareHatEngine& engine,
                                                 double blockStartPpq,
                                                 double bpm,
                                                 int globalStep,
                                                 int numSamples) const;

    void clearPending();
    void emitNativeRow (KickSnareHatEngine& engine,
                        int globalStep,
                        int stepSampleOffset,
                        int numSamples,
                        MidiPlaybackResult& result);
    void flushPendingNoteOffs (int numSamples, juce::MidiBuffer& midi);
    void flushAuditionNotes (int numSamples, juce::MidiBuffer& midi);

    juce::CriticalSection auditionLock;
    std::optional<MidiNoteEvent> pendingAudition;
};

} // namespace ksh
