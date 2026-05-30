#pragma once

#include "KickSnareHatEngine.h"
#include "KshNativePlayback.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <readerwriterqueue.h>

#include <array>
#include <cstdint>
#include <optional>

namespace ksh
{

struct MidiPlaybackResult
{
    static constexpr size_t maxNoteHitsPerBlock =
        static_cast<size_t> (Constants::maxChannels) * Constants::maxRoll * 8;

    std::array<NativeHit, maxNoteHitsPerBlock> noteHits {};
    size_t noteHitCount = 0;
    bool noteHitsDropped = false;
    int currentStepOneBased = 0; ///< Playing step for the UI (0 = stopped).
    int latestGlobalStep = 0;    ///< Newest global step the audio thread reached.
    bool playing = false;        ///< Whether the transport was advancing this block.

    void addNoteHit (const NativeHit& hit)
    {
        if (noteHitCount < noteHits.size())
        {
            noteHits[noteHitCount++] = hit;
            return;
        }

        noteHitsDropped = true;
    }
};

/** Audio-thread playback: evaluates {@link PlaybackSnapshot} live at each step boundary. */
class MidiPlaybackRunner
{
public:
    void prepare (double sampleRateIn);
    void reset (bool clearAuditions = true);
    void queueAuditionNote (const MidiNoteEvent& note);

    MidiPlaybackResult processBlock (const PlaybackSnapshot& snapshot,
                                     double ppqPosition,
                                     double bpm,
                                     bool isPlaying,
                                     int numSamples,
                                     juce::MidiBuffer& midiOut);

private:
    static constexpr size_t cycleCounterSlots =
        static_cast<size_t> (Constants::sourceCount) * Constants::maxChannels * Constants::maxSteps;

    struct PendingNoteOff
    {
        int sampleOffset = 0;
        int midiChannel = 1;
        int pitch = 0;
    };

    struct PendingNoteOn
    {
        int sampleOffset = 0;
        NativeHit hit {};
    };

    static constexpr size_t maxPendingNoteOffs =
        static_cast<size_t> (Constants::maxChannels) * Constants::maxRoll * 16;

    static constexpr size_t maxPendingNoteOns =
        static_cast<size_t> (Constants::maxChannels) * Constants::maxRoll * 16;

    double sampleRate = 44100.0;
    bool wasPlaying = false;
    std::optional<int> lastEmittedGlobalStep;
    std::array<PendingNoteOff, maxPendingNoteOffs> pendingNoteOffs {};
    size_t pendingNoteOffCount = 0;
    std::array<PendingNoteOn, maxPendingNoteOns> pendingNoteOns {};
    size_t pendingNoteOnCount = 0;
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
                             juce::MidiBuffer& midiOut,
                             MidiPlaybackResult& result);
    void scheduleHit (int stepSampleOffset,
                      int numSamples,
                      const NativeHit& hit,
                      juce::MidiBuffer& midiOut,
                      MidiPlaybackResult& result);
    void emitHitAtSample (int onSample,
                          int numSamples,
                          const NativeHit& hit,
                          juce::MidiBuffer& midiOut,
                          MidiPlaybackResult& result);
    void flushPendingNoteOns (int numSamples, juce::MidiBuffer& midiOut, MidiPlaybackResult& result);
    void flushPendingNoteOffs (int numSamples, juce::MidiBuffer& midi);
    void flushAuditionNotes (int numSamples, juce::MidiBuffer& midi);
    void addPendingNoteOff (int sampleOffset, int midiChannel, int pitch);
    void addPendingNoteOn (int sampleOffset, const NativeHit& hit);

    moodycamel::ReaderWriterQueue<MidiNoteEvent> pendingAuditions { 64 };
};

} // namespace ksh
