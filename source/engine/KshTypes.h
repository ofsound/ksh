#pragma once

#include "KshConstants.h"

#include <array>
#include <cstdint>
#include <string>

namespace ksh
{

enum class GenerationMode
{
    stack,
    perChannel,
    staticSource
};

[[nodiscard]] inline GenerationMode normalizeGenerationMode (std::string_view mode)
{
    const std::string lower (mode);

    if (lower == "perchannel" || lower == "per_channel" || lower == "per-channel")
        return GenerationMode::perChannel;

    if (lower == "static")
        return GenerationMode::staticSource;

    return GenerationMode::stack;
}

[[nodiscard]] inline std::string_view generationModeToString (GenerationMode mode)
{
    switch (mode)
    {
        case GenerationMode::perChannel: return "per_channel";
        case GenerationMode::staticSource: return "static";
        case GenerationMode::stack: return "stack";
    }

    return "stack";
}

enum class PlaybackMode
{
    normal,
    reverse,
    ping_pong,
    reverse_ping_pong
};

[[nodiscard]] inline PlaybackMode normalizePlaybackMode (std::string_view mode)
{
    const auto normalized = Constants::normalizeChannelPlaybackMode (mode);

    if (normalized == "reverse")
        return PlaybackMode::reverse;

    if (normalized == "ping_pong")
        return PlaybackMode::ping_pong;

    if (normalized == "reverse_ping_pong")
        return PlaybackMode::reverse_ping_pong;

    return PlaybackMode::normal;
}

[[nodiscard]] inline std::string playbackModeToString (PlaybackMode mode)
{
    switch (mode)
    {
        case PlaybackMode::reverse: return "reverse";
        case PlaybackMode::ping_pong: return "ping_pong";
        case PlaybackMode::reverse_ping_pong: return "reverse_ping_pong";
        case PlaybackMode::normal: return "normal";
    }

    return "normal";
}

[[nodiscard]] inline bool playbackModeIsPingPong (PlaybackMode mode)
{
    return mode == PlaybackMode::ping_pong || mode == PlaybackMode::reverse_ping_pong;
}

[[nodiscard]] inline bool normalizeToggle (std::string_view value)
{
    const std::string lower (value);

    return ! (lower == "0" || lower == "false" || lower == "off");
}

struct Cell
{
    bool enabled = false;
    int velocity = 100;
    int probability = 100;
    int cycle = 1;
    int cycleMask = 1;
    int roll = 1;
    int source = -1;
    int sourceStep = 0;

    [[nodiscard]] bool operator== (const Cell&) const = default;
};

struct CellDefaults
{
    static constexpr bool enabled = false;
    static constexpr int velocity = 100;
    static constexpr int probability = 100;
    static constexpr int cycle = 1;
    static constexpr int cycleMask = 1;
    static constexpr int roll = 1;
    static constexpr int source = -1;
};

[[nodiscard]] inline Cell defaultCell()
{
    return {};
}

inline constexpr int maxCycleMaskBits = 30;

[[nodiscard]] inline int cycleMaskForLength (int cycle)
{
    const auto bitCount = clampInt (cycle, 1, maxCycleMaskBits);
    return (1 << bitCount) - 1;
}

[[nodiscard]] inline int clampCycleMask (int mask, int cycle)
{
    return std::max (0, mask) & cycleMaskForLength (cycle);
}

[[nodiscard]] inline int cycleMaskFromLegacyOffset (int offset, int cycle, bool inverted)
{
    const auto length = clampInt (cycle, 1, 64);
    const auto phase = clampInt (offset, 0, length - 1);
    const auto oneHot = 1 << clampInt (phase, 0, maxCycleMaskBits - 1);
    return inverted ? cycleMaskForLength (length) & ~oneHot : oneHot;
}

[[nodiscard]] inline int normalizeCycleMask (int mask, int cycle)
{
    const auto normalized = clampCycleMask (mask, cycle);
    return normalized == 0 ? 1 : normalized;
}

[[nodiscard]] inline bool cycleGateMatches (int count, int cycle, int mask)
{
    const auto length = clampInt (cycle, 1, 64);
    const auto pattern = normalizeCycleMask (mask, length);
    const auto phase = ((count % length) + length) % length;
    return (pattern & (1 << clampInt (phase, 0, maxCycleMaskBits - 1))) != 0;
}

[[nodiscard]] inline Cell cloneCell (const Cell& cell)
{
    Cell out;
    out.enabled = cell.enabled;
    out.velocity = clampInt (cell.velocity, 1, 127);

    const int probability = cell.probability;
    const int cycle = clampInt (cell.cycle, 1, 64);
    const int cycleMask = cell.cycleMask;
    const int roll = cell.roll;

    out.probability = clampInt (probability, 0, 100);
    out.cycle = cycle;
    out.cycleMask = normalizeCycleMask (cycleMask, cycle);
    out.roll = clampInt (roll, 1, Constants::maxRoll);
    out.source = cell.source;
    out.sourceStep = cell.sourceStep;
    return out;
}

struct Channel
{
    std::string label;
    int note = 36;
    int lock = -1;
    int loopStart = 0;
    int loopLength = Constants::maxSteps;
    PlaybackMode playbackMode = PlaybackMode::normal;
};

struct LoopRange
{
    int loopStart = 0;
    int loopLength = Constants::maxSteps;
};

[[nodiscard]] inline Channel defaultChannel (int index)
{
    Channel channel;
    channel.label = std::string { Constants::defaultChannelLabels[static_cast<size_t> (index)] };
    channel.note = Constants::defaultChannelNotes[static_cast<size_t> (index)];
    channel.lock = -1;
    channel.loopStart = 0;
    channel.loopLength = 16;
    channel.playbackMode = PlaybackMode::normal;
    return channel;
}

struct SourceSettings
{
    int stepCount = 16;
    std::string rate = std::string { Constants::defaultRate };
    std::array<LoopRange, Constants::maxChannels> loopRanges {};
};

[[nodiscard]] inline SourceSettings defaultSourceSettings()
{
    SourceSettings settings;
    for (auto& range : settings.loopRanges)
        range.loopLength = 16;
    return settings;
}

using SourcePattern = std::array<std::array<Cell, Constants::maxSteps>, Constants::maxChannels>;
using GeneratedPattern = SourcePattern;

[[nodiscard]] inline SourcePattern makeEmptySourcePattern()
{
    SourcePattern pattern {};

    for (auto& channel : pattern)
    {
        for (auto& cell : channel)
            cell = defaultCell();
    }

    return pattern;
}

} // namespace ksh
