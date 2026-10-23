#pragma once
#include "KshStyle.h"

namespace ksh::ui
{
    /** A note field opens the keyboard; pointer drags and wheel ticks never retune it. */
    class NoteInput : public ValueControl
    {
    public:
        std::function<void()> onChoose;
        NoteInput() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void mouseDown (const juce::MouseEvent& e) override { armed = isEnabled() && e.mods.isLeftButtonDown(); }
        void mouseDrag (const juce::MouseEvent&) override {}
        void mouseUp (const juce::MouseEvent& e) override
        {
            const bool clicked = armed && e.getDistanceFromDragStart() < 4;
            armed = false;
            if (clicked)
                choose();
        }
        void mouseDoubleClick (const juce::MouseEvent&) override {}
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
            {
                choose();
                return true;
            }
            return false;
        }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
        {
            juce::AccessibilityActions actions;
            actions.addAction (juce::AccessibilityActionType::press, [this] { choose(); });
            return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::button, std::move (actions));
        }
    private:
        bool armed = false;
        void choose() { if (isEnabled() && onChoose) onChoose(); }
    };

    class NoteChooser : public juce::Component
    {
    public:
        NoteChooser()
        {
            setTitle ("Choose MIDI note");
            setAlwaysOnTop (true);
            setWantsKeyboardFocus (true);
            setFocusContainerType (FocusContainerType::keyboardFocusContainer);
            addAndMakeVisible (close);
            close.setTitle ("Close note chooser");
            close.getProperties().set ("icon", "clear");
            close.onClick = [this] { dismiss(); };
            for (auto* button : { &previous, &next })
                addAndMakeVisible (*button);
            previous.setTitle ("Scroll keyboard left");
            next.setTitle ("Scroll keyboard right");
            previous.onClick = [this] { scroll (-12); };
            next.onClick = [this] { scroll (12); };
            for (int index = 0; index < keyCount; ++index)
            {
                auto& key = keys[static_cast<size_t> (index)];
                addAndMakeVisible (key);
                key.black = juce::MidiMessage::isMidiNoteBlack (index);
                key.onClick = [this, index] { choose (firstNote + index, false); };
                key.pressed = [this] (const juce::MouseEvent& event)
                {
                    lastScrubNote = -1;
                    scrub (event.getEventRelativeTo (this).getPosition());
                };
                key.dragged = [this] (const juce::MouseEvent& event) { scrub (event.getEventRelativeTo (this).getPosition()); };
            }
            for (auto& key : keys)
                if (key.black)
                    key.toFront (false);
        }
        std::function<void (int)> onChoose;
        void open (juce::Rectangle<int> target, const juce::String& rowName, int note, juce::Component* input, juce::Component* rowTitle = nullptr)
        {
            anchor = target;
            returnFocus = input;
            activeRowTitle = rowTitle;
            setDescription ("Editing row " + rowName);
            candidate = juce::jlimit (0, 127, note);
            firstNote = juce::jlimit (0, lastFirstNote, (candidate / 12 - 1) * 12);
            refresh();
            setVisible (true);
            resized();
            toFront (false);
            if (getPeer() != nullptr && isShowing() && !isCurrentlyModal())
                enterModalState (true);
        }
        void dismiss()
        {
            if (isCurrentlyModal())
                exitModalState (0);
            setVisible (false);
            if (returnFocus != nullptr && returnFocus->isShowing())
                returnFocus->grabKeyboardFocus();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey)
                dismiss();
            else if (key == juce::KeyPress::returnKey)
                choose (candidate);
            else if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
            {
                candidate = juce::jlimit (0, 127, candidate + (key == juce::KeyPress::rightKey ? 1 : -1));
                refresh();
            }
            else
                return false;
            return true;
        }
        void inputAttemptWhenModal() override { dismiss(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (!panel.contains (e.getPosition()))
            {
                juce::Component::SafePointer<NoteInput> nextInput;
                if (auto* parent = getParentComponent())
                {
                    const auto position = parent->getLocalPoint (this, e.getPosition());
                    for (int i = parent->getNumChildComponents(); --i >= 0;)
                    {
                        auto* child = parent->getChildComponent (i);
                        if (child == this || !child->isVisible())
                            continue;
                        if (auto* target = child->getComponentAt (child->getLocalPoint (parent, position)))
                        {
                            nextInput = dynamic_cast<NoteInput*> (target);
                            break;
                        }
                    }
                }
                dismiss();
                if (e.mods.isLeftButtonDown() && nextInput != nullptr)
                    nextInput->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
            }
        }
        void resized() override
        {
            constexpr int height = 244;
            constexpr int anchorGap = 24;
            const int rightSpace = getWidth() - anchor.getRight() - anchorGap - 12;
            const int leftSpace = anchor.getX() - anchorGap - 12;
            const bool beside = juce::jmax (rightSpace, leftSpace) >= 800;
            const bool right = rightSpace >= leftSpace;
            const int width = juce::jmin (1000, beside ? (right ? rightSpace : leftSpace) : juce::jmax (0, getWidth() - 24));
            const int xPosition = beside ? (right ? anchor.getRight() + anchorGap : anchor.getX() - width - anchorGap)
                                         : juce::jlimit (12, juce::jmax (12, getWidth() - width - 12), anchor.getX());
            const bool below = getHeight() - anchor.getBottom() >= height + anchorGap + 12;
            const int yPosition = beside ? anchor.getCentreY() - height / 2
                                         : (below ? anchor.getBottom() + anchorGap : anchor.getY() - height - anchorGap);
            panel = { xPosition, juce::jlimit (12, juce::jmax (12, getHeight() - height - 12), yPosition), width, height };
            const int x = panel.getX(), y = panel.getY();
            close.setBounds (panel.getRight() - 44, y + 12, 28, 28);
            previous.setBounds (x + 20, y + 64, 36, 30);
            next.setBounds (panel.getRight() - 56, y + 64, 36, 30);
            const float whiteWidth = static_cast<float> (width - 40) / 28.0f;
            int white = 0;
            for (int index = 0; index < keyCount; ++index)
            {
                auto& key = keys[static_cast<size_t> (index)];
                const float left = static_cast<float> (x + 20) + static_cast<float> (white) * whiteWidth;
                if (key.black)
                    key.setBounds (juce::roundToInt (left - whiteWidth * 0.3f), y + 108, juce::roundToInt (whiteWidth * 0.6f), 74);
                else
                {
                    key.setBounds (juce::roundToInt (left), y + 108, juce::roundToInt (whiteWidth) - 1, 116);
                    ++white;
                }
            }
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black.withAlpha (0.58f));
            if (auto* input = dynamic_cast<NoteInput*> (returnFocus.getComponent()); input != nullptr && input->isVisible())
            {
                auto button = anchor.toFloat();
                if (input->caption.isNotEmpty())
                {
                    if (input->captionWidth > 0)
                        button.removeFromLeft (static_cast<float> (input->captionWidth));
                    else
                        button.removeFromTop (18.0f + static_cast<float> (input->captionGap));
                }
                if (input->valueWidth > 0)
                    button.setWidth (static_cast<float> (juce::jmin (input->valueWidth, input->getWidth())));
                for (int spread = 8; spread > 0; --spread)
                {
                    g.setColour (Palette::accent.withAlpha (0.018f * static_cast<float> (9 - spread)));
                    g.drawRoundedRectangle (button.expanded (static_cast<float> (spread)), 5.0f + static_cast<float> (spread), 2);
                }
                surface (g, button.reduced (0.5f), true, false);
                const auto label = input->format ? input->format (input->value) : juce::String (input->value) + input->suffix;
                text (g, label, button, input->valueFontSize, juce::Colours::white, true);
            }
            if (activeRowTitle != nullptr && activeRowTitle->isVisible())
            {
                const auto origin = getLocalPoint (activeRowTitle.getComponent(), juce::Point<int>());
                const juce::Graphics::ScopedSaveState saved (g);
                g.addTransform (juce::AffineTransform::translation (static_cast<float> (origin.x), static_cast<float> (origin.y)));
                activeRowTitle->paintEntireComponent (g, false);
            }
            const auto r = panel.toFloat();
            g.setColour (Palette::shell);
            g.fillRoundedRectangle (r, 9);
            g.setColour (Palette::border);
            g.drawRoundedRectangle (r.reduced (0.5f), 9, 1);
            const auto noteName = juce::MidiMessage::getMidiNoteName (candidate, true, true, 4);
            const auto midiNumber = juce::String (candidate);
            const auto labelFont = font (36, true);
            constexpr float noteWidth = 88, numberWidth = 78, gap = 12;
            const float groupX = r.getCentreX() - (noteWidth + gap + numberWidth) / 2;
            text (g, noteName, { groupX, r.getY() + 44, noteWidth, 48 }, 36, Palette::accent, true, juce::Justification::centredRight);

            // Reserve three equal-width digit slots, keeping units in the same place
            // across both proportional glyphs and changes between 1/2/3 digits.
            float digitWidth = 0;
            for (const auto digit : juce::String ("0123456789"))
                digitWidth = juce::jmax (digitWidth, juce::GlyphArrangement::getStringWidth (labelFont, juce::String::charToString (digit)));
            const float numberX = groupX + noteWidth + gap;
            const float baseline = r.getY() + 68 + (labelFont.getAscent() - labelFont.getDescent()) * 0.5f;
            g.setColour (Palette::text);
            for (int index = 0; index < midiNumber.length(); ++index)
            {
                juce::GlyphArrangement digit;
                digit.addLineOfText (labelFont, midiNumber.substring (index, index + 1), 0, 0);
                const auto bounds = digit.getBoundingBox (0, -1, true);
                const float slotX = numberX + static_cast<float> (3 - midiNumber.length() + index) * digitWidth;
                digit.draw (g, juce::AffineTransform::translation (slotX + (digitWidth - bounds.getWidth()) * 0.5f - bounds.getX(), baseline));
            }
        }
    private:
        class PianoKey : public juce::TextButton
        {
        public:
            bool black = false;
            std::function<void (const juce::MouseEvent&)> pressed, dragged;
            void mouseDown (const juce::MouseEvent& event) override
            {
                if (isEnabled() && event.mods.isLeftButtonDown() && pressed)
                    pressed (event);
            }
            void mouseDrag (const juce::MouseEvent& event) override
            {
                if (event.mods.isLeftButtonDown() && dragged)
                    dragged (event);
            }
            void mouseUp (const juce::MouseEvent&) override {}

            void paintButton (juce::Graphics& g, bool hover, bool down) override
            {
                const auto r = getLocalBounds().toFloat().reduced (0.5f);
                auto colour = getToggleState() ? Palette::accent : black ? Palette::app : Palette::text.darker (0.12f);
                if (hover || down)
                    colour = colour.interpolatedWith (Palette::accent, 0.25f);
                g.setColour (colour.withMultipliedAlpha (isEnabled() ? 1.0f : 0.2f));
                g.fillRoundedRectangle (r, 4);
                g.setColour (Palette::border.withAlpha (0.65f));
                g.drawRoundedRectangle (r, 4, 1);
                ksh::ui::text (g, getButtonText(), r.withTrimmedTop (r.getHeight() - 28), 13, !isEnabled() ? Palette::faint : getToggleState() || !black ? Palette::app : Palette::text, true);
            }
        };
        static constexpr int keyCount = 48;
        static constexpr int lastFirstNote = 84;
        std::array<PianoKey, keyCount> keys;
        juce::TextButton previous { "<" }, next { ">" };
        juce::TextButton close { juce::String::fromUTF8 ("×") };
        juce::Rectangle<int> anchor, panel;
        juce::Component::SafePointer<juce::Component> returnFocus;
        juce::Component::SafePointer<juce::Component> activeRowTitle;
        int candidate = 36, firstNote = 24, lastScrubNote = -1;
        void refresh()
        {
            previous.setEnabled (firstNote > 0);
            next.setEnabled (firstNote < lastFirstNote);
            for (int index = 0; index < keyCount; ++index)
            {
                auto& key = keys[static_cast<size_t> (index)];
                const int midi = firstNote + index;
                key.setEnabled (midi <= 127);
                key.setToggleState (midi == candidate, juce::dontSendNotification);
                const auto name = juce::MidiMessage::getMidiNoteName (juce::jmin (127, midi), true, true, 4);
                key.setButtonText (midi % 12 == 0 ? name : juce::String());
                key.setTitle (midi <= 127 ? name : "Unavailable note");
                key.setTooltip (midi <= 127 ? name + " / MIDI " + juce::String (midi) : "Outside MIDI range");
            }
            repaint();
        }
        void scroll (int offset)
        {
            firstNote = juce::jlimit (0, lastFirstNote, firstNote + offset);
            refresh();
        }
        void scrub (juce::Point<int> position)
        {
            auto* key = dynamic_cast<PianoKey*> (getComponentAt (position));
            if (key == nullptr || !key->isEnabled())
            {
                lastScrubNote = -1;
                return;
            }
            const int midi = firstNote + static_cast<int> (key - keys.data());
            if (midi == lastScrubNote)
                return;
            lastScrubNote = midi;
            choose (midi, false);
        }
        void choose (int midi, bool closeAfter = true)
        {
            if (!isVisible() || midi < 0 || midi > 127)
                return;
            candidate = midi;
            refresh();
            if (onChoose)
                onChoose (midi);
            if (closeAfter)
                dismiss();
        }
    };
}
