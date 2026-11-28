#pragma once

#include "KshStyle.h"

namespace ksh::ui
{
    /** Click selects; a deliberate drag exports without selecting or copying the pattern. */
    class PatternButton : public juce::TextButton
    {
    public:
        std::function<void()> onDrag;
        bool copyDestination = false;
        void setOccupied (bool value)
        {
            if (std::exchange (occupied, value) != value)
                repaint();
        }
        bool showsCopyDestination() const { return copyDestination && isEnabled() && isOver(); }
        void paintButton (juce::Graphics& g, bool hover, bool down) override
        {
            juce::TextButton::paintButton (g, hover, down);
            if (occupied)
            {
                const auto led = juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ getWidth() * 0.5f, getHeight() - 9.0f });
                const auto ink = getToggleState() ? Palette::text : Palette::accent;
                const auto centre = led.getCentre();
                g.setGradientFill ({ ink.interpolatedWith (juce::Colours::white, 0.28f), centre,
                                     ink.withAlpha (0.82f), centre.withX (led.getRight()), true });
                g.fillEllipse (led);
            }
            if (showsCopyDestination())
                paintPatternCopyOutline (g, getLocalBounds().toFloat());
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            dragged = false;
            armed = isEnabled() && e.mods.isLeftButtonDown();
            juce::TextButton::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (!dragged && armed && isEnabled() && e.mods.isLeftButtonDown() && e.getDistanceFromDragStart() >= 6 && onDrag)
            {
                dragged = true;
                setState (juce::Button::buttonNormal);
                const auto callback = onDrag;
                callback();
                return;
            }
            if (!dragged)
                juce::TextButton::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            armed = false;
            if (dragged)
            {
                setState (juce::Button::buttonNormal);
                return;
            }
            juce::TextButton::mouseUp (e);
        }

    private:
        bool armed = false, dragged = false, occupied = false;
    };
}
