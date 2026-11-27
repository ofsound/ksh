#pragma once

#include "KickSnareHatEngine.h"
#include "KshNativePlayback.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <readerwriterqueue.h>

#include <array>
#include <cstdint>
#include <optional>
#include <memory>

namespace ksh
{

struct MidiPlaybackResult
{
    static constexpr size_t maxNoteHitsPerBlock =
        static_cast<size_t> (Constants::maxChannels) * Constants::maxRoll * 8;

    std::array<NativeHit, maxNoteHitsPerBlock> noteHits {};
    std::array<NativeCycleVisit, maxNoteHitsPerBlock> cycleVisits {};
    size_t cycleVisitCount = 0;
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
    void resetForAudioDeviceRestart();
    void queueAuditionNote (const MidiNoteEvent& note);
    /** Audio thread: avoid replaying this take's cell attack after it was already monitored.
        The deadline is relative to the beginning of the next processBlock call. */
    void suppressRecordedHit (int source, int channel, int step, int64_t sampleOffset);
    void clearRecordingMonitors();

    /** Sends note-offs for every sounding note at sample 0 and drops queued note-ons. */
    void releaseHeldNotes (juce::MidiBuffer& midiOut);
    /** Releases held notes and treats the next block as a fresh transport start (bypass). */
    void suspend (juce::MidiBuffer& midiOut);

    /** endGlobalStepExclusive bounds lookahead when rendering a finite MIDI export. */
    MidiPlaybackResult processBlock (const PlaybackSnapshot& snapshot,
                                     double ppqPosition,
                                     double bpm,
                                     bool isPlaying,
                                     int numSamples,
                                     juce::MidiBuffer& midiOut,
                                     const MidiPatternSelectionBlock& patternSelections = {},
                                     const HostLoopRange& hostLoop = {},
                                     double callbackTimeMs = 0.0,
                                     std::optional<int> endGlobalStepExclusive = std::nullopt,
                                     int firstPlaybackSample = 0,
                                     const PlaybackMacros* blockMacros = nullptr);

    /** Sources currently chosen for generative modes, tagged with the engine generation they extend. */
    [[nodiscard]] GeneratedSourcesFrame generatedSourcesFrame() const;

private:
    static constexpr size_t cycleCounterSlots =
        static_cast<size_t> (Constants::sourceCount) * Constants::maxChannels * Constants::maxSteps;

    struct BlockAttack
    {
        int sampleOffset;
        int durationSamples;
        size_t order;
        size_t key;
        juce::uint8 velocity;
    };

    struct PendingNoteOn
    {
        double beat = 0.0;
        NativeHit hit {};
        int span = 1;
        int roll = 1;
    };

    struct ControllerPoint
    {
        double beat = 0.0, endBeat = 0.0;
        int row = 0, source = -1, step = 0, number = 0, channel = 1, value = 0;
        int part = 0, roll = 1, span = 1;
        int endStep = 0, endPart = 0, endRoll = 1, endValue = 0, intervals = 0;
        uint64_t order = 0;
    };

    struct ControllerRamp
    {
        ControllerPoint point;
        int next = 0;
    };

    static constexpr size_t maxPendingNoteOns =
        static_cast<size_t> (Constants::maxChannels) * Constants::maxRoll * 16;
    static constexpr size_t maxBlockAttacks = maxPendingNoteOns
        + static_cast<size_t> (Constants::maxChannels) * Constants::maxRoll * Constants::maxSteps * 2;
    static constexpr size_t midiKeyCount = 16 * 128;

