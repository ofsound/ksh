#pragma once

#include "KshConstants.h"

#include <cmath>

namespace ksh
{

[[nodiscard]] inline int clampSwingSubdivisionIndex (int subdivisionIndex)
{
    return clampInt (subdivisionIndex, 0, Constants::swingSubdivisionCount - 1);
}

[[nodiscard]] inline double swingSubdivisionValueForIndex (int subdivisionIndex)
{
    return Constants::swingSubdivisionValues[static_cast<size_t> (clampSwingSubdivisionIndex (subdivisionIndex))];
}

/** Swing delay in step units using midi-phrases' absolute-phase subdivision algorithm. */
[[nodiscard]] inline double swingDelayStepsForPosition (double stepPosition,
                                                        int swingPercent,
                                                        int subdivisionIndex)
{
    const auto swing = clampInt (swingPercent, 0, 100);

    if (swing <= 0)
        return 0.0;

    const auto subdivision = 2.0 * swingSubdivisionValueForIndex (subdivisionIndex);

    if (subdivision <= 0.0)
        return 0.0;

    constexpr auto epsilon = 1.0e-9;
    const auto subdivisionNumber = static_cast<int> (std::floor ((stepPosition + epsilon) / subdivision));

    return subdivisionNumber % 2 != 0
               ? subdivision * 0.5 * (static_cast<double> (swing) / 100.0)
               : 0.0;
}

} // namespace ksh
