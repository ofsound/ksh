#pragma once

#include "KshConstants.h"
#include "KshTypes.h"

#include <array>
#include <cstdint>

namespace ksh
{

/** One emitted note and the grid cell it came from, reported from the audio thread for UI flashes. */
struct NativeHit
{
    int pitch = 0;
    int velocity = 0;
    int durationMs = Constants::defaultNoteDurationMs;
    int midiChannel = Constants::defaultMidiChannel;
    double delayMs = 0.0;
    int uiChannel = 0;
    int uiGeneratedStep = 0;
    int uiSource = 0;
    int uiSourceStep = 0;
    int uiCycleStep = 0; ///< One-based cycle gate position for this emitted note.

    [[nodiscard]] bool operator== (const NativeHit&) const = default;
};

/** A source cell reached this cycle position, including turns with its gate switched off. */
struct NativeCycleVisit
{
    int source = 0;
    int channel = 0;
    int step = 0;
    int cycleStep = 0;
};

/** Source chosen for each generated cell (-1 = none), as picked by the audio thread. */
using GeneratedSources = std::array<std::array<int8_t, Constants::maxSteps>, Constants::maxChannels>;

struct GeneratedSourcesFrame
{
    GeneratedSources sources {};
    unsigned long basisVersion = 0; ///< Engine generation the audio thread had adopted.
};

/** Immutable, realtime-safe view of everything the audio thread needs to evaluate steps live.
    Built on the message thread and handed to the audio thread via a lock-free mailbox. */
struct PlaybackSnapshot
{
    /** Bumped when the message thread deliberately chooses new generated sources;
        the audio thread then adopts {@link generated} instead of its own choices. */
    unsigned long generationVersion = 0;
    GeneratedPattern generated {};
    std::array<SourcePattern, Constants::sourceCount> sources {};
    std::array<SourceSettings, Constants::sourceCount> sourceSettings {};
    std::array<std::array<bool, Constants::maxChannels>, Constants::sourceCount> sourceChannelMutes {};
    std::array<Channel, Constants::maxChannels> channels {};
    GenerationMode generationMode = GenerationMode::staticSource;
    int staticSource = 0;
    int stepCount = 16;
    int channelCount = Constants::defaultChannelCount;
    int refreshSteps = 16;
    double beatsPerStep = 0.25;
    double tempo = 120.0;
    double stepIntervalMs = 125.0;
    int swing = 0;
    int swingSubdivisionIndex = Constants::defaultSwingSubdivisionIndex;
    int velocityHumanize = 0;
    int timingHumanize = 0;
    bool deviceActive = true;
    bool hasEarlyCells = false;
};

} // namespace ksh
