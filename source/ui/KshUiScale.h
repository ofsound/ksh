#pragma once

#include <algorithm>

namespace ksh::ui
{
    struct UiScale
    {
        static constexpr int minimumPercent = 50;
        static constexpr int maximumPercent = 400;
        // 100% is the comfortable desktop size, relative to the design geometry.
        static constexpr int referencePercent = 85;
        static constexpr float baseline = static_cast<float> (referencePercent) / 100.0f;
        static constexpr float factor (int percent) { return static_cast<float> (percent) / 100.0f * baseline; }
        static constexpr float pluginDefaultFactor (float width, float height, float workWidth, float workHeight)
        {
            // Leave space for the host's window chrome and placement below its toolbar.
            constexpr float horizontalClearance = 32.0f, verticalClearance = 112.0f;
            if (workWidth <= horizontalClearance || workHeight <= verticalClearance)
                return baseline;
            return std::clamp (std::min ((workWidth - horizontalClearance) / width,
                                         (workHeight - verticalClearance) / height),
                               factor (minimumPercent), baseline);
        }
    };
}
