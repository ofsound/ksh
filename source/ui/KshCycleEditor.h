#pragma once
#include "KshEditorController.h"
#include "KshStyle.h"
#include "KshLayout.h"
namespace ksh::ui
{
    class CycleEditor : public juce::Component, private juce::Timer
    {
    public:
        explicit CycleEditor (EditorController& c) : controller (c)
        {
            setTitle ("Cycle pattern");
            handle.setMouseCursor (horizontalResizeCursor());
            handle.setTitle ("Cycle length");
            handle.setWantsKeyboardFocus (true);
            addAndMakeVisible (handle);
            for (int i = 0; i < 8; ++i)
            {
                auto& b = bits[static_cast<size_t> (i)];
                b.owner = this;
                b.index = i;
                addAndMakeVisible (b);
                b.setButtonText (juce::String (i + 1));
                b.setTooltip ("Drag active beats to move them. Double-click to reset the cycle.");
                b.onClick = [this, i] { toggleBeat (i, locations()); };
            }
        }
        ~CycleEditor() override { handle.cancel(); }
        std::function<std::vector<Location>()> locations = [] { return std::vector<Location>(); };
        bool inlineLayout = false;
        bool compact = false;
        bool previewCycleLayer = true;
        bool useAnchorPatternForGroup = false;
        bool resetOnDoubleClickAnywhere = false;
        int captionHeight = 0;
        void setBeatAccent (juce::Colour colour)
        {
            if (beatAccent == colour)
                return;
            beatAccent = colour;
            repaint();
            handle.repaint();
            for (auto& bit : bits)
                bit.repaint();
        }
        int preferredWidth (int height) const
        {
            const int side = juce::jmax (0, height - (inlineLayout ? 8 : 56));
            return 2 * beatPadding + 8 * side + 7 * beatGap + Layout::rowEndHandleSpace;
        }
        void cancelGesture()
        {
            handle.cancel();
            cancelBitDrag();
            cancelPendingClick();
        }
        void commitPendingBeatClick() { finishPendingClick (false); }
        void sync()
        {
            const auto list = locations();
            setEnabled (!list.empty());
            const auto c = list.empty() ? Cell {} : controller.cell (list.front());
            updatePlayback();
            const std::tuple shown { !list.empty(), c.cycle, c.cycleMask, compact };
            if (displayed == shown)
                return;
            displayed = shown;
            cycleLength = c.cycle;
            resized();
            for (int i = 0; i < 8; ++i)
            {
                auto& bit = bits[static_cast<size_t> (i)];
                bit.inCycle = i < c.cycle;
                bit.setToggleState (i < c.cycle && (c.cycleMask & (1 << i)) != 0, juce::dontSendNotification);
                bit.repaint();
            }
            repaint();
        }
        void updatePlayback()
        {
            const auto list = locations();
            const auto now = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 8; ++i)
            {
                const bool flashing = controller.playingStep > 0 && std::any_of (list.begin(), list.end(), [&] (Location p)
                {
                    return controller.sourceCycleFlashes[static_cast<size_t> (p.channel)][static_cast<size_t> (p.step)] > now
                        && controller.sourceCycleSteps[static_cast<size_t> (p.channel)][static_cast<size_t> (p.step)] == i + 1;
                });
                auto& bit = bits[static_cast<size_t> (i)];
                if (bit.flashing != flashing)
                {
                    bit.flashing = flashing;
                    bit.repaint();
                }
            }
        }
        void resized() override
        {
            const int top = captionHeight + (inlineLayout ? 4 : 10);
            const int availableHeight = juce::jmax (0, getHeight() - captionHeight - (inlineLayout ? 8 : 56));
            const int availableWidth = juce::jmax (0, getWidth() - 2 * beatPadding - 7 * beatGap);
            const int side = juce::jmin (juce::jmax (0, availableWidth - Layout::rowEndHandleSpace) / 8, availableHeight);
            const int length = juce::jlimit (1, 8, cycleLength);
            const int beatHeight = inlineLayout ? availableHeight : side;
            cellStride = side + beatGap;
            for (int i = 0; i < 8; ++i)
            {
                auto& beat = bits[static_cast<size_t> (i)];
                beat.fontHeight = inlineLayout ? (compact ? 13.0f : 18.0f) : 12.0f;
                beat.setBounds (beatPadding + i * cellStride, top, side, beatHeight);
            }
            const int lastBeatRight = bits[static_cast<size_t> (length - 1)].getRight();
            // Overlap the seam while leaving the centres of the neighbouring beats clickable.
            const int gripWidth = juce::jmin (Layout::rowHandleWidth, cellStride - 2);
            handle.setBounds (lastBeatRight + beatGap / 2 - gripWidth / 2, top, gripWidth, beatHeight);
            handle.toFront (false);
        }
        void paint (juce::Graphics& g) override
        {
            const auto first = bits.front().getBounds().toFloat();
            const auto last = bits[static_cast<size_t> (juce::jlimit (1, 8, cycleLength) - 1)].getBounds().toFloat();
            const auto range = first.getUnion (last).expanded (3, 3);
            const float alpha = isEnabled() ? 1.0f : 0.35f;
            g.setColour (accent().withAlpha (0.08f * alpha));
            g.fillRoundedRectangle (range, 6);
            g.setColour (accent().withAlpha (0.42f * alpha));
            g.drawRoundedRectangle (range, 6, 1);
            if (!inlineLayout)
                text (g, "CYCLE", { 120.0f, static_cast<float> (getHeight() - 30), static_cast<float> (getWidth() - 132), 20.0f }, 10, Palette::muted, true, juce::Justification::centredRight);
        }
        void mouseDoubleClick (const juce::MouseEvent& event) override
        {
            if (!resetOnDoubleClickAnywhere || !isEnabled() || !event.mods.isLeftButtonDown() || event.mods.isPopupMenu()
                || event.mouseWasDraggedSinceMouseDown() || !getLocalBounds().contains (event.getEventRelativeTo (this).getPosition())
                || controller.editing())
                return;
            if (pendingClick)
            {
                finishPendingClick (true);
                return;
            }
            controller.refresh();
            resetCycle (locations());
        }

