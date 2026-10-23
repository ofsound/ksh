#pragma once
#include "KshEditorController.h"
namespace ksh::ui
{
    struct Layout
    {
        static constexpr int outerGutter = 12;
        static constexpr int footerHeight = 56;
        static constexpr int previewHeight = 134;
        static constexpr int compactHeaderHeight = 34;
        static constexpr int compactInspectorHeight = 48;
        int channels = 8, steps = 16;
        bool standalone = false, randomVisible = false;
        bool compact = false;
        bool touchRows = false;
        bool reverseRows = false;
        float compactWidth = 814.0f, compactHeight = 409.0f;
        int firstStep = 0;
        bool phoneRowControls = false;
        int compactVisibleRows = 8, firstRow = 0;
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
        bool operator== (const Layout&) const = default;
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
        int visibleSteps() const { return compact ? juce::jmin (16, steps - firstStep) : steps; }
        bool stepVisible (int step) const { return step >= firstStep && step < firstStep + visibleSteps(); }
        bool hasRowTriggers() const { return compact || touchRows; }
        float transportHeight() const { return standalone ? 44.0f : 0.0f; }
        float toolbarY() const { return compact ? 0.0f : transportHeight() + 116; }
        float gridY() const { return compact ? static_cast<float> (compactHeaderHeight) : toolbarY() + 80; }
        float cellWidth() const { return gridWidth() / static_cast<float> (compact ? 16 : juce::jmax (16, steps)); }
        float cellHeight() const { return compact ? rowHeight() - 3.0f : 54.0f; }
        float rowHeight() const { return compact ? (compactHeight - gridY() - cellsY() - compactInspectorHeight) / static_cast<float> (compactVisibleRows) : cellHeight() + 6.0f; }
        float gridLeft() const { return compact ? 132.0f : 346.0f; }
        float gridWidth() const { return compact ? compactWidth - gridLeft() - 6.0f : 902.0f; }
        float width() const { return compact ? compactWidth : juce::jmax (1370.0f, 436.0f + gridWidth()); }
        float cellsY() const { return compact ? (phoneRowControls ? 34.0f : 18.0f) : 46.0f; }
        float gridHeight() const { return compact ? compactHeight - gridY() - compactInspectorHeight : cellsY() + static_cast<float> (channels) * rowHeight() + 18.0f; }
        float previewY() const { return gridY() + gridHeight(); }
        float height() const { return compact ? compactHeight : previewY() + previewHeight + footerHeight; }
        int displayRow (int channel) const { return reverseRows ? channels - 1 - channel : channel; }
        int channelForRow (int row) const { return reverseRows ? channels - 1 - row : row; }
        bool channelVisible (int channel) const
        {
            const int row = displayRow (channel);
            return channel >= 0 && channel < channels && (!compact || (row >= firstRow && row < firstRow + compactVisibleRows));
        }
        int channelAtY (float y) const
        {
            const int row = static_cast<int> (std::floor ((y - cellsY()) / rowHeight()));
            return row >= 0 && row < (compact ? juce::jmin (channels - firstRow, compactVisibleRows) : channels) ? channelForRow (row + (compact ? firstRow : 0)) : -1;
        }
        juce::Rectangle<float> cell (Location p) const { return { gridLeft() + static_cast<float> (p.step - firstStep) * cellWidth(), cellsY() + static_cast<float> (displayRow (p.channel) - (compact ? firstRow : 0)) * rowHeight(), cellWidth(), cellHeight() }; }
        Location at (juce::Point<float> point) const
        {
            const int ch = channelAtY (point.y);
            const int step = firstStep + static_cast<int> (std::floor ((point.x - gridLeft()) / cellWidth()));
            return ch >= 0 && ch < channels && stepVisible (step) && cell ({ ch, step }).contains (point) ? Location { ch, step } : Location {};
        }
    };
}
