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
            close.onClick = [this] { dismiss(); };
            for (int octave = 0; octave < 11; ++octave)
            {
                auto& button = octaves[static_cast<size_t> (octave)];
                addAndMakeVisible (button);
                const auto name = juce::MidiMessage::getMidiNoteName (octave * 12, true, true, 4);
                button.setButtonText (name);
                button.setTitle ("Octave " + name);
                button.setTooltip ("Browse the octave beginning at " + name);
                button.onClick = [this, octave]
                {
                    candidate = juce::jmin (127, octave * 12 + candidate % 12);
                    refresh();
                };
            }
            for (int pitch = 0; pitch < 12; ++pitch)
            {
                auto& key = keys[static_cast<size_t> (pitch)];
                addAndMakeVisible (key);
                key.black = juce::MidiMessage::isMidiNoteBlack (pitch);
                key.setButtonText (juce::MidiMessage::getMidiNoteName (pitch, true, false, 4));
                key.onClick = [this, pitch] { choose (candidate / 12 * 12 + pitch); };
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
            refresh();
            setVisible (true);
            resized();
            toFront (false);
            if (getPeer() != nullptr && isShowing())
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
                dismiss();
        }
        void resized() override
        {
            const int width = juce::jmin (440, juce::jmax (0, getWidth() - 24));
            panel = { juce::jlimit (12, juce::jmax (12, getWidth() - width - 12), anchor.getRight() + 12),
                      juce::jlimit (12, juce::jmax (12, getHeight() - 286 - 12), anchor.getCentreY() - 143), width, 286 };
            const int x = panel.getX(), y = panel.getY();
            close.setBounds (panel.getRight() - 44, y + 12, 28, 28);
            const float stride = static_cast<float> (width - 40) / 11.0f;
            for (int octave = 0; octave < 11; ++octave)
                octaves[static_cast<size_t> (octave)].setBounds (x + 20 + juce::roundToInt (static_cast<float> (octave) * stride), y + 68, juce::roundToInt (stride) - 4, 28);
            const float whiteWidth = static_cast<float> (width - 40) / 7.0f;
            int white = 0;
            for (int pitch = 0; pitch < 12; ++pitch)
            {
                auto& key = keys[static_cast<size_t> (pitch)];
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
            text (g, "OCTAVE", { r.getX() + 20, r.getY() + 45, 110, 16 }, 10, Palette::muted, true, juce::Justification::centredLeft);
            text (g, "MIDI", { r.getX() + 20, r.getY() + 232, 36, 30 }, 10, Palette::muted, true, juce::Justification::centredLeft);
            text (g, "Click a key to assign.", { r.getX() + 204, r.getY() + 230, r.getWidth() - 224, 18 }, 11, Palette::muted, false, juce::Justification::centredLeft);
            text (g, "Esc or click outside to cancel.", { r.getX() + 204, r.getY() + 248, r.getWidth() - 224, 18 }, 11, Palette::muted, false, juce::Justification::centredLeft);
        }
    private:
        class PianoKey : public juce::TextButton
        {
        public:
            bool black = false;
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
                ksh::ui::text (g, getButtonText(), r.withTrimmedTop (r.getHeight() - 28), 12, !isEnabled() ? Palette::faint : getToggleState() || !black ? Palette::app : Palette::text, true);
            }
        };
        std::array<juce::TextButton, 11> octaves;
        std::array<PianoKey, 12> keys;
        juce::TextButton close { juce::String::fromUTF8 ("×") }, apply { "Set" };
        juce::TextEditor number;
        juce::Rectangle<int> anchor, panel;
        juce::Component::SafePointer<juce::Component> returnFocus;
        juce::String channel;
        int candidate = 36;
        bool validNumber() const { return number.getText().isNotEmpty() && number.getText().getIntValue() <= 127; }
        void refresh (bool updateInput = true)
        {
            const int octave = candidate / 12;
            for (int i = 0; i < 11; ++i)
                octaves[static_cast<size_t> (i)].setToggleState (i == octave, juce::dontSendNotification);
            for (int pitch = 0; pitch < 12; ++pitch)
            {
                auto& key = keys[static_cast<size_t> (pitch)];
                const int midi = octave * 12 + pitch;
                key.setEnabled (midi <= 127);
                key.setToggleState (midi == candidate, juce::dontSendNotification);
                key.setTitle (juce::MidiMessage::getMidiNoteName (midi, true, true, 4));
                key.setTooltip (key.getTitle() + " / MIDI " + juce::String (midi));
            }
            if (updateInput)
            {
                number.setText (juce::String (candidate), false);
                apply.setEnabled (true);
            }
            repaint();
        }
        void choose (int midi)
        {
            if (!isVisible() || midi < 0 || midi > 127)
                return;
            dismiss();
            if (onChoose)
                onChoose (midi);
        }
    };
}
