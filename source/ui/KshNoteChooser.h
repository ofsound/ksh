#pragma once
#include "KshLayout.h"
#include "KshStyle.h"

namespace ksh::ui
{
    /** A note field opens the keyboard; pointer drags and wheel ticks never retune it. */
    class NoteInput : public ValueControl
    {
    public:
        std::function<void()> onChoose;
        void setStackedCcLabel (bool enabled)
        {
            if (std::exchange (stackedCcLabel, enabled) != enabled)
                repaint();
        }
        void paint (juce::Graphics& g) override
        {
            if (!stackedCcLabel)
            {
                ValueControl::paint (g);
                return;
            }

            const auto face = valueFaceBounds().reduced (0.5f);
            controlSurface (g, face, false, isEnabled() && (isMouseOver() || hasKeyboardFocus (false)));
            auto labels = face.withSizeKeepingCentre (face.getWidth(), std::min (28.0f, face.getHeight()));
            const auto title = labels.removeFromTop (labels.getHeight() * 0.40f);
            text (g, "CC", title, 9.5f, isEnabled() ? Palette::muted : Palette::faint, true);
            text (g, juce::String (value), labels, 14.0f, isEnabled() ? Palette::text : Palette::faint, true);
        }
        juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::NormalCursor; }
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
        bool armed = false, stackedCcLabel = false;
        void choose() { if (isEnabled() && onChoose) onChoose(); }
    };

    class NoteChooser : public juce::Component, private juce::Timer
    {
    public:
        NoteChooser()
        {
            setTitle ("Choose MIDI note");
            setAlwaysOnTop (true);
            setWantsKeyboardFocus (true);
            setFocusContainerType (FocusContainerType::keyboardFocusContainer);
            for (auto* button : { &previous, &next })
                addAndMakeVisible (*button);
            previous.setTitle ("Scroll keyboard left");
            next.setTitle ("Scroll keyboard right");
            previous.getProperties().set ("icon", "keyboardPrevious");
            next.getProperties().set ("icon", "keyboardNext");
            previous.onClick = [this] { scroll (-notesPerOctave); };
            next.onClick = [this] { scroll (notesPerOctave); };
            constexpr std::array labels { "−12", "−1", "+1", "+12" };
            constexpr std::array titles { "Transpose all rows down an octave", "Transpose all rows down a semitone",
                                          "Transpose all rows up a semitone", "Transpose all rows up an octave" };
            for (size_t i = 0; i < transposeButtons.size(); ++i)
            {
                auto& button = transposeButtons[i];
                addAndMakeVisible (button);
                button.setButtonText (juce::String::fromUTF8 (labels[i]));
                button.setTitle (titles[i]);
                button.onClick = [this, i] { if (onTransposeAll) onTransposeAll (transposeOffsets[i]); };
            }
            addAndMakeVisible (scalePresets);
            scalePresets.getProperties().set ("dropdown", true);
            scalePresets.onClick = [this] { showScalePresets(); };
            for (auto* button : { &noteMode, &ccMode })
            {
                addAndMakeVisible (*button);
                button->setToggleable (true);
            }
            noteMode.setTitle ("Note mode");
            ccMode.setTitle ("CC mode");
            noteMode.setToggleState (true, juce::dontSendNotification);
            noteMode.onClick = [this] { if (controllerMode) updateController (false, ccNumber.value, ccChannel.value); };
            ccMode.onClick = [this] { if (!controllerMode) updateController (true, ccNumber.value, ccChannel.value); };
            for (auto* control : { &ccNumber, &ccChannel })
            {
                addChildComponent (*control);
                control->captionFontSize = 16;
                control->valueFontSize = 22;
                control->captionGap = 5;
                control->sensitivity = 2.0f;
                control->horizontalDragOnTouch = true;
                control->useInlineTextDisplay();
                control->onEnd = [this] { updateController (true, ccNumber.value, ccChannel.value); };
            }
            ccNumber.caption = "CC number";
            ccNumber.setTitle ("CC number");
            ccNumber.maximum = 127;
            ccChannel.caption = "MIDI channel";
            ccChannel.setTitle ("CC MIDI channel");
            ccChannel.minimum = 1;
            ccChannel.maximum = 16;
            addAndMakeVisible (keyboard);
            keyboard.setInterceptsMouseClicks (false, true);
            keyboard.addAndMakeVisible (keyStrip);
            keyStrip.setInterceptsMouseClicks (false, true);
            for (int midi = 0; midi < keyCount; ++midi)
            {
                auto& key = keys[static_cast<size_t> (midi)];
                keyStrip.addChildComponent (key);
                key.black = juce::MidiMessage::isMidiNoteBlack (midi);
                key.setEnabled (midi <= 127);
                const auto name = juce::MidiMessage::getMidiNoteName (juce::jmin (127, midi), true, true, 4);
                key.setButtonText (midi % 12 == 0 ? name : juce::String());
                key.setTitle (midi <= 127 ? name : "Unavailable note");
                key.setTooltip (midi <= 127 ? name + " / MIDI " + juce::String (midi) : "Outside MIDI range");
                key.onClick = [this, midi] { choose (midi, false); };
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
            keys[static_cast<size_t> (candidate)].setToggleState (true, juce::dontSendNotification);
            noteMode.toFront (false);
            setAccent (Palette::accent);
        }
        std::function<void (int)> onChoose, onNavigate;
        std::function<void (bool, int, int)> onControllerChange;
        std::function<void()> onDismiss;
        using RowNotes = std::array<int, Constants::maxChannels>;
        enum class PresetOctave { current, fixed };
        std::function<void (int)> onTransposeAll;
        std::function<void (RowNotes, PresetOctave)> onPresetAll;

        void setAccent (juce::Colour colour)
        {
            if (isColourSpecified (juce::TextButton::buttonOnColourId) && findColour (juce::TextButton::buttonOnColourId) == colour)
                return;
            setColour (juce::TextButton::buttonOnColourId, colour);
            scalePresets.setColour (juce::PopupMenu::highlightedTextColourId, colour);
            repaint();
        }

        void setTransposeRange (int lowest, int highest)
        {
            for (size_t i = 0; i < transposeButtons.size(); ++i)
                transposeButtons[i].setEnabled (lowest + transposeOffsets[i] >= 0 && highest + transposeOffsets[i] <= 127);
        }

        void setAssignedNote (int note)
        {
            setCandidate (assignedNote = note);
            revealCandidate();
        }

        void setRowOutput (const Channel& row)
        {
            controllerMode = row.controller;
            noteMode.setToggleState (!row.controller, juce::dontSendNotification);
            ccMode.setToggleState (row.controller, juce::dontSendNotification);
            (row.controller ? ccMode : noteMode).toFront (false);
            ccNumber.setValue (row.ccNumber);
            ccChannel.setValue (row.ccChannel);
            for (auto* component : { static_cast<juce::Component*> (&keyboard), static_cast<juce::Component*> (&previous),
                                     static_cast<juce::Component*> (&next), static_cast<juce::Component*> (&scalePresets) })
                component->setVisible (!row.controller);
            for (auto& button : transposeButtons)
                button.setVisible (!row.controller);
            ccNumber.setVisible (row.controller);
            ccChannel.setVisible (row.controller);
            repaint();
        }
        void setGridCells (juce::Rectangle<float> bounds, juce::Rectangle<float> dimBounds)
        {
            if (gridCells == bounds && gridDimmingBounds == dimBounds)
                return;
            gridCells = bounds;
            gridDimmingBounds = dimBounds;
            resized();
            repaint();
        }

        void setMenuPage (bool enabled)
        {
            if (std::exchange (menuPage, enabled) == enabled)
                return;
            setAlwaysOnTop (!enabled);
            scalePresets.getProperties().set ("dropdownInsetReduction", enabled ? phoneDropdownInsetReduction : 0);
            resized();
            resetKeyboard();
            repaint();
        }

        void setMenuArea (juce::Rectangle<int> area) { setBounds (area); }

        void open (const juce::String& rowName, int note, juce::Component* input)
        {
            returnFocus = input;
            setDescription ("Editing track " + rowName + ". Up and Down switch tracks.");
            setCandidate (assignedNote = juce::jlimit (0, 127, note));
            resetKeyboard();
            setVisible (true);
            toFront (false);
            repaint();
            if (isShowing())
                grabKeyboardFocus();
        }
        void dismiss()
        {
            const bool wasVisible = isVisible();
            ccNumber.cancelGesture();
            ccChannel.cancelGesture();
            setVisible (false);
            if (wasVisible && onDismiss)
                onDismiss();
            if (returnFocus != nullptr && returnFocus->isShowing())
                returnFocus->grabKeyboardFocus();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey)
                dismiss();
            else if (controllerMode)
            {
                if ((key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) && onNavigate)
                    onNavigate (key == juce::KeyPress::upKey ? -1 : 1);
                else
                    return false;
            }
            else if (key == juce::KeyPress::returnKey)
                choose (candidate);
            else if ((key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) && onNavigate)
            {
                if (candidate != assignedNote)
                    choose (candidate, false);
                if (isVisible())
                    onNavigate (key == juce::KeyPress::upKey ? -1 : 1);
            }
            else if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
            {
                setCandidate (juce::jlimit (0, 127, candidate + (key == juce::KeyPress::rightKey ? 1 : -1)));
                revealCandidate();
            }
            else
                return false;
            return true;
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (!panel.contains (e.getPosition()) && !modeBounds.contains (e.getPosition()))
            {
                ValueControl::endPendingWheelGesture();
                if (menuPage)
                {
                    dismiss();
                    return;
                }
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
            constexpr int modeHeight = 48, modeWidth = 146;
            if (menuPage)
                panel = getLocalBounds();
            else
            {
                const auto area = gridCells.isEmpty() ? getLocalBounds().toFloat() : gridCells;
                const int width = juce::jlimit (0, 1000, juce::roundToInt (area.getWidth()) - 24);
                constexpr int noteHeight = 244 + allRowsHeight, modeGap = 8, overlayMargin = 4;
                const auto room = getLocalBounds().withTrimmedTop (overlayMargin + modeHeight + modeGap).withTrimmedBottom (overlayMargin);
                const int height = std::min (room.getHeight(), juce::jlimit (noteHeight - 32, noteHeight, juce::roundToInt (area.getHeight()) - 12));
                panel = area.withSizeKeepingCentre (static_cast<float> (width), static_cast<float> (height)).toNearestInt().constrainedWithin (room);
                panelOutline = popupPanelOutline (panel.toFloat().reduced (0.5f), 9.0f);
                modeBounds = { panel.getX(), panel.getY() - modeGap - modeHeight, std::min (modeWidth, panel.getWidth()), modeHeight };
            }
            const auto content = panel.reduced (menuPage ? menuInset : contentInset, 0);
            auto toolbar = content.withY (panel.getY() + (menuPage ? 0 : allRowsPadding))
                                  .withHeight (menuPage ? 42 : Layout::touchTarget);
            if (menuPage)
            {
                modeBounds = toolbar.withWidth (126);
                const int presetsWidth = dropdownWidth (scalePresets.getButtonText()) - 2 * phoneDropdownInsetReduction + 2;
                scalePresets.setBounds (toolbar.withRight (content.getRight()).removeFromRight (presetsWidth));
                toolbar = toolbar.translated (0, toolbar.getHeight() + 5);
            }
            const int segmentWidth = modeBounds.getWidth() / 2;
            noteMode.setBounds (modeBounds.withWidth (std::min (modeBounds.getWidth(), segmentWidth + 1)));
            ccMode.setBounds (modeBounds.withTrimmedLeft (segmentWidth));
            const int controlsWidth = std::min (304, content.getWidth());
            auto controls = menuPage ? content.withY (modeBounds.getBottom() + 26).withSize (controlsWidth, 77)
                                     : content.withSizeKeepingCentre (controlsWidth, 77);
            ccNumber.setBounds (controls.removeFromLeft (controls.getWidth() / 2).reduced (5, 0));
            ccChannel.setBounds (controls.reduced (5, 0));
            const int controlGap = menuPage ? 4 : 8;
            constexpr int buttonCount = static_cast<int> (transposeOffsets.size());
            const int gapWidth = (buttonCount - 1) * controlGap;
            const int labelGap = menuPage ? 8 : 12;
            const int labelWidth = juce::GlyphArrangement::getStringWidthInt (font (menuPage ? 16 : 19, true), transposeLabel);
            allRowsLabelBounds = toolbar.removeFromLeft (menuPage ? 66 : 96);
            if (!menuPage)
                scalePresets.setBounds (toolbar.removeFromRight (156));
            const int fixedWidth = labelWidth + labelGap + gapWidth;
            const int buttonWidth = juce::jlimit (0, Layout::touchTarget, (toolbar.getWidth() - fixedWidth) / buttonCount);
            auto transposeGroup = toolbar.withSizeKeepingCentre (buttonCount * buttonWidth + fixedWidth, toolbar.getHeight());
            transposeLabelBounds = transposeGroup.removeFromLeft (labelWidth);
            transposeGroup.removeFromLeft (labelGap);
            for (auto& button : transposeButtons)
            {
                button.setBounds (transposeGroup.removeFromLeft (buttonWidth));
                transposeGroup.removeFromLeft (controlGap);
            }
            constexpr int octaveButtonSize = 42;
            constexpr int octaveButtonGap = 8;
            const int keyY = menuPage ? toolbar.getBottom() + 6 + octaveButtonSize + octaveButtonGap
                                      : panel.getBottom() - contentInset - whiteKeyHeight;
            const int keyHeight = menuPage ? juce::jmax (0, panel.getBottom() - menuInset - keyY) : whiteKeyHeight;
            const int octaveButtonY = keyY - octaveButtonGap - octaveButtonSize;
            previous.setBounds (content.getX(), octaveButtonY, octaveButtonSize, octaveButtonSize);
            next.setBounds (content.getRight() - octaveButtonSize, octaveButtonY, octaveButtonSize, octaveButtonSize);
            const int keyboardWidth = content.getWidth();
            if (keyboard.getWidth() != keyboardWidth || keyboard.getHeight() != keyHeight || laidOutOctaves != visibleOctaves())
                layoutKeys (keyboardWidth, keyHeight);
            keyboard.setBounds (content.getX(), keyY, keyboardWidth, keyHeight);
            positionKeyboard();
        }
        void visibilityChanged() override
        {
            if (!isVisible())
                stopTimer();
        }
        void paint (juce::Graphics& g) override
        {
            if (!menuPage && !gridDimmingBounds.isEmpty())
            {
                g.setColour (juce::Colours::black.withAlpha (0.80f));
                g.fillRect (gridDimmingBounds);
            }
            const auto accent = findColour (juce::TextButton::buttonOnColourId);
            if (const auto* input = dynamic_cast<NoteInput*> (returnFocus.getComponent()); !menuPage && input != nullptr && input->isVisible())
            {
                juce::Path outline;
                outline.addRoundedRectangle (getLocalArea (input, input->valueFaceBounds()).reduced (0.5f), 5.0f);
                anchorGlow.setColor (accent.withAlpha (0.78f));
                anchorGlow.render (g, outline, juce::PathStrokeType (3.0f));
                g.setColour (accent);
                g.strokePath (outline, juce::PathStrokeType (1.25f));
            }
            const auto r = panel.toFloat();
            if (!menuPage)
                paintPopupPanel (g, panelOutline, accent, panelGlow);
            if (!controllerMode || menuPage)
            {
                const int dividerY = menuPage ? modeBounds.getBottom() + 2 : panel.getY() + allRowsHeight;
                g.setColour (Palette::muted.withAlpha (0.40f));
                g.fillRect (r.reduced (menuPage ? menuInset : contentInset, 0.0f).withY (static_cast<float> (dividerY)).withHeight (1.5f));
            }
            if (controllerMode)
                return;
            const float labelSize = menuPage ? 16.0f : 19.0f;
            text (g, "All rows:", allRowsLabelBounds.toFloat(), labelSize, Palette::muted, true, juce::Justification::centredLeft);
            text (g, transposeLabel, transposeLabelBounds.toFloat(), labelSize, Palette::muted, true, juce::Justification::centredLeft);
            const auto noteName = juce::MidiMessage::getMidiNoteName (candidate, true, true, 4);
            const auto midiNumber = juce::String (candidate);
            const auto labelFont = font (36, true);
            constexpr float noteWidth = 88, numberWidth = 78, gap = 12;
            const float groupX = r.getCentreX() - (noteWidth + gap + numberWidth) / 2;
            const float noteReadoutY = static_cast<float> (menuPage ? previous.getY() - 3 : keyboard.getY() - 64);
            text (g, noteName, { groupX, noteReadoutY, noteWidth, 48 }, 36, accent, true, juce::Justification::centredRight);

            // Reserve three equal-width digit slots, keeping units in the same place
            // across both proportional glyphs and changes between 1/2/3 digits.
            float digitWidth = 0;
            for (const auto digit : juce::String ("0123456789"))
                digitWidth = juce::jmax (digitWidth, juce::GlyphArrangement::getStringWidth (labelFont, juce::String::charToString (digit)));
            const float numberX = groupX + noteWidth + gap;
            const float baseline = noteReadoutY + 24 + (labelFont.getAscent() - labelFont.getDescent()) * 0.5f;
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
        void layoutKeys (int keyboardWidth, int height)
        {
            laidOutOctaves = visibleOctaves();
            const int visibleWhiteKeys = laidOutOctaves * 7;
            const auto whiteKeyX = [keyboardWidth, visibleWhiteKeys] (int index) { return static_cast<double> (index * keyboardWidth) / visibleWhiteKeys; };
            const double whiteWidth = whiteKeyX (1);
            const int blackKeyHeight = juce::roundToInt (74.0f * static_cast<float> (height) / whiteKeyHeight);
            int white = 0;
            for (int index = 0; index < keyCount; ++index)
            {
                auto& key = keys[static_cast<size_t> (index)];
                const double left = whiteKeyX (white);
                if (key.black)
                    key.setBounds (juce::roundToInt (left - whiteWidth * 0.3), 0, juce::roundToInt (whiteWidth * 0.6), blackKeyHeight);
                else
                {
                    const int x = juce::roundToInt (left);
                    const int right = juce::roundToInt (whiteKeyX (++white));
                    key.setBounds (x, 0, juce::jmax (0, right - x - 1), height);
                }
            }
            keyStrip.setSize (juce::roundToInt (whiteKeyX (white)), height);
        }

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
                const auto accent = findColour (juce::TextButton::buttonOnColourId, true);
                auto colour = getToggleState() ? accent : black ? Palette::controls.app : Palette::text.darker (0.12f);
                if (isEnabled() && (hover || down || hasKeyboardFocus (false)))
                    colour = colour.interpolatedWith (accent, 0.25f);
                g.setColour (colour.withMultipliedAlpha (isEnabled() ? 1.0f : 0.2f));
                g.fillRoundedRectangle (r, 4);
                g.setColour (Palette::controls.border.withAlpha (0.65f));
                g.drawRoundedRectangle (r, 4, 1);
                ksh::ui::text (g, getButtonText(), r.withTrimmedTop (r.getHeight() - 28), 13, !isEnabled() ? Palette::faint : getToggleState() || !black ? Palette::controls.app : Palette::text, true);
            }
        };
        class ModeButton : public juce::TextButton
        {
        public:
            ModeButton (const char* label, bool left) : juce::TextButton (label), leftSegment (left) {}

            void paintButton (juce::Graphics& g, bool hover, bool down) override
            {
                const auto bounds = getLocalBounds().toFloat();
                const auto accent = findColour (juce::TextButton::buttonOnColourId, true);
                LookAndFeel::paintToolbarSegment (g, bounds.reduced (0.5f), leftSegment, !leftSegment,
                                                  hover || down || hasKeyboardFocus (false), getToggleState(), accent);
                ksh::ui::text (g, getButtonText(), bounds, 14.0f, Palette::text, true);
            }
        private:
            const bool leftSegment;
        };
        static constexpr int notesPerOctave = 12;
        static constexpr int keyCount = 132;
        int visibleOctaves() const { return menuPage ? 2 : 4; }
        int visibleNotes() const { return visibleOctaves() * notesPerOctave; }
        int lastFirstNote() const { return keyCount - visibleNotes(); }
        int laidOutOctaves = 0;
        static constexpr double scrollDurationMs = 150.0;
        static constexpr int contentInset = 20, menuInset = 14;
        static constexpr int whiteKeyHeight = 116;
        static constexpr int allRowsPadding = 8;
        static constexpr int allRowsHeight = Layout::touchTarget + 2 * allRowsPadding;
        static constexpr std::array transposeOffsets { -12, -1, 1, 12 };
        static constexpr auto transposeLabel = "Transpose";
        juce::Component keyboard, keyStrip;
        std::array<PianoKey, keyCount> keys;
        std::array<juce::TextButton, transposeOffsets.size()> transposeButtons;
        juce::TextButton scalePresets { "Scale presets" };
        juce::TextButton previous { "<" }, next { ">" };
        ModeButton noteMode { "NOTE", true }, ccMode { "CC", false };
        ValueControl ccNumber, ccChannel;
        bool controllerMode = false;
        bool menuPage = false;
        juce::Rectangle<float> gridCells, gridDimmingBounds;
        juce::Rectangle<int> panel, modeBounds, allRowsLabelBounds, transposeLabelBounds;
        juce::Path panelOutline;
        melatonin::DropShadow panelGlow { juce::Colours::transparentBlack, 10, {}, 1 };
        melatonin::DropShadow anchorGlow { juce::Colours::transparentBlack, 6, {}, 1 };
        juce::Component::SafePointer<juce::Component> returnFocus;
        int candidate = 36, assignedNote = 36, firstNote = 24, lastScrubNote = -1;
        float displayedOctave = 2.0f, scrollStartOctave = 2.0f;
        double scrollStarted = 0.0;

        void updateController (bool enabled, int number, int channel)
        {
            if (onControllerChange)
                onControllerChange (enabled, number, channel);
        }

        void showScalePresets()
        {
            struct Preset
            {
                const char* label;
                RowNotes pitches;
                PresetOctave octave = PresetOctave::current;
            };
            static constexpr std::array presets {
                Preset { "Chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 } },
                Preset { "Ruismaker", { 49, 51, 54, 56, 58, 61, 63, 66, -1, -1, -1, -1, -1, -1, -1, -1 }, PresetOctave::fixed },
                Preset { "Major", { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26 } },
                Preset { "Natural minor", { 0, 2, 3, 5, 7, 8, 10, 12, 14, 15, 17, 19, 20, 22, 24, 26 } },
                Preset { "Dorian", { 0, 2, 3, 5, 7, 9, 10, 12, 14, 15, 17, 19, 21, 22, 24, 26 } },
                Preset { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10, 12, 14, 16, 17, 19, 21, 22, 24, 26 } },
                Preset { "Major pentatonic", { 0, 2, 4, 7, 9, 12, 14, 16, 19, 21, 24, 26, 28, 31, 33, 36 } },
                Preset { "Minor pentatonic", { 0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24, 27, 29, 31, 34, 36 } },
                Preset { "Whole tone", { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 } },
            };
            juce::PopupMenu menu;
            menu.setLookAndFeel (&getLookAndFeel());
            for (size_t i = 0; i < presets.size(); ++i)
                menu.addItem (static_cast<int> (i) + 1, presets[i].label);
            const juce::Component::SafePointer<NoteChooser> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scalePresets).withParentComponent (this).withStandardItemHeight (44),
                [safe] (int result)
                {
                    if (safe == nullptr || !safe->isVisible() || !safe->onPresetAll
                        || !juce::isPositiveAndBelow (result - 1, static_cast<int> (presets.size())))
                        return;
                    const auto& preset = presets[static_cast<size_t> (result - 1)];
                    auto notes = preset.pitches;
                    if (preset.octave == PresetOctave::current)
                        for (auto& note : notes)
                            note += Constants::defaultChannelNotes.front();
                    safe->onPresetAll (notes, preset.octave);
                });
        }
        void setCandidate (int note)
        {
            if (candidate == note)
                return;
            keys[static_cast<size_t> (candidate)].setToggleState (false, juce::dontSendNotification);
            candidate = note;
            keys[static_cast<size_t> (candidate)].setToggleState (true, juce::dontSendNotification);
            repaint();
        }

        void revealCandidate()
        {
            if (candidate < firstNote || candidate >= firstNote + visibleNotes())
                resetKeyboard();
        }

        void updateScrollButtons()
        {
            previous.setEnabled (firstNote > 0);
            next.setEnabled (firstNote < lastFirstNote());
        }

        void positionKeyboard()
        {
            const int left = juce::roundToInt (static_cast<double> (displayedOctave) * keyboard.getWidth() / visibleOctaves());
            keyStrip.setTopLeftPosition (-left, 0);
            const auto visible = keyboard.getLocalBounds().translated (left, 0);
            for (auto& key : keys)
                key.setVisible (visible.intersects (key.getBounds()));
        }

        void resetKeyboard()
        {
            stopTimer();
            firstNote = juce::jlimit (0, lastFirstNote(), (candidate / notesPerOctave - 1) * notesPerOctave);
            displayedOctave = static_cast<float> (firstNote / notesPerOctave);
            updateScrollButtons();
            positionKeyboard();
        }

        void scroll (int offset)
        {
            const int target = juce::jlimit (0, lastFirstNote(), firstNote + offset);
            if (firstNote == target)
                return;
            firstNote = target;
            scrollStartOctave = displayedOctave;
            scrollStarted = juce::Time::getMillisecondCounterHiRes();
            updateScrollButtons();
            startTimerHz (60);
        }

        void timerCallback() override
        {
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - scrollStarted;
            const float remaining = static_cast<float> (juce::jmax (0.0, 1.0 - elapsed / scrollDurationMs));
            displayedOctave = juce::jmap (1.0f - remaining * remaining * remaining, scrollStartOctave,
                                          static_cast<float> (firstNote / notesPerOctave));
            positionKeyboard();
            if (elapsed >= scrollDurationMs)
                stopTimer();
        }

        void scrub (juce::Point<int> position)
        {
            auto* key = dynamic_cast<PianoKey*> (getComponentAt (position));
            if (key == nullptr || !key->isEnabled())
            {
                lastScrubNote = -1;
                return;
            }
            const int midi = static_cast<int> (key - keys.data());
            if (midi == lastScrubNote)
                return;
            lastScrubNote = midi;
            choose (midi, false);
        }
        void choose (int midi, bool closeAfter = true)
        {
            if (!isVisible() || midi < 0 || midi > 127)
                return;
            setCandidate (assignedNote = midi);
            if (onChoose)
                onChoose (midi);
            if (closeAfter && !menuPage)
                dismiss();
        }
    };
}
