#pragma once

#include "KickSnareHatEngine.h"
#include "KshNativePlayback.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace ksh
{

struct MidiPlaybackResult
{
    juce::MidiBuffer midi;
    std::vector<NativeHit> noteHits;
    int currentStepOneBased = 0; ///< Playing step for the UI (0 = stopped).
    int latestGlobalStep = 0;    ///< Newest global step the audio thread reached.
    bool playing = false;        ///< Whether the transport was advancing this block.
};

/** Audio-thread playback: evaluates {@link PlaybackSnapshot} live at each step boundary. */
class MidiPlaybackRunner
{
public:
    void prepare (double sampleRateIn);
    void reset();
    void queueAuditionNote (const MidiNoteEvent& note);

    MidiPlaybackResult processBlock (const PlaybackSnapshot& snapshot,
                                     double ppqPosition,
                                     double bpm,
                                     bool isPlaying,
                                     int numSamples);

private:
    static constexpr size_t cycleCounterSlots =
        static_cast<size_t> (Constants::sourceCount) * Constants::maxChannels * Constants::maxSteps;

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
    std::array<uint16_t, cycleCounterSlots> cycleCounters {};
    uint32_t rngState = 0x12345678u;

    [[nodiscard]] int sampleOffsetForGlobalStep (const PlaybackSnapshot& snapshot,
                                                 double blockStartPpq,
                                                 double bpm,
                                                 int globalStep,
                                                 int numSamples) const;

    void clearPending();
    void resetCycleCounters();
    [[nodiscard]] double nextRandomUnit();
    void evaluateGlobalStep (const PlaybackSnapshot& snapshot,
                             int globalStep,
                             int stepSampleOffset,
                             int numSamples,
                             MidiPlaybackResult& result);
    void scheduleHit (int stepSampleOffset,
                      int numSamples,
                      const NativeHit& hit,
                      MidiPlaybackResult& result);
    void flushPendingNoteOffs (int numSamples, juce::MidiBuffer& midi);
    void flushAuditionNotes (int numSamples, juce::MidiBuffer& midi);

    juce::CriticalSection auditionLock;
    std::optional<MidiNoteEvent> pendingAudition;
};

} // namespace ksh
