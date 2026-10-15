#pragma once
#include "KshEditorController.h"
namespace ksh::ui
{
    struct Layout
    {
        int channels = 8, steps = 16;
        float patternScale = 1.0f;
        bool standalone = false, randomVisible = false;
        float transportHeight() const { return standalone ? 44.0f : 0.0f; }
        float toolbarY() const { return transportHeight() + 86; }
        float gridY() const { return toolbarY() + 68; }
        float cellWidth() const { return 54.375f * patternScale * (steps > 16 ? 16.0f / static_cast<float> (steps) : 1.0f); }
        float cellHeight() const { return std::round (54.375f * patternScale); }
        float rowHeight() const { return cellHeight() + std::round (4.0f * patternScale); }
        float gridLeft() const { return 322.0f; }
        float gridWidth() const { return 870.0f * patternScale; }
        float width() const { return juce::jmax (1338.0f, 436.0f + gridWidth()); }
        float cellsY() const { return 58.0f; }
        float gridHeight() const { return cellsY() + static_cast<float> (channels) * rowHeight() + 18.0f; }
        float previewY() const { return gridY() + gridHeight(); }
        float height() const { return previewY() + 204; }
        juce::Rectangle<float> cell (Location p) const { return { gridLeft() + static_cast<float> (p.step) * cellWidth(), cellsY() + static_cast<float> (p.channel) * rowHeight(), cellWidth(), cellHeight() }; }
        Location at (juce::Point<float> point) const
        {
            const int ch = static_cast<int> (std::floor ((point.y - cellsY()) / rowHeight()));
            const int step = static_cast<int> (std::floor ((point.x - gridLeft()) / cellWidth()));
            return ch >= 0 && ch < channels && step >= 0 && step < steps && cell ({ ch, step }).contains (point) ? Location { ch, step } : Location {};
        }
    };
}
