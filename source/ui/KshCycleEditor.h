#pragma once
#include "KshEditorController.h"
#include "KshStyle.h"
namespace ksh::ui
{
    class CycleEditor : public juce::Component
    {
    public:
        explicit CycleEditor (EditorController& c) : controller (c)
        {
            setTitle ("Cycle pattern");
            handle.setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
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
                b.setTooltip ("Toggle cycle beat; drag an active beat to move it on release; click beyond length to extend");
                b.onClick = [this, i] {
                    const auto list = locations();
                    if (list.empty())
                        return;
                    const auto first = controller.cell (list.front());
                    const int nextLength = juce::jmax (first.cycle, i + 1);
                    const int mask = i >= first.cycle ? first.cycleMask : first.cycleMask ^ (1 << i);
                    beginEdit ("Cycle pattern");
                    controller.setCycle (nextLength, mask, list);
                    endEdit();
                    sync();
                };
            }
        }
        ~CycleEditor() override { handle.cancel(); }
        std::function<std::vector<Location>()> locations = [] { return std::vector<Location>(); };
        bool inlineLayout = false;
        bool compact = false;
        int preferredWidth (int height) const
        {
            const int side = juce::jmax (0, height - (inlineLayout ? 8 : 56));
            return 20 + 8 * side + 7 * 4;
        }
        void cancelGesture()
        {
            handle.cancel();
            cancelBitDrag();
        }
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
            const int top = inlineLayout ? 4 : 10;
            const int availableHeight = juce::jmax (0, getHeight() - (inlineLayout ? 8 : 56));
            const int side = juce::jmax (0, juce::jmin ((getWidth() - 20 - 7 * 4) / 8, availableHeight));
            cellStride = side + 4;
            for (int i = 0; i < 8; ++i)
            {
                bits[static_cast<size_t> (i)].fontHeight = inlineLayout ? 13.0f : 12.0f;
                bits[static_cast<size_t> (i)].setBounds (10 + i * cellStride, top, side, compact ? availableHeight : side);
            }
            const int edge = 10 + juce::jlimit (1, 8, cycleLength) * cellStride - 4;
            handle.setBounds (edge - 4, top, 9, side);
            handle.toFront (false);
            handle.setVisible (!compact);
        }
        void paint (juce::Graphics& g) override
        {
            const auto first = bits.front().getBounds().toFloat();
            const auto last = bits[static_cast<size_t> (juce::jlimit (1, 8, cycleLength) - 1)].getBounds().toFloat();
            const auto range = first.getUnion (last).expanded (3, 3);
            const float alpha = isEnabled() ? 1.0f : 0.35f;
            g.setColour (Palette::accent.withAlpha (0.08f * alpha));
            g.fillRoundedRectangle (range, 6);
            g.setColour (Palette::accent.withAlpha (0.42f * alpha));
            g.drawRoundedRectangle (range, 6, 1);
            if (!inlineLayout)
                text (g, "CYCLE", { 120.0f, static_cast<float> (getHeight() - 30), static_cast<float> (getWidth() - 132), 20.0f }, 10, Palette::muted, true, juce::Justification::centredRight);
        }

    private:
        EditorController& controller;
        int dragFrom = -1, dragTo = -1, dragSource = -1;
        bool bitDragging = false;
        uint64_t dragEpoch = 0;
        std::vector<Location> dragLocations;
        std::vector<Cell> dragCells;
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
            if (index >= first.cycle || (first.cycleMask & (1 << index)) == 0)
                return;
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
                for (size_t i = 0; i < dragLocations.size(); ++i)
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
            previousLayer = controller.layer;
            controller.layer = Layer::cycle;
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
                setTooltip ("Drag horizontally to set cycle length (1-8); arrow keys adjust");
            }
            void enablementChanged() override
            {
                const bool active = isEnabled();
                setMouseCursor (active ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
                setWantsKeyboardFocus (active);
                setInterceptsMouseClicks (active, false);
                if (!active)
                    cancel();
                repaint();
            }
            void paint (juce::Graphics& g) override
            {
                const auto centre = getLocalBounds().toFloat().getCentre();
                g.setColour ((dragging || isMouseOver() ? Palette::accent : Palette::text).withMultipliedAlpha (isEnabled() ? 1.0f : 0.35f));
                for (int dot = -2; dot <= 2; ++dot)
                    g.fillEllipse (centre.x - 0.6f, centre.y + static_cast<float> (dot) * 4, 1.2f, 1.2f);
            }
            void mouseEnter (const juce::MouseEvent&) override { repaint(); }
            void mouseExit (const juce::MouseEvent&) override { repaint(); }
            void mouseDown (const juce::MouseEvent& e) override
            {
                if (!isEnabled() || !e.mods.isLeftButtonDown())
                    return;
                targets = owner.locations();
                if (targets.empty())
                    return;
                grabKeyboardFocus();
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
                apply (juce::jlimit (1, 8, static_cast<int> (std::ceil ((x - 8) / width))));
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
                if (n == owner.cycleLength)
                    return;
                const auto c = owner.controller.cell (targets.front());
                owner.controller.setCycle (n, c.cycleMask, targets);
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
            void mouseUp (const juce::MouseEvent& e) override
            {
                if (suppressClick || (armedMove && owner->dragFrom < 0))
                {
                    if (owner->bitDragging)
                        owner->updateBitDrag (e);
                    setState (juce::Button::buttonNormal);
                    owner->finishBitDrag();
                }
                else
                {
                    owner->cancelBitDrag();
                    juce::TextButton::mouseUp (e);
                }
            }
            bool keyPressed (const juce::KeyPress& key) override
            {
                if (key == juce::KeyPress::escapeKey && owner->dragFrom >= 0)
                {
                    suppressClick = true;
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
                auto background = on ? Palette::accent : inCycle ? Palette::surface : Palette::app;
                if (flashing)
                    background = Palette::text;
                if (isEnabled() && (hover || down))
                    background = background.brighter (0.12f);
                g.setColour (background.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
                g.fillRoundedRectangle (r, 3);
                ksh::ui::text (g, getButtonText(), r, juce::jmin (fontHeight, r.getHeight() * 0.45f),
                      flashing || on ? Palette::app : inCycle ? Palette::muted : Palette::faint, true);
            }
        };
        std::array<CycleBit, 8> bits;
    };
}
