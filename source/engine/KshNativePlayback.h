#pragma once

#include "KshConstants.h"
#include "KshTypes.h"

#include <array>
#include <vector>

namespace ksh
{

/** Flat native playback hit matching M4L coll row fields (9 values per hit). */
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

    [[nodiscard]] bool operator== (const NativeHit&) const = default;
};

using NativePlaybackRow = std::vector<NativeHit>;
using NativePlaybackTable = std::vector<NativePlaybackRow>;

/** Flat row encoding used by M4L tests (9 fields per hit). */
[[nodiscard]] inline std::vector<double> nativeHitRow (int pitch,
                                                       int velocity,
                                                       int durationMs,
                                                       int midiChannel,
                                                       double delayMs,
                                                       int uiChannel,
                                                       int uiGeneratedStep,
                                                       int uiSource,
                                                       int uiSourceStep)
{
    return {
        static_cast<double> (pitch),
        static_cast<double> (velocity),
        static_cast<double> (durationMs),
        static_cast<double> (midiChannel),
        delayMs,
        static_cast<double> (uiChannel),
        static_cast<double> (uiGeneratedStep),
        static_cast<double> (uiSource),
        static_cast<double> (uiSourceStep)
    };
}

[[nodiscard]] inline std::vector<double> flattenNativeRow (const NativePlaybackRow& row)
{
    std::vector<double> flat;
    flat.reserve (row.size() * Constants::nativeHitFieldCount);

    for (const auto& hit : row)
    {
        flat.push_back (static_cast<double> (hit.pitch));
        flat.push_back (static_cast<double> (hit.velocity));
        flat.push_back (static_cast<double> (hit.durationMs));
        flat.push_back (static_cast<double> (hit.midiChannel));
        flat.push_back (hit.delayMs);
        flat.push_back (static_cast<double> (hit.uiChannel));
        flat.push_back (static_cast<double> (hit.uiGeneratedStep));
        flat.push_back (static_cast<double> (hit.uiSource));
        flat.push_back (static_cast<double> (hit.uiSourceStep));
    }

    return flat;
}

struct TransportProtection
{
    int globalStep = 0;
    bool includeCurrent = false;
};

/** Result of {@link KickSnareHatEngine::buildNativePlaybackRows}; commit via syncNativePlaybackTable. */
struct NativePlaybackBuild
{
    NativePlaybackTable rows;
    int stepCount = 0;
};

/** Immutable, realtime-safe view of everything the audio thread needs to evaluate steps live.
    Built on the message thread and handed to the audio thread via a lock-free mailbox. */
struct PlaybackSnapshot
{
    GeneratedPattern generated {};
    std::array<SourcePattern, Constants::sourceCount> sources {};
    std::array<SourceSettings, Constants::sourceCount> sourceSettings {};
    std::array<std::array<bool, Constants::maxChannels>, Constants::sourceCount> sourceChannelMutes {};
    std::array<Channel, Constants::maxChannels> channels {};
    GenerationMode generationMode = GenerationMode::staticSource;
    int staticSource = 0;
    int stepCount = 16;
    int channelCount = Constants::defaultChannelCount;
    double beatsPerStep = 0.25;
    double tempo = 120.0;
    double stepIntervalMs = 125.0;
    int swing = 0;
    int swingSubdivisionIndex = Constants::defaultSwingSubdivisionIndex;
    int velocityHumanize = 0;
    int timingHumanize = 0;
    bool deviceActive = true;
};

} // namespace ksh
