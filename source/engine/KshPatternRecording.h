#pragma once

#include "KshMath.h"
#include "KshNativePlayback.h"

#include <cmath>
#include <limits>

namespace ksh
{
/** Small, string-free recording geometry, also handed to the message thread for pad input. */
struct PatternRecordingGrid
{
    int stepCount = 16;
    double beatsPerStep = 0.25;
    int swing = 0;
    int swingSubdivisionIndex = Constants::defaultSwingSubdivisionIndex;
    std::array<LoopRange, Constants::maxChannels> loops {};
    std::array<PlaybackMode, Constants::maxChannels> modes {};
};

inline PatternRecordingGrid patternRecordingGrid (const PlaybackSnapshot& snapshot, int source)
{
    PatternRecordingGrid grid;
    const auto& settings = snapshot.sourceSettings[static_cast<size_t> (clampInt (source, 0, Constants::sourceCount - 1))];
    grid.stepCount = clampInt (settings.stepCount, 1, Constants::maxSteps);
    grid.beatsPerStep = Constants::beatsForRate (settings.rate);
    grid.swing = snapshot.swing;
    grid.swingSubdivisionIndex = snapshot.swingSubdivisionIndex;
    grid.loops = settings.loopRanges;
    for (size_t channel = 0; channel < grid.modes.size(); ++channel)
        grid.modes[channel] = snapshot.channels[channel].playbackMode;
    return grid;
}

struct PatternRecordingPosition
{
    int step = 0;
    int nudge = 0;
    double onsetPpq = 0.0;
};

/** Match an actual playback occurrence, retaining the residual timing when unlocked.
    The unwrapped occurrence is used for swing and nudge even across a pattern seam. */
inline PatternRecordingPosition patternRecordingPosition (const PatternRecordingGrid& grid,
                                                           int channel, double ppq, int strength)
{
    if (!std::isfinite (ppq) || grid.beatsPerStep <= 0.0)
        return {};
    const double position = ppq / grid.beatsPerStep;
    const auto nearest = static_cast<int64_t> (std::llround (position));
    auto occurrence = nearest;
    double residual = 0.0;
    if (strength < 100)
    {
        double bestDistance = std::numeric_limits<double>::max();
        // Swing can delay an occurrence by up to one step. Include both neighbours
        // and their swung neighbours rather than clipping a residual to the nudge range.
        for (int offset = -2; offset <= 2; ++offset)
        {
            const auto candidate = nearest + offset;
            const double onset = static_cast<double> (candidate)
                                 + swingDelayStepsForPosition (static_cast<double> (candidate), grid.swing, grid.swingSubdivisionIndex);
            const double difference = position - onset;
            if (std::abs (difference) < bestDistance)
            {
                bestDistance = std::abs (difference);
                occurrence = candidate;
                residual = difference;
            }
        }
    }

    const auto index = static_cast<size_t> (clampInt (channel, 0, Constants::maxChannels - 1));
    const auto& range = grid.loops[index];
    const int start = clampInt (range.loopStart, 0, grid.stepCount - 1);
    const int length = clampInt (range.loopLength, 1, grid.stepCount - start);
    const auto wrap = [] (int64_t value, int size)
    {
        return static_cast<int> ((value % size + size) % size);
    };
    int step = 0;
    const auto mode = grid.modes[index];
    if (mode == PlaybackMode::reverse)
        step = length - 1 - wrap (occurrence, length);
    else if (mode == PlaybackMode::ping_pong || mode == PlaybackMode::reverse_ping_pong)
    {
        const int active = wrap (occurrence + (mode == PlaybackMode::reverse_ping_pong ? length : 0), length * 2);
        step = active < length ? active : length * 2 - 1 - active;
    }
    else
        step = wrap (occurrence, length);

    const int nudge = clampInt (static_cast<int> (std::lround (residual * (100 - clampInt (strength, 0, 100)))), -100, 100);
    const double onset = static_cast<double> (occurrence)
                         + swingDelayStepsForPosition (static_cast<double> (occurrence), grid.swing, grid.swingSubdivisionIndex)
                         + static_cast<double> (nudge) / 100.0;
    return { start + step, nudge, onset * grid.beatsPerStep };
}
}
