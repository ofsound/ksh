#pragma once

#include "KshMath.h"
#include "KshTypes.h"
#include <algorithm>

namespace ksh
{
    inline double splitOffsetInSteps (const Cell& cell, int part, int step, int swing, int subdivision, double humanize = 0.0)
    {
        const double fraction = static_cast<double> (part) / cell.roll;
        const double delay = cell.span > 1 ? 0.0 : swingDelayStepsForPosition (step + fraction, swing, subdivision);
        return fraction * cell.span + delay + humanize + cell.nudge / 100.0;
    }

    inline int controllerGlideIntervals (double durationBeats, int from, int to)
    {
        // Keep short glides smooth while sending each 7-bit value at most once per ramp.
        constexpr double updatesPerQuarter = 192.0;
        return std::min (std::abs (to - from), static_cast<int> (std::ceil (durationBeats * updatesPerQuarter - 1.0e-9)));
    }

    inline int controllerGlideValue (int from, int to, double fraction)
    {
        return static_cast<int> (std::lround (from + (to - from) * fraction));
    }
}