    private:
        static constexpr int beatPadding = 10;
        static constexpr int beatGap = 4;
        EditorController& controller;
        std::optional<juce::Colour> beatAccent;
        juce::Colour accent() const { return beatAccent.value_or (Palette::accent); }
        int dragFrom = -1, dragTo = -1, dragSource = -1;
        bool bitDragging = false;
        uint64_t dragEpoch = 0;
        std::vector<Location> dragLocations;
        std::vector<Cell> dragCells;
        struct PendingClick
        {
            int index, source;
            uint64_t epoch;
            std::vector<Location> locations;
            std::vector<Cell> cells;
        };
        std::optional<PendingClick> pendingClick;
        void toggleBeat (int index, const std::vector<Location>& list)
        {
            if (list.empty())
                return;
            const auto first = controller.cell (list.front());
            const int nextLength = juce::jmax (first.cycle, index + 1);
            const int mask = index >= first.cycle ? first.cycleMask : first.cycleMask ^ (1 << index);
            beginEdit ("Cycle pattern");
            controller.setCycle (nextLength, mask, list);
            endEdit();
            sync();
        }
        void queueBeatClick (int index, const juce::MouseEvent& event)
        {
            pendingClick = PendingClick { index, dragSource, dragEpoch, dragLocations, dragCells };
            startTimer (juce::jmax (1, juce::MouseEvent::getDoubleClickTimeout() - event.getLengthOfMousePress()));
        }
        void cancelPendingClick()
        {
            stopTimer();
            pendingClick.reset();
        }
        void finishPendingClick (bool reset)
        {
            stopTimer();
            const auto click = std::exchange (pendingClick, std::nullopt);
            if (!click || !isEnabled() || controller.editing()
                || controller.processor.getUiBridge().getDocumentEpoch() != click->epoch)
                return;
            controller.refresh();
            if (controller.source() != click->source || locations() != click->locations)
                return;
            for (size_t i = 0; i < click->locations.size(); ++i)
                if (controller.cell (click->locations[i]) != click->cells[i])
                    return;
            if (!reset)
            {
                toggleBeat (click->index, click->locations);
                return;
            }
            resetCycle (click->locations);
        }
        void resetCycle (const std::vector<Location>& list)
        {
            if (list.empty())
                return;
            beginEdit ("Reset cycle");
            controller.setCycle (CellDefaults::cycle, CellDefaults::cycleMask, list);
            endEdit();
            sync();
        }
        void timerCallback() override { finishPendingClick (false); }
        void cancelBitDrag()
        {
            dragFrom = dragTo = -1;
            bitDragging = false;
            dragLocations.clear();
            dragCells.clear();
            for (auto& bit : bits)
                bit.repaint();
        }
        void startBitDrag (int index)
        {
            cancelBitDrag();
            const auto list = locations();
            if (!isEnabled() || list.empty())
                return;
            const auto first = controller.cell (list.front());
            const bool canMove = index < first.cycle && (first.cycleMask & (1 << index)) != 0;
            if (!canMove && !resetOnDoubleClickAnywhere)
                return;
            if (canMove)
                dragFrom = dragTo = index;
            dragSource = controller.source();
            dragEpoch = controller.state.documentEpoch;
            dragLocations = list;
            for (auto p : list)
                dragCells.push_back (controller.cell (p));
        }
        void updateBitDrag (const juce::MouseEvent& e)
        {
            if (dragFrom < 0 || e.getDistanceFromDragStart() < 4)
                return;
            bitDragging = true;
            const auto point = e.getEventRelativeTo (this).position;
            dragTo = -1;
            for (int i = 0; i < juce::jmin (8, cycleLength); ++i)
                if (bits[static_cast<size_t> (i)].getBounds().toFloat().contains (point))
                    dragTo = i;
            for (auto& bit : bits)
                bit.repaint();
        }
        void finishBitDrag()
        {
            bool valid = isEnabled() && dragFrom >= 0 && dragTo >= 0 && dragFrom != dragTo
                         && controller.source() == dragSource && controller.state.documentEpoch == dragEpoch && locations() == dragLocations;
            for (size_t i = 0; valid && i < dragLocations.size(); ++i)
                valid = controller.cell (dragLocations[i]) == dragCells[i];
            if (valid)
            {
                beginEdit ("Move cycle beat");
                if (useAnchorPatternForGroup)
                {
                    const auto& cell = dragCells.front();
                    controller.setCycle (cell.cycle, (cell.cycleMask & ~(1 << dragFrom)) | (1 << dragTo), dragLocations);
                }
                else for (size_t i = 0; i < dragLocations.size(); ++i)
                {
                    const auto& cell = dragCells[i];
                    if (dragTo < cell.cycle && (cell.cycleMask & (1 << dragFrom)) != 0)
                        controller.setCycle (cell.cycle, (cell.cycleMask & ~(1 << dragFrom)) | (1 << dragTo), { dragLocations[i] });
                }
                endEdit();
            }
            cancelBitDrag();
            sync();
        }
        std::optional<Layer> previousLayer;
        void beginEdit (const juce::String& label)
        {
            if (previewCycleLayer)
            {
                previousLayer = controller.layer;
                controller.layer = Layer::cycle;
            }
            controller.begin (label);
            controller.notify();
        }
        void restoreLayer()
        {
            if (previousLayer.has_value())
            {
                controller.layer = *previousLayer;
                previousLayer.reset();
            }
        }
        void endEdit()
        {
            restoreLayer();
            controller.commit();
            controller.notify();
        }
        int cycleLength = 1;
        int cellStride = 4;
        std::optional<std::tuple<bool, int, int, bool>> displayed;
        class LengthHandle : public juce::Component, public juce::SettableTooltipClient
        {
        public:
            explicit LengthHandle (CycleEditor& editor) : owner (editor)
            {
                setTooltip ("Drag or use arrow keys to set cycle length");
            }
            void enablementChanged() override
            {
                const bool active = isEnabled();
                setMouseCursor (active ? horizontalResizeCursor() : juce::MouseCursor (juce::MouseCursor::NormalCursor));
                setWantsKeyboardFocus (active);
                setInterceptsMouseClicks (active, false);
                if (!active)
                    cancel();
                repaint();
            }
            void paint (juce::Graphics& g) override
            {
                paintLengthGrip (g, getLocalBounds().toFloat().reduced (0, 2),
                                 isEnabled() && (dragging || isMouseOver() || hasKeyboardFocus (false)) ? owner.accent() : Palette::text, isEnabled() ? 1.0f : 0.35f);
            }
            void focusGained (FocusChangeType) override { repaint(); }
            void focusLost (FocusChangeType) override { repaint(); }
            void mouseEnter (const juce::MouseEvent&) override { repaint(); }
            void mouseExit (const juce::MouseEvent&) override { repaint(); }
            void mouseDown (const juce::MouseEvent& e) override
            {
                if (!isEnabled() || !e.mods.isLeftButtonDown())
                    return;
                owner.finishPendingClick (false);
                targets = owner.locations();
                if (targets.empty())
                    return;
                if (isShowing())
                    grabKeyboardFocus();
                dragStartX = e.getEventRelativeTo (&owner).position.x;
                dragStartLength = owner.cycleLength;
                dragging = true;
                repaint();
                owner.beginEdit ("Cycle length");
            }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (!dragging)
                    return;
                if (!isEnabled() || owner.locations().empty())
                {
                    cancel();
                    return;
                }
                const float x = e.getEventRelativeTo (&owner).position.x;
                const float width = static_cast<float> (owner.cellStride);
                apply (juce::jlimit (1, 8, dragStartLength + juce::roundToInt ((x - dragStartX) / width)));
            }
            void mouseUp (const juce::MouseEvent&) override
            {
                if (!isEnabled() || owner.locations().empty())
                {
                    cancel();
                    return;
                }
                if (dragging)
                {
                    dragging = false;
                    repaint();
                    owner.endEdit();
                }
            }
            void mouseDoubleClick (const juce::MouseEvent& event) override { owner.mouseDoubleClick (event); }
            bool keyPressed (const juce::KeyPress& key) override
            {
                if (!isEnabled())
                    return false;
                if (key == juce::KeyPress::escapeKey && dragging)
                {
                    cancel();
                    return true;
                }
                const int delta = key == juce::KeyPress::leftKey ? -1 : key == juce::KeyPress::rightKey ? 1 : 0;
                if (delta == 0 || dragging)
                    return false;
                owner.finishPendingClick (false);
                targets = owner.locations();
                if (targets.empty())
                    return false;
                owner.beginEdit ("Cycle length");
                apply (juce::jlimit (1, 8, owner.cycleLength + delta));
                owner.endEdit();
                return true;
            }
        private:
            void apply (int n)
            {
                if (owner.useAnchorPatternForGroup)
                {
                    const auto cell = owner.controller.cell (targets.front());
                    if (cell.cycle != n)
                        owner.controller.setCycle (n, cell.cycleMask, targets);
                }
                else for (auto target : targets)
                {
                    const auto cell = owner.controller.cell (target);
                    if (cell.cycle != n)
                        owner.controller.setCycle (n, cell.cycleMask, { target });
                }
                owner.sync();
            }
        public:
            void cancel()
            {
                if (dragging)
                {
                    dragging = false;
                    repaint();
                    owner.restoreLayer();
                    owner.controller.cancel();
                }
            }
        private:
            CycleEditor& owner;
            float dragStartX = 0.0f;
            int dragStartLength = 1;
            bool dragging = false;
            std::vector<Location> targets;
        } handle { *this };
        class CycleBit : public juce::TextButton
        {
        public:
            CycleEditor* owner = nullptr;
            int index = 0;
            bool inCycle = false, flashing = false;
            float fontHeight = 14;
            void mouseDown (const juce::MouseEvent& e) override
            {
                suppressClick = false;
                armedMove = false;
                juce::TextButton::mouseDown (e);
                if (isEnabled() && e.mods.isLeftButtonDown())
                {
                    if (owner->pendingClick && owner->pendingClick->index == index && e.getNumberOfClicks() >= 2)
                        owner->stopTimer();
                    else
                        owner->finishPendingClick (false);
                    owner->startBitDrag (index);
                    armedMove = owner->dragFrom >= 0;
                }
            }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                juce::TextButton::mouseDrag (e);
                owner->updateBitDrag (e);
                suppressClick = suppressClick || owner->bitDragging;
            }
            void mouseExit (const juce::MouseEvent& e) override
            {
                juce::TextButton::mouseExit (e);
                if (e.source.canHover() && !e.mods.isAnyMouseButtonDown()
                    && owner->pendingClick && owner->pendingClick->index == index)
                    owner->commitPendingBeatClick();
            }
            void mouseUp (const juce::MouseEvent& e) override
            {
                if (suppressClick || (armedMove && owner->dragFrom < 0))
                {
                    owner->cancelPendingClick();
                    if (owner->bitDragging)
                        owner->updateBitDrag (e);
                    setState (juce::Button::buttonNormal);
                    owner->finishBitDrag();
                }
                else
                {
                    const bool clicked = isDown() && e.mods.isLeftButtonDown() && !e.mods.isPopupMenu()
                        && !e.mouseWasDraggedSinceMouseDown() && getLocalBounds().contains (e.getPosition());
                    if (clicked && owner->resetOnDoubleClickAnywhere && e.getNumberOfClicks() >= 2)
                    {
                        // JUCE delivers mouseDoubleClick after this release.
                        setState (juce::Button::buttonNormal);
                    }
                    else if (owner->pendingClick && owner->pendingClick->index == index)
                    {
                        setState (juce::Button::buttonNormal);
                        owner->finishPendingClick (clicked && e.getNumberOfClicks() >= 2);
                    }
                    else if (clicked && (armedMove || (!owner->dragLocations.empty() && owner->resetOnDoubleClickAnywhere))
                             && e.getNumberOfClicks() == 1)
                    {
                        // Let a double-click replace this toggle with one undoable reset.
                        setState (juce::Button::buttonNormal);
                        owner->queueBeatClick (index, e);
                    }
                    owner->cancelBitDrag();
                    juce::TextButton::mouseUp (e);
                }
            }
            void mouseDoubleClick (const juce::MouseEvent& event) override { owner->mouseDoubleClick (event); }
            bool keyPressed (const juce::KeyPress& key) override
            {
                if (key == juce::KeyPress::escapeKey && (owner->dragFrom >= 0 || owner->pendingClick))
                {
                    suppressClick = true;
                    owner->cancelPendingClick();
                    owner->cancelBitDrag();
                    setState (juce::Button::buttonNormal);
                    return true;
                }
                return juce::TextButton::keyPressed (key);
            }
        private:
            bool suppressClick = false, armedMove = false;
        public:
            void paintButton (juce::Graphics& g, bool hover, bool down) override
            {
                const auto r = getLocalBounds().toFloat().reduced (0.5f);
                const bool preview = owner != nullptr && owner->bitDragging && owner->dragTo >= 0;
                const bool on = preview && index == owner->dragTo ? true : preview && index == owner->dragFrom ? false : getToggleState();
                auto background = on ? owner->accent() : inCycle ? Palette::controls.surface : Palette::controls.app;
                if (flashing)
                    background = Palette::text;
                if (isEnabled() && (hover || down))
                    background = background.brighter (0.12f);
                g.setColour (background.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
                g.fillRoundedRectangle (r, 3);
                ksh::ui::text (g, getButtonText(), r, juce::jmin (fontHeight, r.getHeight() * 0.45f),
                      flashing || on ? Palette::controls.app : inCycle ? Palette::muted : Palette::faint, true);
            }
        };
        std::array<CycleBit, 8> bits;
    };
}
