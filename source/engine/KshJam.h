#pragma once

#include "KshTypes.h"
#include <optional>
#include <vector>

namespace ksh
{
struct JamHit
{
    double timestampMs = 0.0;
    int row = 0;
    int velocity = 100;
};

struct JamTake
{
    struct Swing
    {
        int amount = 0;
        int subdivision = Constants::defaultSwingSubdivisionIndex;
    };

    static constexpr size_t maxHits = 1024;
    static constexpr double maxDurationMs = 48000.0;
    double bpm = 120.0;
    int steps = 16;
    SourcePattern pattern {};
    int droppedHits = 0;
    std::optional<Swing> swing;
};

/** Hits use one monotonic clock. The finish press marks the next downbeat. */
[[nodiscard]] std::optional<JamTake> transcribeJam (std::vector<JamHit> hits, double endMs, double previousTempo,
                                                    int swing, int swingSubdivision, int quantizeStrength = 0);
}
