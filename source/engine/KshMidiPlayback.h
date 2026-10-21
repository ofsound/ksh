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
    bool generatedSourcesChanged = false; ///< A refresh window picked new sources this block.

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

struct MidiPatternSelectionEvent
{
    int samplePosition = 0;
    int source = 0;
};

struct MidiPatternSelectionBlock
{
    static constexpr size_t maxEvents = 128;

    std::array<MidiPatternSelectionEvent, maxEvents> events {};
    size_t count = 0;
    bool dropped = false;

    void clear()
    {
        count = 0;
        dropped = false;
    }

    void add (int samplePosition, int source)
    {
        if (count >= events.size())
        {
            dropped = true;
            return;
        }

        events[count++] = { samplePosition, source };
    }
};

/** Host cycle/loop region in quarter notes, when the host reports one. */
struct HostLoopRange
{
    bool active = false;
    double startPpq = 0.0;
    double endPpq = 0.0;
};

/** Audio-thread playback: evaluates {@link PlaybackSnapshot} live at each step boundary. */
class MidiPlaybackRunner
{
public:
    void prepare (double sampleRateIn);
    void reset (bool clearAuditions = true);
    void queueAuditionNote (const MidiNoteEvent& note);
    /** Audio thread: avoid replaying this take's cell attack after it was already monitored.
        The deadline is relative to the beginning of the next processBlock call. */
    void suppressRecordedHit (int source, int channel, int step, int64_t sampleOffset);
    void clearRecordingMonitors();

    /** Sends note-offs for every sounding note at sample 0 and drops queued note-ons. */
    void releaseHeldNotes (juce::MidiBuffer& midiOut);
    /** Releases held notes and treats the next block as a fresh transport start (bypass). */
    void suspend (juce::MidiBuffer& midiOut);

    MidiPlaybackResult processBlock (const PlaybackSnapshot& snapshot,
                                     double ppqPosition,
                                     double bpm,
                                     bool isPlaying,
                                     int numSamples,
                                     juce::MidiBuffer& midiOut,
                                     const MidiPatternSelectionBlock& patternSelections = {},
                                     const HostLoopRange& hostLoop = {},
                                     double callbackTimeMs = 0.0);

    /** Sources currently chosen for generative modes, tagged with the engine generation they extend. */
    [[nodiscard]] GeneratedSourcesFrame generatedSourcesFrame() const;

private:
    static constexpr size_t cycleCounterSlots =
        static_cast<size_t> (Constants::sourceCount) * Constants::maxChannels * Constants::maxSteps;

    struct PendingNoteOff
    {
        int64_t samplePosition = 0;
        int midiChannel = 1;
        int pitch = 0;
    };

    struct PendingNoteOn
    {
        int64_t samplePosition = 0;
        NativeHit hit {};
    };

    static constexpr size_t maxPendingNoteOffs =
        static_cast<size_t> (Constants::maxChannels) * Constants::maxRoll * 16;

    static constexpr size_t maxPendingNoteOns =
        static_cast<size_t> (Constants::maxChannels) * Constants::maxRoll * 16;

    double sampleRate = 44100.0;
    // Monotonic callback sample timeline, independent of host seeks and block size.
    int64_t blockStartSample = 0;
    int64_t nextBlockStartSample = 0;
    bool wasPlaying = false;
    double expectedNextPpq = 0.0;
    std::optional<int> lastEmittedGlobalStep;
    std::optional<int> lastScheduledGlobalStep;
    std::optional<int> anticipatedLoopStep;
    std::array<PendingNoteOff, maxPendingNoteOffs> pendingNoteOffs {};
    size_t pendingNoteOffCount = 0;
    std::array<PendingNoteOn, maxPendingNoteOns> pendingNoteOns {};
    size_t pendingNoteOnCount = 0;
    std::array<int64_t, cycleCounterSlots> recordedHitSamples {};
    int recordingSuppressionTolerance = 2;
    [[nodiscard]] bool wasRecordedHit (const NativeHit&, int64_t sample) const;

    struct CycleDefinition
    {
        int cycle = 0;
        int mask = 0;

        [[nodiscard]] bool operator== (const CycleDefinition&) const = default;
    };

    std::array<uint16_t, cycleCounterSlots> cycleCounters {};
    std::array<CycleDefinition, cycleCounterSlots> cycleDefinitions {};
    GeneratedSources generatedSources {};
    std::optional<unsigned long> adoptedGenerationVersion;
    uint32_t rngState = 0x12345678u;
    static constexpr int noStaticSourceOverride = Constants::mutedStaticSource - 1;
    int pendingStaticSourceOverride = noStaticSourceOverride;

    [[nodiscard]] int sampleOffsetForGlobalStep (const PlaybackSnapshot& snapshot,
                                                 double blockStartPpq,
                                                 double bpm,
                                                 int globalStep,
                                                 int numSamples) const;

    void clearPending();
    void dropPendingNoteOnsAtOrAfter (int sampleOffset);
    void resetCycleCounters();
    [[nodiscard]] double nextRandomUnit();
    void adoptGeneratedSources (const PlaybackSnapshot& snapshot);
    [[nodiscard]] bool regenerateWindowIfDue (const PlaybackSnapshot& snapshot, int globalStep);
    [[nodiscard]] Cell generatedCellForStep (const PlaybackSnapshot& snapshot, int channel, int playbackStep) const;
    [[nodiscard]] int staticSourceForStep (const PlaybackSnapshot& snapshot,
                                           const MidiPatternSelectionBlock& patternSelections,
                                           int stepSampleOffset);
    void evaluateGlobalStep (const PlaybackSnapshot& snapshot,
                             int globalStep,
                             int stepSampleOffset,
                             int numSamples,
                             juce::MidiBuffer& midiOut,
                             MidiPlaybackResult& result,
                             int staticSourceOverride,
                             double stepDurationMs);
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
    void flushAuditionNotes (int numSamples, juce::MidiBuffer& midi, double callbackTimeMs);
    void addPendingNoteOff (int sampleOffset, int midiChannel, int pitch);
    void addPendingNoteOn (int sampleOffset, const NativeHit& hit);

    moodycamel::ReaderWriterQueue<MidiNoteEvent> pendingAuditions { 64 };
};

} // namespace ksh
