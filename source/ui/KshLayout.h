#pragma once
#include "KshEditorController.h"
namespace ksh::ui
{
    struct Layout
    {
        static constexpr int outerGutter = 12;
        static constexpr int footerHeight = 56;
        int channels = 8, steps = 16;
        bool standalone = false, randomVisible = false;
        bool compact = false;
        float compactWidth = 814.0f, compactHeight = 409.0f;
        int firstStep = 0;
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
        bool operator== (const Layout&) const = default;
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
        int visibleSteps() const { return compact ? juce::jmin (16, steps - firstStep) : steps; }
        bool stepVisible (int step) const { return step >= firstStep && step < firstStep + visibleSteps(); }
        float transportHeight() const { return standalone ? 44.0f : 0.0f; }
        float toolbarY() const { return compact ? 0.0f : transportHeight() + 86; }
        float gridY() const { return compact ? 46.0f : toolbarY() + 68; }
        float cellWidth() const { return compact ? gridWidth() / 16.0f : 54.375f * (steps > 16 ? 16.0f / static_cast<float> (steps) : 1.0f); }
        float cellHeight() const { return compact ? rowHeight() - 3.0f : 54.0f; }
        float rowHeight() const { return compact ? (compactHeight - gridY() - cellsY() - 84.0f) / 8.0f : cellHeight() + 4.0f; }
        float gridLeft() const { return compact ? 132.0f : 322.0f; }
        float gridWidth() const { return compact ? compactWidth - gridLeft() - 6.0f : 870.0f; }
        float width() const { return compact ? compactWidth : juce::jmax (1338.0f, 436.0f + gridWidth()); }
        float cellsY() const { return compact ? 18.0f : 58.0f; }
        float gridHeight() const { return compact ? compactHeight - gridY() - 84.0f : cellsY() + static_cast<float> (channels) * rowHeight() + 18.0f; }
        float previewY() const { return gridY() + gridHeight(); }
        float height() const { return compact ? compactHeight : previewY() + 164 + footerHeight; }
        juce::Rectangle<float> cell (Location p) const { return { gridLeft() + static_cast<float> (p.step - firstStep) * cellWidth(), cellsY() + static_cast<float> (p.channel) * rowHeight(), cellWidth(), cellHeight() }; }
        Location at (juce::Point<float> point) const
        {
            const int ch = static_cast<int> (std::floor ((point.y - cellsY()) / rowHeight()));
            const int step = firstStep + static_cast<int> (std::floor ((point.x - gridLeft()) / cellWidth()));
            return ch >= 0 && ch < channels && stepVisible (step) && cell ({ ch, step }).contains (point) ? Location { ch, step } : Location {};
        }
    };
}
