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

class KickSnareHatEngine
{
public:
    explicit KickSnareHatEngine (EngineCallbacks callbacks = {});

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
    [[nodiscard]] std::vector<int> activeSourceIndices() const;
    [[nodiscard]] int pickRandomSource (const std::vector<int>* active = nullptr);

    [[nodiscard]] nlohmann::json snapshot() const;
    [[nodiscard]] nlohmann::json serialize() const;
    void deserialize (const nlohmann::json& state);
    [[nodiscard]] nlohmann::json serializeForPersistence() const;
    [[nodiscard]] bool deserializeForPersistence (const nlohmann::json& state);

    [[nodiscard]] std::optional<MidiNoteEvent> auditionChannel (int channel);

    [[nodiscard]] int nativePlaybackPeriod() const;
    [[nodiscard]] bool nativePlaybackSupported() const;
    [[nodiscard]] bool nativePlaybackActive() const;
    [[nodiscard]] int playbackStepForChannel (int channel, int playbackIndex) const;
    [[nodiscard]] NativePlaybackBuild buildNativePlaybackRows (
        const std::optional<TransportProtection>& transportProtection = std::nullopt);
    void commitNativePlaybackBuild (NativePlaybackBuild build);
    [[nodiscard]] int globalStepForBeats (double songBeats) const;
    [[nodiscard]] double beatsPerStep() const;
    void transportPosition (double songBeats, bool isPlaying);
    void syncNativePlaybackTable();

    /** Test hook: incremented on each RNG draw. */
    mutable int randomCallCount = 0;

    [[nodiscard]] static int mod (int value, int divisor);

    void setRandomValuesForTests (std::vector<double> values);

private:
    EngineCallbacks callbacks;
    mutable std::vector<double> testRandomValues;
    mutable size_t testRandomIndex = 0;
    bool previewDirty = false;
    bool nativeTransportRefreshInProgress = false;

    void initChannels();
    void initSources();
    double nextRandom() const;
    void status (const std::string& message);
    void markPreviewDirty (bool forceEmit);
    void flushPreview();

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
