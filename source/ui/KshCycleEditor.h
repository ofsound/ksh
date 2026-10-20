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
                addAndMakeVisible (b);
                b.setButtonText (juce::String (i + 1));
                b.setTooltip ("Toggle cycle beat; click beyond length to extend");
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
        void cancelGesture() { handle.cancel(); }
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
                    return controller.sourceFlashes[static_cast<size_t> (p.channel)][static_cast<size_t> (p.step)] > now
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
                bits[static_cast<size_t> (i)].fontHeight = inlineLayout ? 20.0f : 14.0f;
                bits[static_cast<size_t> (i)].setBounds (10 + i * cellStride, top, side, side);
            }
            const int edge = 10 + juce::jlimit (1, 8, cycleLength) * cellStride - 4;
            handle.setBounds (edge - 4, top, 9, side);
            handle.toFront (false);
            handle.setVisible (!compact);
        }
        void paint (juce::Graphics& g) override
        {
            surface (g, getLocalBounds().toFloat().reduced (0.5f));
            const auto first = bits.front().getBounds().toFloat();
            const auto last = bits[static_cast<size_t> (juce::jlimit (1, 8, cycleLength) - 1)].getBounds().toFloat();
            const auto range = first.getUnion (last).expanded (3, 3);
            const float alpha = isEnabled() ? 1.0f : 0.35f;
            g.setColour (Palette::accent.withAlpha (0.14f * alpha));
            g.fillRoundedRectangle (range, 6);
            g.setColour (Palette::accent.withAlpha (0.55f * alpha));
            g.drawRoundedRectangle (range, 6, 1);
            if (!inlineLayout)
                text (g, "CYCLE", { 120.0f, static_cast<float> (getHeight() - 30), static_cast<float> (getWidth() - 132), 20.0f }, 10, Palette::muted, true, juce::Justification::centredRight);
        }

    private:
        EditorController& controller;
        std::optional<Layer> previousLayer;
        void beginEdit (const juce::String& label)
        {
            previousLayer = controller.layer;
            controller.layer = Layer::chance;
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
                const auto r = getLocalBounds().toFloat().reduced (0.5f);
                g.setColour (Palette::surface);
                g.fillRoundedRectangle (r, 2);
                g.setColour (Palette::accent.withMultipliedAlpha (isEnabled() ? 1.0f : 0.35f));
                g.drawRoundedRectangle (r, 2, 1);
                for (const float x : { 3.0f, 6.0f })
                    g.drawVerticalLine (static_cast<int> (x), r.getCentreY() - 5, r.getCentreY() + 5);
            }
            void mouseDown (const juce::MouseEvent& e) override
            {
                if (!isEnabled() || !e.mods.isLeftButtonDown())
                    return;
                targets = owner.locations();
                if (targets.empty())
                    return;
                grabKeyboardFocus();
                dragging = true;
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
            bool inCycle = false, flashing = false;
            float fontHeight = 14;
            void paintButton (juce::Graphics& g, bool hover, bool down) override
            {
                const auto r = getLocalBounds().toFloat().reduced (0.5f);
                const bool on = getToggleState();
                auto background = on ? Palette::strong : inCycle ? Palette::raised.interpolatedWith (Palette::accent, 0.22f) : Palette::app;
                if (flashing)
                    background = Palette::text;
                if (isEnabled() && (hover || down))
                    background = background.brighter (0.12f);
                g.setColour (background.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
                g.fillRoundedRectangle (r, 4);
                if (inCycle || hasKeyboardFocus (true))
                {
                    g.setColour (Palette::accent.withAlpha (hasKeyboardFocus (true) ? 1.0f : 0.3f));
                    g.drawRoundedRectangle (r, 4, 1);
                }
                ksh::ui::text (g, getButtonText(), r, juce::jmin (fontHeight, r.getHeight() * 0.45f),
                      (on || flashing) ? Palette::app : inCycle ? Palette::text : Palette::faint, true);
            }
        };
        std::array<CycleBit, 8> bits;
    };
}
