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
            for (int index = 0; index < 60; ++index)
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
            addAndMakeVisible (number);
            number.setTitle ("MIDI note number");
            number.setInputRestrictions (3, "0123456789");
            number.setJustification (juce::Justification::centred);
            number.setFont (font (14));
            number.onTextChange = [this]
            {
                const bool valid = validNumber();
                apply.setEnabled (valid);
                if (valid)
                {
                    candidate = number.getText().getIntValue();
                    refresh (false);
                }
            };
            number.onReturnKey = [this] { if (validNumber()) choose (number.getText().getIntValue()); };
            number.onEscapeKey = [this] { dismiss(); };
            addAndMakeVisible (apply);
            apply.setTitle ("Assign MIDI note");
            apply.onClick = [this] { if (validNumber()) choose (number.getText().getIntValue()); };
        }
        std::function<void (int)> onChoose;
        void open (juce::Rectangle<int> target, const juce::String& channelName, int note, juce::Component* input)
        {
            anchor = target;
            returnFocus = input;
            channel = channelName;
            candidate = juce::jlimit (0, 127, note);
            firstNote = 24; // C1: the initial five-octave window.
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
            const int width = juce::jmin (900, juce::jmax (0, getWidth() - 24));
            panel = { (getWidth() - width) / 2,
                      juce::jlimit (12, juce::jmax (12, getHeight() - 286 - 12), anchor.getCentreY() - 143), width, 286 };
            const int x = panel.getX(), y = panel.getY();
            close.setBounds (panel.getRight() - 44, y + 12, 28, 28);
            previous.setBounds (x + 20, y + 60, 36, 30);
            next.setBounds (panel.getRight() - 56, y + 60, 36, 30);
            const float whiteWidth = static_cast<float> (width - 40) / 35.0f;
            int white = 0;
            for (int index = 0; index < 60; ++index)
            {
                auto& key = keys[static_cast<size_t> (index)];
                const float left = static_cast<float> (x + 20) + static_cast<float> (white) * whiteWidth;
                if (key.black)
                    key.setBounds (juce::roundToInt (left - whiteWidth * 0.3f), y + 108, juce::roundToInt (whiteWidth * 0.6f), 66);
                else
                {
                    key.setBounds (juce::roundToInt (left), y + 108, juce::roundToInt (whiteWidth) - 1, 104);
                    ++white;
                }
            }
            number.setBounds (x + 60, y + 232, 64, 30);
            apply.setBounds (x + 132, y + 232, 52, 30);
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black.withAlpha (0.18f));
            const auto r = panel.toFloat();
            g.setColour (Palette::shell);
            g.fillRoundedRectangle (r, 9);
            g.setColour (Palette::border);
            g.drawRoundedRectangle (r.reduced (0.5f), 9, 1);
            if (panel.getX() > anchor.getRight())
            {
                const float tipY = juce::jlimit (r.getY() + 18, r.getBottom() - 18, static_cast<float> (anchor.getCentreY()));
                juce::Path pointer;
                pointer.addTriangle (r.getX(), tipY - 7, r.getX() - 8, tipY, r.getX(), tipY + 7);
                g.setColour (Palette::shell);
                g.fillPath (pointer);
            }
            text (g, "Channel " + channel + " / Note", r.reduced (20, 14).withWidth (220).withHeight (22), 14, Palette::text, true, juce::Justification::centredLeft);
            text (g, juce::MidiMessage::getMidiNoteName (candidate, true, true, 4) + " / " + juce::String (candidate), { r.getRight() - 164, r.getY() + 16, 112, 20 }, 13, Palette::accent, true, juce::Justification::centredRight);
            text (g, juce::MidiMessage::getMidiNoteName (firstNote, true, true, 4) + " – "
                      + juce::MidiMessage::getMidiNoteName (juce::jmin (127, firstNote + 59), true, true, 4),
                  { r.getX() + 64, r.getY() + 60, r.getWidth() - 128, 30 }, 12, Palette::muted, true);
            text (g, "MIDI", { r.getX() + 20, r.getY() + 232, 36, 30 }, 10, Palette::muted, true, juce::Justification::centredLeft);
            text (g, "Press or drag keys to assign and audition.", { r.getX() + 204, r.getY() + 230, r.getWidth() - 224, 18 }, 12, Palette::muted, false, juce::Justification::centredLeft);
            text (g, "Esc or click outside to close.", { r.getX() + 204, r.getY() + 248, r.getWidth() - 224, 18 }, 12, Palette::muted, false, juce::Justification::centredLeft);
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
                ksh::ui::text (g, getButtonText(), r.withTrimmedTop (r.getHeight() - 28), 10, !isEnabled() ? Palette::faint : getToggleState() || !black ? Palette::app : Palette::text, true);
            }
        };
        std::array<PianoKey, 60> keys;
        juce::TextButton previous { "<" }, next { ">" };
        juce::TextButton close { juce::String::fromUTF8 ("×") }, apply { "Set" };
        juce::TextEditor number;
        juce::Rectangle<int> anchor, panel;
        juce::Component::SafePointer<juce::Component> returnFocus;
        juce::String channel;
        int candidate = 36, firstNote = 24, lastScrubNote = -1;
        bool validNumber() const { return number.getText().isNotEmpty() && number.getText().getIntValue() <= 127; }
        void refresh (bool updateInput = true)
        {
            previous.setEnabled (firstNote > 0);
            next.setEnabled (firstNote < 72);
            for (int index = 0; index < 60; ++index)
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
            if (updateInput)
            {
                number.setText (juce::String (candidate), false);
                apply.setEnabled (true);
            }
            repaint();
        }
        void scroll (int offset)
        {
            firstNote = juce::jlimit (0, 72, firstNote + offset);
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