    double sampleRate = 44100.0;
    // Monotonic callback sample timeline, independent of host seeks and block size.
    int64_t blockStartSample = 0;
    int64_t nextBlockStartSample = 0;
    bool wasPlaying = false;
    double expectedNextPpq = 0.0;
    double previousBeatsPerStep = 0.0;
    double blockStartBeat = 0.0;
    double beatsPerSample = 0.0;
    double loopBeatOffset = 0.0;
    std::optional<int> lastEmittedGlobalStep;
    std::optional<int> lastScheduledGlobalStep;
    std::optional<int> anticipatedLoopStep;
    const std::unique_ptr<BlockAttack[]> blockAttacks = std::make_unique<BlockAttack[]> (maxBlockAttacks);
    size_t blockAttackCount = 0;
    std::array<int64_t, midiKeyCount> gateDeadlines {};
    std::array<size_t, midiKeyCount> activeKeys {};
    std::array<size_t, midiKeyCount> restartNoteOffKeys {};
    size_t restartNoteOffCount = 0;
    size_t activeKeyCount = 0;
    std::array<PendingNoteOn, maxPendingNoteOns> pendingNoteOns {};
    size_t pendingNoteOnCount = 0;
    using ControllerPoints = std::array<ControllerPoint, Constants::maxChannels * Constants::maxSteps * Constants::maxRoll * 2>;
    const std::unique_ptr<ControllerPoints> controllerPoints = std::make_unique<ControllerPoints>();
    size_t controllerPointCount = 0;
    uint64_t controllerPointOrder = 0;
    std::array<ControllerRamp, Constants::maxChannels> controllerRamps {};
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
    struct SplitCycleState
    {
        uint16_t definition = 0;
        uint8_t nextPhase = 0;
    };
    using SplitCycles = std::array<std::array<SplitCycleState, Constants::maxRoll>, cycleCounterSlots>;
    const std::unique_ptr<SplitCycles> splitCycles = std::make_unique<SplitCycles>();
    GeneratedSources generatedSources {};
    std::optional<unsigned long> adoptedGenerationVersion;
    uint32_t rngState = 0x12345678u;
    uint32_t controllerRngState = 0x12345678u;
    static constexpr int noStaticSourceOverride = Constants::mutedStaticSource - 1;
    int pendingStaticSourceOverride = noStaticSourceOverride;
    uint64_t adoptedPatternSelectionRevision = 0;
    std::optional<int> previousHostPattern;

    [[nodiscard]] int sampleOffsetForBeat (double beat) const;

    void clearPending();
    void dropPendingNoteOnsAtOrAfter (int sampleOffset);
    void resetCycleCounters();
    [[nodiscard]] double nextRandomUnit();
    [[nodiscard]] double nextTimingOffset (double range);
    void adoptGeneratedSources (const PlaybackSnapshot& snapshot);
    [[nodiscard]] bool regenerateWindowIfDue (const PlaybackSnapshot& snapshot, int globalStep);
    [[nodiscard]] Cell generatedCellForStep (const PlaybackSnapshot& snapshot, int channel, int playbackStep) const;
    [[nodiscard]] std::pair<int, Cell> playbackCell (const PlaybackSnapshot&, int channel, int source, int globalStep) const;
    void evaluateGlobalStep (const PlaybackSnapshot& snapshot,
                             const PlaybackMacros& macros,
                             int globalStep,
                             double stepBeat,
                             int numSamples,
                             MidiPlaybackResult& result,
                             MidiPatternSelectionEvent selection,
                             double beatsPerStep,
                             int earliestHitSample,
                             int spanEndSample,
                             bool skipElapsedHits = false);
    void scheduleHit (double beat,
                      int numSamples,
                      const NativeHit& hit,
                      MidiPlaybackResult& result,
                      int earliestHitSample,
                      int span,
                      int roll,
                      bool rowMuted);
    void collectPendingNoteOns (const PlaybackSnapshot&, const PlaybackMacros&, int activeSource, int numSamples,
                                double stepDurationMs, MidiPlaybackResult&);
    void collectAuditionNotes (int numSamples, double callbackTimeMs);
    void emitRestartNoteOffs (juce::MidiBuffer&);
    bool collectAttack (int onSample, int midiChannel, int pitch, int velocity, int durationMs);
    void renderAttacksAndGates (int numSamples, juce::MidiBuffer&);
    void scheduleController (const PlaybackSnapshot&, const PlaybackMacros&, const Cell&, int part, int row, int source,
                             int globalStep, double stepBeat, double beatsPerStep, double beat, int earliestSample, int endSample);
    void renderControllers (const PlaybackSnapshot&, const PlaybackMacros&, const MidiPatternSelectionBlock&, int sourceAtStart,
                            int numSamples, int endSample, juce::MidiBuffer&);

    moodycamel::ReaderWriterQueue<MidiNoteEvent> pendingAuditions { 64 };
};

} // namespace ksh
