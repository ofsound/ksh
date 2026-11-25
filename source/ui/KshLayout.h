#pragma once
#include "KshEditorController.h"
namespace ksh::ui
{
    struct Layout
    {
        static constexpr int outerGutter = 12;
        static constexpr int rowClearWidth = 42;
        static constexpr int rowHandleWidth = 28;
        static constexpr int rowHandleOffset = 8;
        static constexpr int rowEndHandleSpace = rowHandleWidth / 2 + rowHandleOffset;
        static constexpr int loopGripWidth = 24;
        static constexpr int rowVelocityGutter = 7;
        static constexpr int loopEdgeSpace = loopGripWidth + rowVelocityGutter;
        static constexpr int previewHeight = 174;
        // 64 design units remain >= 44 points at a 1024 x 744 iPad viewport.
        static constexpr int touchTarget = 64;
        static constexpr int footerPadding = 2;
        static constexpr int footerGap = 0;
        static constexpr int footerHeight = touchTarget + 2 * footerPadding;
        static constexpr int headerPadding = 10;
        static constexpr int headerRowGap = 4;
        static constexpr int headerHeight = 2 * touchTarget + headerRowGap + 2 * headerPadding;
        static constexpr int toolbarPadding = 8;
        static constexpr int toolbarCaptionSpace = 24;
        static constexpr int fullWidth = 1400;
        static constexpr int fullHeight = fullWidth * 3 / 4;
        static constexpr float cellInsetY = 0.8f;
        static constexpr float fullCellInsetX = 1.0f;
        static constexpr int compactHeaderHeight = 34;
        static constexpr int compactInspectorHeight = 48;
        static constexpr int compactStepsPerPage = 16;
        int channels = 8, steps = 16;
        bool standalone = false, randomVisible = false;
        bool compact = false;
        bool touchRows = false;
        bool showSwing = true;
        bool reverseRows = false;
        float compactWidth = 814.0f, compactHeight = 409.0f;
        int firstStep = 0;
        bool phoneRowControls = false, compactOnPhone = false;
        int compactHeaderTopInset = 0;
        bool settingsInHeader = false;
        int compactVisibleRows = 8, firstRow = 0;
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
        bool operator== (const Layout&) const = default;
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
        int visibleSteps() const { return compact ? juce::jmin (compactStepsPerPage, steps - firstStep) : steps; }
        bool stepVisible (int step) const { return step >= firstStep && step < firstStep + visibleSteps(); }
        float toolbarY() const { return compact ? 0.0f : static_cast<float> (headerHeight); }
        int compactHeaderBottom() const { return compactHeaderHeight + compactHeaderTopInset; }
        float gridY() const { return compact ? static_cast<float> (compactHeaderBottom()) : toolbarY() + 2 * toolbarPadding + toolbarCaptionSpace + touchTarget; }
        float cellWidth() const { return gridWidth() / static_cast<float> (compact ? compactStepsPerPage : juce::jmax (16, steps)); }
        float cellInsetX() const { return compact ? cellInsetY : fullCellInsetX; }
        juce::Rectangle<float> cellArtwork (juce::Rectangle<float> bounds) const
        {
            const auto face = bounds.reduced (cellInsetX(), cellInsetY);
            // Keep the gutter consistent for long rolls and triplets.
            constexpr float inset = 0.025f;
            const float stepFaceWidth = cellWidth() - 2.0f * cellInsetX();
            return face.reduced (stepFaceWidth * inset, face.getHeight() * inset);
        }
        float cellHeight() const { return rowHeight() - (compact ? 3.0f : 0.0f); }
        float rowHeight() const
        {
            if (compact)
                return (compactHeight - gridY() - cellsY() - compactInspectorHeight) / static_cast<float> (compactVisibleRows);
            return gridWidth() / 16.0f - 2.0f * (fullCellInsetX - cellInsetY);
        }
        // Make room for the right gutter while keeping the full grid's cell width and row height.
        float gridLeft() const { return compact ? 104.0f : 288.0f - (loopEdgeSpace - rowEndHandleSpace); }
        float gridWidth() const { return width() - gridLeft() - (compact ? 34.0f : static_cast<float> (rowClearWidth + loopEdgeSpace)); }
        float width() const { return compact ? compactWidth : static_cast<float> (fullWidth); }
        float cellsY() const { return compact ? 18.0f : 30.0f; }
        float gridHeight() const
        {
            if (compact)
                return compactHeight - gridY() - compactInspectorHeight;
            return fullHeight - gridY() - previewHeight - footerHeight - static_cast<float> (Constants::maxChannels - channels) * rowHeight();
        }
        float previewY() const { return gridY() + gridHeight(); }
        float height() const { return compact ? compactHeight : previewY() + previewHeight + (settingsInHeader ? 0 : footerHeight); }
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
        juce::Rectangle<float> cell (Location p) const
        {
            const float y = cellsY() + static_cast<float> (displayRow (p.channel) - (compact ? firstRow : 0)) * rowHeight();
            return { gridLeft() + static_cast<float> (p.step - firstStep) * cellWidth(), y, cellWidth(), cellHeight() };
        }
        Location at (juce::Point<float> point) const
        {
            const int ch = channelAtY (point.y);
            const int step = firstStep + static_cast<int> (std::floor ((point.x - gridLeft()) / cellWidth()));
            return ch >= 0 && ch < channels && stepVisible (step) && cell ({ ch, step }).contains (point) ? Location { ch, step } : Location {};
        }
    };
}
