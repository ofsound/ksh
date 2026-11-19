#pragma once

#include "KshConstants.h"

#include <array>
#include <bit>
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

class SplitGate
{
public:
    static constexpr int maxCycle = 8;

    [[nodiscard]] int probability() const { return settings & 0x7f; }
    [[nodiscard]] int cycle() const { return std::bit_width (static_cast<unsigned int> (cycleDefinition())) - 1; }
    [[nodiscard]] int cycleMask() const { return cycleDefinition() ^ (1 << cycle()); }
    [[nodiscard]] uint16_t cycleDefinition() const { return settings >> 7; }

    void setProbability (int value)
    {
        settings = static_cast<uint16_t> ((settings & ~0x7f) | clampInt (value, 0, 100));
    }
    void setCycle (int length, int mask)
    {
        const int lengthBit = 1 << clampInt (length, 1, maxCycle);
        settings = static_cast<uint16_t> (((lengthBit | (std::max (0, mask) & (lengthBit - 1))) << 7) | probability());
    }

    [[nodiscard]] bool operator== (const SplitGate&) const = default;

private:
    // Seven bits of probability, then the active beats with a length bit above them.
    uint16_t settings = 100 | (3 << 7);
};

struct Cell
{
    bool enabled = false;
    int velocity = 100;
    int probability = 100;
    int cycle = 1;
    int cycleMask = 1;
    int roll = 1;
    int nudge = 0; // Signed percent of one step; negative plays early.
    int source = -1;
    int sourceStep = 0;
    int span = 1; // Grid steps occupied by this roll.
    std::array<SplitGate, Constants::maxRoll> splitGates {};
    std::array<uint8_t, Constants::maxRoll> splitVelocities {}; // Zero inherits the step velocity.

    [[nodiscard]] int splitVelocity (int part) const
    {
        const auto value = splitVelocities[static_cast<size_t> (part)];
        return value == 0 ? velocity : value;
    }
    void setSplitVelocity (int part, int value)
    {
        splitVelocities[static_cast<size_t> (part)] = static_cast<uint8_t> (clampInt (value, 0, 127));
    }

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
    static constexpr int nudge = 0;
    static constexpr int source = -1;
    static constexpr int span = 1;
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
    out.nudge = clampInt (cell.nudge, -100, 100);
    out.source = cell.source;
    out.sourceStep = cell.sourceStep;
    out.span = clampInt (cell.span, 1, Constants::maxSteps);
    out.splitGates = cell.splitGates;
    for (int i = 0; i < Constants::maxRoll; ++i)
        out.setSplitVelocity (i, cell.splitVelocities[static_cast<size_t> (i)]);
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

    [[nodiscard]] bool operator== (const Channel&) const = default;
};

struct LoopRange
{
    int loopStart = 0;
    int loopLength = Constants::maxSteps;

    [[nodiscard]] bool operator== (const LoopRange&) const = default;
};

// A span owns empty steps on its right. If a loop edit or another hit takes
// those steps, retain the saved span and use an ordinary roll until it fits.
[[nodiscard]] inline bool rollSpanFits (const std::array<Cell, Constants::maxSteps>& row,
                                       LoopRange loop, int step, int span)
{
    if (step < loop.loopStart || step + span > loop.loopStart + loop.loopLength)
        return false;
    for (int next = step + 1; next < step + span; ++next)
        if (row[static_cast<size_t> (next)].enabled)
            return false;
    return true;
}

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
    std::array<PlaybackMode, Constants::maxChannels> playbackModes {};

    [[nodiscard]] bool operator== (const SourceSettings&) const = default;
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
