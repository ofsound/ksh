#pragma once

#include "KshStyle.h"

namespace ksh::ui
{
    /** Click selects; a deliberate drag exports without selecting or copying the pattern. */
    class PatternButton : public juce::TextButton
    {
    public:
        std::function<void()> onDrag;
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
        bool armed = false, dragged = false;
    };
}
