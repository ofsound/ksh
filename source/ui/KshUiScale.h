#pragma once

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
    };
}
