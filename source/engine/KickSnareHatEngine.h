#pragma once

#include "KshTypes.h"
#include "KshNativePlayback.h"

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace ksh
{
namespace test
{
struct EngineTestPeer;
}

struct MidiNoteEvent
{
    int pitch = 0;
    int velocity = 0;
    int channel = Constants::defaultMidiChannel;
    int durationMs = Constants::defaultNoteDurationMs;
    double delayMs = 0.0;
};

struct EngineCallbacks
{
    std::function<double()> rng;
    std::function<void (const std::string&)> emitStatus;
    std::function<void (const nlohmann::json&)> emitPreview;
    std::function<void (const MidiNoteEvent&)> emitNote;
};

struct EngineStateSnapshot
{
    int stepCount = 16;
    int channelCount = Constants::defaultChannelCount;
    int refreshSteps = 1;
    GenerationMode generationMode = GenerationMode::staticSource;
    int staticSource = 0;
    std::string rate = std::string { Constants::defaultRate };
    double tempo = 120.0;
    double stepIntervalMs = 125.0;
    int swing = 0;
    int velocityHumanize = 0;
    int timingHumanize = 0;
    bool deviceActive = true;
    int currentStep = 0;
    double phaseOffsetBeats = 0.0;
    int playingStepOneBased = 0;
    int nativePlaybackStepCount = 16;
    int transportPlaying = 0;
    std::optional<int> lastReportedGlobalStep;
    NativePlaybackTable nativePlaybackRows;
    std::array<Channel, Constants::maxChannels> channels {};
    std::array<SourcePattern, Constants::sourceCount> sources {};
    std::array<std::array<bool, Constants::maxChannels>, Constants::sourceCount> sourceChannelMutes {};
    GeneratedPattern generated {};
    int activeSourceIndicesCallCount = 0;
    int randomCallCount = 0;
};

class KickSnareHatEngine
{
public:
    explicit KickSnareHatEngine (EngineCallbacks callbacks = {});

    [[nodiscard]] EngineStateSnapshot stateSnapshot() const;
    [[nodiscard]] int getStepCount() const { return stepCount; }
    [[nodiscard]] int getChannelCount() const { return channelCount; }
    [[nodiscard]] int getRefreshSteps() const { return refreshSteps; }
    [[nodiscard]] GenerationMode getGenerationMode() const { return generationMode; }
    [[nodiscard]] int getStaticSource() const { return staticSource; }
    [[nodiscard]] std::string_view getRate() const { return rate; }
    [[nodiscard]] double getTempo() const { return tempo; }
    [[nodiscard]] int getSwing() const { return swing; }
    [[nodiscard]] int getVelocityHumanize() const { return velocityHumanize; }
    [[nodiscard]] int getTimingHumanize() const { return timingHumanize; }
    [[nodiscard]] bool isDeviceActive() const { return deviceActive; }
    [[nodiscard]] double getPhaseOffsetBeats() const { return phaseOffsetBeats; }
    [[nodiscard]] int getCurrentStep() const { return currentStep; }
    [[nodiscard]] int getPlayingStepOneBased() const { return playingStepOneBased; }
    [[nodiscard]] int getNativePlaybackStepCount() const { return nativePlaybackStepCount; }
    [[nodiscard]] const Channel& channelAt (int channel) const;
    [[nodiscard]] const Cell& sourceCellAt (int source, int channel, int step) const;
    [[nodiscard]] const Cell& generatedCellAt (int channel, int step) const;
    [[nodiscard]] const NativePlaybackRow& nativePlaybackRowAt (int step) const;
    [[nodiscard]] bool sourceChannelMutedAt (int source, int channel) const;

private:
    friend struct test::EngineTestPeer;

    struct ActiveSourceList
    {
        std::array<int, Constants::sourceCount> values {};
        int count = 0;

        void add (int source) { values[static_cast<size_t> (count++)] = source; }
        [[nodiscard]] bool empty() const { return count == 0; }
        [[nodiscard]] size_t size() const { return static_cast<size_t> (count); }
        [[nodiscard]] int operator[] (size_t index) const { return values[index]; }
    };

    int stepCount = 16;
    int channelCount = Constants::defaultChannelCount;
    int refreshSteps = 1;
    GenerationMode generationMode = GenerationMode::staticSource;
    int staticSource = 0;
    std::string rate = std::string { Constants::defaultRate };
    double tempo = 120.0;
    double stepIntervalMs = 125.0;
    int swing = 0;
    int velocityHumanize = 0;
    int timingHumanize = 0;
    bool deviceActive = true;
    int currentStep = 0;
    double phaseOffsetBeats = 0.0;
    int playingStepOneBased = 0;
    int nativePlaybackStepCount = 16;
    int transportPlaying = 0;
    std::optional<int> lastReportedGlobalStep;
    NativePlaybackTable nativePlaybackRows;

    std::array<Channel, Constants::maxChannels> channels {};
    std::array<SourcePattern, Constants::sourceCount> sources {};
    std::array<std::array<bool, Constants::maxChannels>, Constants::sourceCount> sourceChannelMutes {};
    GeneratedPattern generated {};

    /** Test hook: incremented on each {@link activeSourceIndices} call. */
    mutable int activeSourceIndicesCallCount = 0;
    /** Test hook: incremented on each RNG draw. */
    mutable int randomCallCount = 0;

public:
    void setStepCount (int count);
    void setChannelCount (int count);
    void setRefreshSteps (int count);
    void setGenerationMode (GenerationMode mode);
    void setStaticSource (int source);
    void setRate (std::string_view rate);
    void setTempo (double bpm);
    void setSwing (int amount);
    void setVelocityHumanize (int amount);
    void setTimingHumanize (int amount);
    void setDeviceActive (bool active);
    void setPhaseOffsetBeats (double beats);

    void setChannelLabel (int channel, std::string_view label);
    void setChannelNote (int channel, int note);
    void setChannelLock (int channel, int lock);
    void setChannelLoopLength (int channel, int loopLength);
    void setChannelPlaybackMode (int channel, PlaybackMode mode);

    void setCell (int source,
                  int channel,
                  int step,
                  bool enabled,
                  int velocity,
                  std::optional<int> probability = std::nullopt,
                  std::optional<int> cycle = std::nullopt,
                  std::optional<int> cycleOffset = std::nullopt,
                  std::optional<bool> cycleInverted = std::nullopt,
                  std::optional<int> roll = std::nullopt);

    void setCellEnabled (int source, int channel, int step, bool enabled);
    void setCellVelocity (int source, int channel, int step, int velocity);
    void setCellProbability (int source, int channel, int step, int probability);
    void setCellCycle (int source, int channel, int step, int cycle);
    void setCellCycleOffset (int source, int channel, int step, int cycleOffset);
    void setCellCycleInverted (int source, int channel, int step, bool cycleInverted);
    void setCellRoll (int source, int channel, int step, int roll);

    void setSourceChannelMute (int source, int channel, bool muted);
    void resetSourceChannel (int source, int channel);

    void generateWindow (int startStep, int length, bool forceEmit = false);
    void recomposeWindow (int startStep, int length, bool forceEmit = false);
    void reset();
    void resetPlayback (bool emitStatus = true);

    [[nodiscard]] bool isSourceEmpty (int sourceIndex) const;

    [[nodiscard]] nlohmann::json snapshot() const;
    [[nodiscard]] nlohmann::json serializeForPersistence() const;
    [[nodiscard]] bool deserializeForPersistence (const nlohmann::json& state);

    [[nodiscard]] std::optional<MidiNoteEvent> auditionChannel (int channel);

    [[nodiscard]] int nativePlaybackPeriod() const;
    [[nodiscard]] bool nativePlaybackSupported() const;
    [[nodiscard]] bool nativePlaybackActive() const { return deviceActive; }

    /** Immutable view for the audio thread. Built on the message thread. */
    [[nodiscard]] PlaybackSnapshot makePlaybackSnapshot() const;

    /** Bumped whenever playback-affecting state changes; lets owners publish only on change. */
    [[nodiscard]] unsigned long playbackSnapshotVersion() const { return playbackSnapshotVersion_; }
    [[nodiscard]] int playbackStepForChannel (int channel, int playbackIndex) const;
    [[nodiscard]] NativePlaybackBuild buildNativePlaybackRows (
        const std::optional<TransportProtection>& transportProtection = std::nullopt);
    void commitNativePlaybackBuild (NativePlaybackBuild build);
    [[nodiscard]] int globalStepForBeats (double songBeats) const;
    [[nodiscard]] double beatsPerStep() const;
    void transportPosition (double songBeats, bool isPlaying);
    void syncNativePlaybackTable();

    [[nodiscard]] static int mod (int value, int divisor);

private:
    EngineCallbacks callbacks;
    mutable std::vector<double> testRandomValues;
    mutable size_t testRandomIndex = 0;
    bool previewDirty = false;
    bool nativeTransportRefreshInProgress = false;
    unsigned long playbackSnapshotVersion_ = 0;

    void initChannels();
    void initSources();
    double nextRandom() const;
    void status (const std::string& message);
    void markPreviewDirty (bool forceEmit);
    void flushPreview();

    [[nodiscard]] ActiveSourceList activeSourceIndices() const;
    [[nodiscard]] int pickRandomSource (const ActiveSourceList* active = nullptr);
    [[nodiscard]] Cell generatedCellFromSource (int source, int channel, int step) const;
    [[nodiscard]] const Cell* generatedCellForSourceEdit (int source, int channel, int step) const;
    void refreshGeneratedCellsForSourceEdit (int source, int channel, int sourceStep);
    [[nodiscard]] bool isSourceChannelMuted (int source, int channel) const;
    void updateStepIntervalMs();

    [[nodiscard]] std::string cycleKey (int source, int channel, int step) const;
    [[nodiscard]] double swingDelayMsForStep (int step) const;
    [[nodiscard]] double playbackTimingHumanizeRangeMs() const;
    [[nodiscard]] double playbackHumanizeTimingOffsetMs();
    [[nodiscard]] int humanizeVelocity (int velocity);
    [[nodiscard]] int rollNoteDurationMs (int roll) const;
    void appendNativeHit (NativePlaybackRow& row,
                          int channel,
                          int rowStep,
                          const Cell& cell,
                          int velocity,
                          double delayMs,
                          int durationMs) const;
    void prepareStepForPlayback (int step);
    void reportTransportStep (int globalStep);
    [[nodiscard]] int currentNativePlaybackStep() const;
};

} // namespace ksh
