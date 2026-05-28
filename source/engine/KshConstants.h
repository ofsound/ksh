#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ksh
{

inline int clampInt (int value, int min, int max)
{
    return std::clamp (value, min, max);
}

struct Constants
{
    static constexpr bool debug = false;

    static constexpr int maxSteps = 32;
    static constexpr int maxChannels = 8;
    static constexpr int defaultChannelCount = 8;
    static constexpr int sourceCount = 4;
    static constexpr int maxRoll = 8;

    static constexpr int defaultMidiChannel = 1;
    static constexpr int defaultNoteDurationMs = 100;

    static constexpr int nativeHitFieldCount = 9;

    static constexpr int phaseEarlyMsMin = -80;
    static constexpr int phaseEarlyMsMax = 80;

    static constexpr std::array<std::string_view, maxChannels> defaultChannelLabels {
        "1", "2", "3", "4", "5", "6", "7", "8"
    };

    static constexpr std::array<int, maxChannels> defaultChannelNotes {
        36, 37, 38, 39, 40, 41, 42, 43
    };

    static constexpr std::array<std::string_view, 8> rates {
        "4n", "4nt", "8n", "8nt", "16n", "16nt", "32n", "32nt"
    };

    static constexpr std::string_view defaultRate = "16n";
    static constexpr std::string_view defaultGenerationMode = "static";
    static constexpr std::string_view defaultChannelPlaybackMode = "normal";

    [[nodiscard]] static std::string normalizeRate (std::string_view rate)
    {
        for (const auto candidate : rates)
        {
            if (candidate == rate)
                return std::string { candidate };
        }

        return std::string { defaultRate };
    }

    [[nodiscard]] static std::string normalizeChannelPlaybackMode (std::string_view mode)
    {
        const std::string lower (mode);

        if (lower == "r" || lower == "rev" || lower == "reverse")
            return "reverse";

        if (lower == "b" || lower == "boom" || lower == "boomerang")
            return "boomerang";

        return std::string { defaultChannelPlaybackMode };
    }
};

} // namespace ksh
