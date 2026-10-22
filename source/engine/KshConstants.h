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
    static constexpr int mutedStaticSource = -1;
    static constexpr int sourceCount = 16;
    static constexpr int maxRoll = 8;

    static constexpr int swingSubdivisionCount = 3;
    static constexpr int defaultSwingSubdivisionIndex = 1;

    static constexpr int defaultMidiChannel = 1;
    static constexpr int defaultNoteDurationMs = 100;

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

    static constexpr std::array<double, swingSubdivisionCount> swingSubdivisionValues {
        1.0, 2.0, 4.0
    };

    static constexpr std::string_view defaultRate = "16n";
    static constexpr std::string_view defaultGenerationMode = "static";
    static constexpr std::string_view defaultChannelPlaybackMode = "normal";

    [[nodiscard]] static double beatsForRate (std::string_view rate)
    {
        static constexpr std::array<double, 8> beats { 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0 };
        for (size_t i = 0; i < rates.size(); ++i)
            if (rates[i] == rate)
                return beats[i];
        return 0.25;
    }

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

        if (lower == "rp" || lower == "reverse_ping_pong" || lower == "reverse-ping-pong"
            || lower == "reverse_ping-pong" || lower == "rev_ping_pong" || lower == "rev-ping-pong")
            return "reverse_ping_pong";

        if (lower == "p" || lower == "ping" || lower == "ping-pong" || lower == "ping_pong"
            || lower == "b" || lower == "boom" || lower == "boomerang")
            return "ping_pong";

        return std::string { defaultChannelPlaybackMode };
    }
};

} // namespace ksh
