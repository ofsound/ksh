#include "KshNativeEditor.h"
#include "KshFooterMenu.h"
#include "KshUiScale.h"
#include "../engine/KshMath.h"
#include "../engine/KshMidiExport.h"
namespace ksh::ui
{
    namespace
    {
        constexpr auto backgroundBrightnessPreference = "backgroundBrightness";
        constexpr auto backgroundTintPreference = "backgroundTint";
        constexpr auto textBrightnessPreference = "textBrightness";
        constexpr auto show16RowsPreference = "show16Rows";
        constexpr int maxFeelPercent = 99;
        constexpr float gridHeaderValueFontSize = 22.0f;
        const juce::String padShortcutKeys { "ASDFGHJK" };
        constexpr std::array accentColourKeys { "orangeAccentColour", "blueAccentColour" };
        constexpr std::array legacyAccentModeKeys { "orangeAccentColourMode", "blueAccentColourMode" };
        constexpr std::array legacyAccentShadeKeys { "orangeAccentShade", "blueAccentShade" };

        juce::PopupMenu::Options editorPopupOptions (juce::Component& anchor)
        {
            anchor.getProperties().set ("mutedMenuHeader", true);
            return juce::PopupMenu::Options().withTargetComponent (anchor)
                .withParentComponent (anchor.getTopLevelComponent())
                .withMinimumWidth (juce::jmax (206, anchor.getWidth()))
                .withStandardItemHeight (44)
                .withMaximumNumColumns (1);
        }

        std::optional<juce::Colour> readColour (const juce::PropertiesFile& settings, const juce::String& key)
        {
            const auto saved = settings.getValue (key);
            if (saved.length() == 8 && saved.containsOnly ("0123456789abcdefABCDEF"))
                return juce::Colour::fromString (saved).withAlpha (1.0f);
            return std::nullopt;
        }

        std::optional<juce::Colour> readAccentColour (const juce::PropertiesFile& settings, size_t index)
        {
            if (const auto colour = readColour (settings, accentColourKeys[index]);
                colour && settings.getBoolValue (legacyAccentModeKeys[index], true))
                return colour;

            const int shade = juce::jlimit (-100, 100, settings.getIntValue (legacyAccentShadeKeys[index]));
            if (shade == 0)
                return std::nullopt;
            const auto base = index == 1 ? Palette::blueAccent : Palette::orangeAccent;
            const float lightness = base.getLightness();
            const float available = shade < 0 ? lightness : 1.0f - lightness;
            return base.withLightness (lightness + available * static_cast<float> (shade) / 200.0f);
        }

        std::shared_ptr<juce::PropertiesFile> sharedUiPreferences()
        {
            static std::weak_ptr<juce::PropertiesFile> shared;
            if (auto preferences = shared.lock())
                return preferences;

            juce::PropertiesFile::Options options;
            options.applicationName = "KSH";
            options.filenameSuffix = ".ui.settings";
            options.folderName = "ofsound";
            options.osxLibrarySubFolder = "Application Support";
            options.millisecondsBeforeSaving = -1;
            auto preferences = std::make_shared<juce::PropertiesFile> (options);
            shared = preferences;
            return preferences;
        }

        juce::String trackColourKey (int channel)
        {
            return "trackColour" + juce::String (channel + 1);
        }

        class TrackColourPicker final : public juce::Component, private juce::ChangeListener
        {
        public:
            TrackColourPicker (int track, std::shared_ptr<juce::PropertiesFile> settings)
                : channel (track), preferences (std::move (settings))
            {
                setWantsKeyboardFocus (true);
                setDescription ("Up and Down switch tracks.");
                heading.setFont (font (18, true));
                heading.setColour (juce::Label::textColourId, Palette::text);
                selector.setColour (juce::ColourSelector::backgroundColourId, Palette::shell);
                refreshTrack();
                selector.addChangeListener (this);
                for (auto* component : std::array<juce::Component*, 4> { &heading, &selector, &reset, &resetAll })
                    addAndMakeVisible (*component);
                reset.onClick = [this]
                {
                    selector.setCurrentColour (Palette::defaultTrackColours()[static_cast<size_t> (channel)], juce::dontSendNotification);
                    changeListenerCallback (&selector);
                    save();
                };
                resetAll.onClick = [this]
                {
                    const auto& defaults = Palette::defaultTrackColours();
                    selector.setCurrentColour (defaults[static_cast<size_t> (channel)], juce::dontSendNotification);
                    for (int ch = 0; ch < Constants::maxChannels; ++ch)
                        preferences->removeValue (trackColourKey (ch));
                    Palette::setTrackColours (defaults);
                    save();
                };
                setSize (290, 300);
            }

            ~TrackColourPicker() override
            {
                selector.removeChangeListener (this);
                changeListenerCallback (&selector);
                save();
            }

            std::function<void (int)> onNavigate;
            int getTrack() const { return channel; }
            void setTrack (int track)
            {
                changeListenerCallback (&selector);
                save();
                channel = track;
                refreshTrack();
            }

            bool keyPressed (const juce::KeyPress& key) override
            {
                if ((key != juce::KeyPress::upKey && key != juce::KeyPress::downKey) || !onNavigate)
                    return false;
                onNavigate (key == juce::KeyPress::upKey ? -1 : 1);
                return true;
            }

            void paint (juce::Graphics& g) override { g.fillAll (Palette::shell); }
            void resized() override
            {
                auto bounds = getLocalBounds().reduced (10);
                heading.setBounds (bounds.removeFromTop (28));
                auto actions = bounds.removeFromBottom (54);
                reset.setBounds (actions.removeFromLeft ((actions.getWidth() - 8) / 2));
                resetAll.setBounds (actions.withTrimmedLeft (8));
                selector.setBounds (bounds.withTrimmedBottom (6));
            }

        private:
            void refreshTrack()
            {
                const auto title = "Track " + juce::String (channel + 1) + " color";
                heading.setText (title, juce::dontSendNotification);
                setTitle (title);
                selector.setCurrentColour (Palette::channel (channel), juce::dontSendNotification);
            }

            void changeListenerCallback (juce::ChangeBroadcaster*) override
            {
                const auto colour = selector.getCurrentColour();
                auto colours = Palette::trackColours();
                auto& previous = colours[static_cast<size_t> (channel)];
                if (previous == colour)
                    return;
                previous = colour;
                if (colour == Palette::defaultTrackColours()[static_cast<size_t> (channel)])
                    preferences->removeValue (trackColourKey (channel));
                else
                    preferences->setValue (trackColourKey (channel), colour.toString());
                Palette::setTrackColours (colours);
                saveTimer.startTimer (180);
            }

            void save()
            {
                saveTimer.stopTimer();
                preferences->saveIfNeeded();
            }
            int channel;
            std::shared_ptr<juce::PropertiesFile> preferences;
            juce::Label heading;
            juce::ColourSelector selector { juce::ColourSelector::showColourAtTop | juce::ColourSelector::editableColour | juce::ColourSelector::showColourspace };
            juce::TextButton reset { "Reset" }, resetAll { "Reset All" };
            juce::TimedCallback saveTimer { [this] { save(); } };
        };

        class TrackColourCallout final : public juce::CallOutBox
        {
        public:
            using juce::CallOutBox::CallOutBox;

            bool canModalEventBeSentToComponent (const juce::Component* target) override
            {
                const auto* editor = getParentComponent();
                return target != nullptr && editor != nullptr && editor->isParentOf (target)
                    && dynamic_cast<const RowTriggerButton*> (target) != nullptr
                    && static_cast<bool> (target->getProperties()["rowNumber"])
                    && juce::ModifierKeys::getCurrentModifiersRealtime().isPopupMenu();
            }
        };

        class SettingsMenuRow final : public juce::PopupMenu::CustomComponent
        {
        public:
            SettingsMenuRow (const juce::String& label, std::optional<bool> checked, std::function<void()> action = {})
                : CustomComponent (false), button (label)
            {
                button.setClickingTogglesState (checked.has_value());
                button.setToggleState (checked.value_or (false), juce::dontSendNotification);
                if (action)
                    button.onClick = std::move (action);
                else
                    button.onClick = [this] { triggerMenuItem(); };
                addAndMakeVisible (button);
            }

            void focusButton()
            {
                if (auto* popup = juce::Component::getCurrentlyModalComponent(); popup != nullptr && popup->isParentOf (&button))
                {
                    popup->setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
                    if (button.isShowing())
                        button.grabKeyboardFocus();
                }
            }

        private:
            class MenuButton final : public juce::TextButton
            {
                static constexpr int padding = popupMenuRowPadding, checkboxSize = 16, checkboxGap = 10;

            public:
                using juce::TextButton::TextButton;

                int idealWidth() const
                {
                    return juce::GlyphArrangement::getStringWidthInt (font (labelFontSize), getButtonText())
                        + 2 * padding + checkboxSize + checkboxGap;
                }

                void paintButton (juce::Graphics& g, bool hovered, bool down) override
                {
                    const auto bounds = getLocalBounds().toFloat();
                    const bool checked = getToggleState();
                    const bool toggle = getClickingTogglesState();
                    const bool highlight = isEnabled() && (hovered || down);
                    auto content = bounds.reduced (static_cast<float> (padding), 0.0f);
                    const auto box = content.removeFromLeft (checkboxSize).withSizeKeepingCentre (checkboxSize, checkboxSize);
                    if (toggle)
                    {
                        if (checked)
                        {
                            const auto fill = highlight ? Palette::accent.brighter (0.15f) : Palette::accent;
                            g.setColour (fill);
                            g.fillRoundedRectangle (box, 3.0f);
                            juce::Path tick;
                            tick.startNewSubPath (box.getX() + 4.0f, box.getCentreY());
                            tick.lineTo (box.getX() + 7.0f, box.getCentreY() + 3.0f);
                            tick.lineTo (box.getRight() - 3.0f, box.getCentreY() - 3.0f);
                            g.setColour (fill.contrasting());
                            g.strokePath (tick, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                        }
                        g.setColour (highlight ? Palette::text : checked ? Palette::accent : Palette::muted.withAlpha (0.75f));
                        g.drawRoundedRectangle (box.reduced (0.5f), 2.5f, 1.0f);
                    }
                    const auto labelColour = checked ? Palette::white : toggle ? Palette::muted : Palette::text;
                    const auto ink = !isEnabled() ? Palette::faint
                        : highlight ? labelColour.interpolatedWith (Palette::white, 0.45f) : labelColour;
                    ksh::ui::text (g, getButtonText(), content.withTrimmedLeft (checkboxGap), labelFontSize,
                                   ink, false, juce::Justification::centredLeft);
                    if (showKeyboardFocus && hasKeyboardFocus (false))
                    {
                        g.setColour (Palette::muted.withAlpha (0.7f));
                        g.drawRoundedRectangle (bounds.reduced (6.5f, 1.5f), 4.0f, 1.0f);
                    }
                }

                void focusGained (FocusChangeType cause) override
                {
                    showKeyboardFocus = cause == focusChangedByTabKey;
                    juce::TextButton::focusGained (cause);
                }

                void mouseDown (const juce::MouseEvent& event) override
                {
                    if (std::exchange (showKeyboardFocus, false))
                        repaint();
                    juce::TextButton::mouseDown (event);
                }

                bool keyPressed (const juce::KeyPress& key) override
                {
                    if (key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::downKey))
                    {
                        moveKeyboardFocusToSibling (key.isKeyCode (juce::KeyPress::downKey));
                        return true;
                    }
                    if (key.isKeyCode (juce::KeyPress::spaceKey))
                    {
                        triggerClick();
                        return true;
                    }
                    return juce::TextButton::keyPressed (key);
                }

            private:
                bool showKeyboardFocus = false;
            };

            void getIdealSize (int& width, int& height) override
            {
                width = button.idealWidth();
                height = 44;
            }

            void resized() override { button.setBounds (getLocalBounds()); }

            MenuButton button;
        };

        void paintControlGroup (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title, float horizontalPadding = 8.0f)
        {
            g.setColour (Palette::border.withAlpha (0.65f));
            g.drawRoundedRectangle (bounds, 5, 1);
            text (g, title, bounds.reduced (horizontalPadding, 3).withHeight (22), 16, Palette::muted, true, juce::Justification::centredLeft);
        }
    }
    const std::array<juce::String, 8> NativeEditor::rateNames { "Quarter", "Quarter triplet", "Eighth", "Eighth triplet", "16th", "16th triplet", "32nd", "32nd triplet" };

    int NativeEditor::rateControlWidth()
    {
        static const int width = []
        {
            int widest = 0;
            for (const auto& name : rateNames)
                widest = juce::jmax (widest, dropdownWidth (name, gridHeaderValueFontSize));
            return widest;
        }();
        return width;
    }

    void NativeEditor::dragPattern (int source)
    {
       #if !JUCE_IOS && !JUCE_ANDROID
        controller.commit();
        const auto name = projectTitle() + " - P" + juce::String (source + 1);
        // Keep the source after the drag ends: some hosts import asynchronously or retain its path.
        // The OS temporary directory owns cleanup; each drag gets an immutable, unique file.
        const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("ofsound-KSH-MIDI")
                                   .getChildFile (juce::Uuid().toString());
        const auto file = directory.getChildFile (juce::File::createLegalFileName (name).substring (0, 120) + ".mid");
        auto result = directory.createDirectory();
        if (result.wasOk())
            result = writePatternMidiFile (controller.processor.enginePlaybackSnapshot(), source, file, name);
        if (result.wasOk()
            && !juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false,
                                                                           &patterns[static_cast<size_t> (source)]))
            result = juce::Result::fail ("The MIDI file was created, but the system could not start the drag. Try dragging the pattern again.");
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI export", result.getErrorMessage());
       #else
        juce::ignoreUnused (source);
       #endif
    }

    NativeEditor::NativeEditor (PluginProcessor& processor, std::shared_ptr<juce::PropertiesFile> preferences, [[maybe_unused]] bool windowCanGrow)
        : controller (processor), uiPreferences (preferences ? std::move (preferences) : sharedUiPreferences()),
       #if JUCE_MAC
          allowTallMode (windowCanGrow),
       #endif
          grid (controller), preview (controller), randomPanel (controller, true), projectBrowser (*uiPreferences, processor)
    {
       #if JUCE_IOS
        layout.touchRows = true;
       #endif
        Palette::setBackgroundTone ({ uiPreferences->getIntValue (backgroundBrightnessPreference),
                                      uiPreferences->getIntValue (backgroundTintPreference) });
        Palette::setTextBrightness (uiPreferences->getIntValue (textBrightnessPreference));
        look.refreshPalette();
        footerHelp.refreshInk();
        Palette::setAccentPreferences (uiPreferences->getBoolValue ("blueAccent", true),
                                       { readAccentColour (*uiPreferences, 0), readAccentColour (*uiPreferences, 1) });
        auto trackColours = Palette::defaultTrackColours();
        for (int ch = 0; ch < Constants::maxChannels; ++ch)
            if (const auto saved = readColour (*uiPreferences, trackColourKey (ch)))
                trackColours[static_cast<size_t> (ch)] = *saved;
        Palette::setTrackColours (trackColours);
        footerHelp.setHelpEnabled (uiPreferences->getBoolValue ("footerTooltips", true));
        layout.reverseRows = uiPreferences->getBoolValue ("reverseRows", false);
        layout.firstRow = layout.reverseRows ? layout.maxFirstRow() : 0;
        layout.showSwing = uiPreferences->getBoolValue ("showSwing", true);
        controller.keyboardModifiersEnabled = uiPreferences->getBoolValue ("keyboardModifiersEnabled", true);
        addChildComponent (modifierToggle);
        addChildComponent (midiOutput);
        midiOutput.setTitle ("MIDI output destination");
        midiOutput.getProperties().set ("footerControl", true);
        lastMidiActivity = processor.getStandaloneMidiActivity();
        midiOutput.onClick = [this]
        {
            if (controller.processor.showStandaloneMidiOutputMenu)
                controller.processor.showStandaloneMidiOutputMenu (midiOutput);
        };
        modifierToggle.setTitle ("Shift/Option shortcuts");
        modifierToggle.setTooltip ("Toggle Shift and Option shortcuts");
        modifierToggle.onClick = [this] { toggleKeyboardModifiers(); };
        controller.graphicalCellValues = uiPreferences->getBoolValue ("graphicalCellValues", true);
        addChildComponent (cellDisplay);
        cellDisplay.setTitle ("Show values");
        cellDisplay.setTooltip ("Switch between numbers and graphs");
        cellDisplay.onClick = [this]
        {
            controller.graphicalCellValues = !controller.graphicalCellValues;
            uiPreferences->setValue ("graphicalCellValues", controller.graphicalCellValues);
            uiPreferences->saveIfNeeded();
            sync();
        };
        addChildComponent (rowOrder);
        addChildComponent (show16Rows);
        show16Rows.setTitle ("16 Tracks");
        show16Rows.setToggleable (true);
        show16Rows.setTooltip ("Show 16 tracks and pads and generate on all 16 tracks");
        show16Rows.onClick = [this]
        {
            uiPreferences->setValue (show16RowsPreference, layout.rowLimit != Constants::maxChannels);
            uiPreferences->saveIfNeeded();
            sync();
        };
        rowOrder.setTitle ("Reverse row order");
        rowOrder.onClick = [this]
        {
            cancelCompactGestures();
            layout.reverseRows = !layout.reverseRows;
            layout.firstRow = layout.maxFirstRow() - layout.firstRow;
           #if JUCE_MAC
            if (layout.tallMode)
                firstRowBeforeTallMode = juce::jmax (0, layout.channels - Layout::defaultVisibleRows - firstRowBeforeTallMode);
           #endif
            uiPreferences->setValue ("reverseRows", layout.reverseRows);
            uiPreferences->saveIfNeeded();
            sync();
            repaint();
        };
        addAndMakeVisible (footerHelp);
        setLookAndFeel (&look);
        setOpaque (true);
        setWantsKeyboardFocus (true);
        addMouseListener (this, true);
        addAndMakeVisible (grid);
        grid.rowTriggered = [this] (int channel) { triggerPad (channel); };
        grid.trackColourRequested = [this] (int channel, juce::Component& track)
        {
            const auto anchor = getLocalArea (&track, track.getLocalBounds());
            if (trackColourPopup != nullptr && trackColourPopup->isVisible())
            {
                auto& picker = static_cast<TrackColourPicker&> (*trackColourContent);
                picker.setTrack (channel);
                trackColourPopup->updatePosition (anchor, getLocalBounds());
                if (picker.isShowing())
                    picker.grabKeyboardFocus();
                return;
            }

            trackColourPopup.reset();
            auto picker = std::make_unique<TrackColourPicker> (channel, uiPreferences);
            auto* content = picker.get();
            content->onNavigate = [this, content] (int delta)
            {
                const int row = layout.displayRow (content->getTrack()) + delta;
                if (!juce::isPositiveAndBelow (row, layout.channels))
                    return;
                const int next = layout.channelForRow (row);
                if (!layout.channelVisible (next))
                {
                    layout.firstRow = layout.firstRowToReveal (next);
                    sync();
                }
                if (auto* nextTrack = grid.rowTitle (next))
                {
                    content->setTrack (next);
                    if (auto* callout = dynamic_cast<juce::CallOutBox*> (content->getParentComponent()))
                        callout->updatePosition (getLocalArea (nextTrack, nextTrack->getLocalBounds()), getLocalBounds());
                }
            };
            trackColourContent = std::move (picker);
            trackColourPopup = std::make_unique<TrackColourCallout> (*content, anchor, this);
            trackColourPopup->setAlwaysOnTop (true);
            trackColourPopup->setDismissalMouseClicksAreAlwaysConsumed (true);
            trackColourPopup->enterModalState (false);
            if (content->isShowing())
                content->grabKeyboardFocus();
        };
        addChildComponent (noteChooser);
        grid.noteChooserRequested = [this] (int ch, juce::Component& input) { showNoteChooser (ch, input); };
        noteChooser.onDismiss = [this]
        {
            grid.setRowOutlinesVisible (true);
            if (layout.phoneRowControls)
            {
                if (panel == Panel::channel)
                    panel = Panel::none;
                sync();
                repaint();
            }
        };
        noteChooser.onChoose = [this] (int note)
        {
            controller.refresh();
            if (noteChooserMatches() && !noteChooserOriginal.controller)
            {
                // Accept subsequent scrub notes, while still rejecting external edits.
                const juce::ScopedValueSetter<bool> editing (noteChooserEditing, true);
                controller.command ("channel_note", { noteChooserChannel + 1, note }, "Change channel note");
                noteChooserOriginal.note = note;
                (void) controller.processor.auditionRowFromUi (noteChooserChannel);
            }
        };
        noteChooser.onControllerChange = [this] (bool enabled, int number, int midiChannel)
        {
            controller.refresh();
            if (!noteChooserMatches())
                return;
            const auto& original = noteChooserOriginal;
            if (enabled == original.controller && number == original.ccNumber
                && midiChannel == original.ccChannel)
                return;
            const juce::ScopedValueSetter<bool> editing (noteChooserEditing, true);
            controller.command ("channel_cc", { noteChooserChannel + 1, enabled ? 1 : 0, number, midiChannel },
                                enabled == original.controller ? "Change CC settings" : "Change row MIDI mode");
            noteChooserOriginal = controller.state.playback.channels[static_cast<size_t> (noteChooserChannel)];
            noteChooser.setRowOutput (noteChooserOriginal);
        };
        noteChooser.onNavigate = [this] (int delta)
        {
            const int row = layout.displayRow (noteChooserChannel) + delta;
            if (!juce::isPositiveAndBelow (row, layout.channels))
                return;
            const int channel = layout.channelForRow (row);
            if (!layout.channelVisible (channel))
            {
                layout.firstRow = layout.firstRowToReveal (channel);
                sync();
            }
            if (layout.compact)
            {
                selectedChannel = channel;
                syncChannel();
                showNoteChooser (channel, channelNote);
            }
            else
                showNoteChooser (channel, grid.rowNoteInput (channel));
        };
        noteChooser.onTransposeAll = [this] (int offset)
        {
            controller.refresh();
            NoteChooser::RowNotes notes;
            const auto& channels = controller.state.playback.channels;
            std::transform (channels.begin(), channels.end(), notes.begin(), [offset] (const auto& channel) { return channel.note + offset; });
            applyNoteChooserPreset (notes, "Transpose all row notes");
        };
        noteChooser.onPresetAll = [this] (NoteChooser::RowNotes notes, NoteChooser::PresetOctave octave)
        {
            controller.refresh();
            if (octave == NoteChooser::PresetOctave::current)
            {
                constexpr int octaveSize = 12;
                const auto& playback = controller.state.playback;
                int totalDifference = 0, noteRows = 0;
                for (int row = 0; row < playback.channelCount; ++row)
                {
                    const auto index = static_cast<size_t> (row);
                    if (!playback.channels[index].controller)
                    {
                        totalDifference += playback.channels[index].note - notes[index];
                        ++noteRows;
                    }
                }
                // One shared shift keeps the preset voicing intact while minimizing pitch movement across visible rows.
                const auto [lowest, highest] = std::minmax_element (notes.begin(), notes.end());
                const auto nearestOctave = std::lround (static_cast<double> (totalDifference) / (juce::jmax (1, noteRows) * octaveSize));
                const int shift = juce::jlimit (-(*lowest / octaveSize), (127 - *highest) / octaveSize,
                                               static_cast<int> (nearestOctave)) * octaveSize;
                for (auto& note : notes)
                    note += shift;
            }
            else
            {
                const auto& channels = controller.state.playback.channels;
                for (size_t row = 0; row < notes.size(); ++row)
                    if (notes[row] < 0)
                        notes[row] = channels[row].note;
            }
            applyNoteChooserPreset (notes, "Apply scale preset");
        };
        addAndMakeVisible (preview);
        addChildComponent (aboutViewport);
        aboutViewport.setViewedComponent (&randomPanel, false);
        aboutViewport.setScrollBarsShown (true, false);
        preview.onTextBrightnessEdited = [this] (int value)
        {
            auto theme = currentTheme();
            theme.textBrightness = value;
            editTheme (theme);
        };
        preview.onThemeGesture = [this] (bool started)
        {
            if (started)
                themeBeforeGesture = currentTheme();
            else
                finishThemeGesture();
        };
        preview.onAccentSelected = [this] (bool blue)
        {
            auto theme = currentTheme();
            theme.blue = blue;
            editTheme (theme);
        };
        preview.onAccentColourEdited = [this] (bool blue, std::optional<juce::Colour> colour)
        {
            auto theme = currentTheme();
            theme.colours[blue ? 1 : 0] = colour;
            editTheme (theme);
        };
        preview.onBackgroundToneEdited = [this] (Palette::BackgroundTone tone)
        {
            auto theme = currentTheme();
            theme.backdrop = tone;
            editTheme (theme);
        };
        const std::array<juce::String, 6> projectLabels { "NEW", "LOAD", "SAVE", juce::String::fromUTF8 ("◀"), juce::String::fromUTF8 ("▶"), "SAVE AS" };
        const std::array<juce::String, 6> projectTips { "", "", "", "Previous project in this folder", "Next project in this folder", "" };
        for (int i = 0; i < static_cast<int> (projectButtons.size()); ++i)
        {
            auto& button = projectButtons[static_cast<size_t> (i)];
            if (i != 3 && i != 4)
                button.getProperties().set ("insetProjectArtwork", true);
            addAndMakeVisible (button);
            button.setButtonText (projectLabels[static_cast<size_t> (i)]);
            button.setTooltip (projectTips[static_cast<size_t> (i)]);
            button.onClick = [this, i] { controller.commit(); if (projectAction) projectAction (i); };
        }
        addAndMakeVisible (projectHeader);
        projectHeader.onClick = projectButtons[5].onClick;
        addChildComponent (projectBrowser);
        projectBrowser.onDismiss = [this] (const juce::File& folder)
        {
            if (projectBrowserDismissed) projectBrowserDismissed (folder);
            sync();
            repaint();
            if (isShowing()) grabKeyboardFocus();
        };
        addChildComponent (discardChanges);
        discardChanges.onClose = [this]
        {
            if (layout.compact)
                resized();
            repaint();
        };
        projectBrowser.onRestore = [this]
        {
            if (projectAction) projectAction (restorePreviousProjectAction);
        };
       #if JUCE_IOS
        projectBrowser.onShare = [this] { if (projectAction) projectAction (6); };
       #endif
        addChildComponent (recordingStrip);
        recordingStrip.setTitle ("Performance pads");
        controller.onPlaybackHit = [this] (int channel, int velocity)
        {
            if (recordingStrip.isVisible() && juce::isPositiveAndBelow (channel, layout.channels))
                recordingPads[static_cast<size_t> (channel)].flash (velocity);
        };
        controller.processor.getUiBridge().onMidiInputNote = [this] (int note, int velocity, double timestampMs)
        {
            if (!recordingStrip.isVisible())
                return;
            const auto& playback = controller.state.playback;
            for (size_t index = 0; index < static_cast<size_t> (layout.channels); ++index)
            {
                if (playback.channels[index].note == note && recordingPads[index].isEnabled())
                    recordingPads[index].flash (velocity, timestampMs);
            }
        };
        for (int channel = 0; channel < static_cast<int> (recordingPads.size()); ++channel)
        {
            auto& pad = recordingPads[static_cast<size_t> (channel)];
            recordingStrip.addAndMakeVisible (pad);
            pad.onClick = [this, channel]
            {
                const auto& pressedPad = recordingPads[static_cast<size_t> (channel)];
                triggerPad (channel, pressedPad.pressedVelocity(), pressedPad.pressedTouchTime());
            };
        }
        recordingStrip.addAndMakeVisible (padVelocityMode);
        padVelocityMode.setTitle ("Pad velocity mode");
        padVelocityMode.setTooltip ("Hit higher on a pad for more velocity");
        padVelocityMode.setToggleState (uiPreferences->getBoolValue ("padVelocityMode", false), juce::dontSendNotification);
        padVelocityMode.onClick = [this]
        {
            uiPreferences->setValue ("padVelocityMode", padVelocityMode.getToggleState());
            uiPreferences->saveIfNeeded();
            syncRecordingPads();
        };
        for (auto* b : { &mutePattern, &velocityLayer, &clear, &record, &left, &right, &undoButton, &redoButton, &rate, &play })
            addAndMakeVisible (*b);
        addChildComponent (dc);
        addAndMakeVisible (chanceLayer);
        for (juce::TextButton* button : std::array<juce::TextButton*, 4> { &generate, &jamButton, &showPads, &metronome })
        {
            addAndMakeVisible (*button);
            button->setClickingTogglesState (true);
            button->getProperties().set ("outlinedIcon", true);
        }
        generate.setButtonText ("Generate");
        generate.setTitle ("Generate");
        generate.getProperties().set ("icon", "computer");
        generate.setTooltip ("Show or hide the pattern generator");
        generate.setClickingTogglesState (false);
        generate.onClick = [this]
        {
            if (jamTarget)
                return;
            if (layout.compact)
            {
                if (showingCompactPadPreview())
                    showPads.setToggleState (false, juce::dontSendNotification);
                showPanel (panel == Panel::generate ? Panel::none : Panel::generate);
            }
            else
                showDesktopRandomizer (!layout.randomVisible);
        };
        jamButton.setButtonText ("Jam");
        jamButton.setTitle ("Jam");
        jamButton.getProperties().set ("icon", "jam");
        jamButton.setClickingTogglesState (false);
        jamButton.setTriggeredOnMouseDown (true);
        jamButton.onClick = [this] { toggleJam(); };
        recordingStrip.addChildComponent (finishJamButton);
        finishJamButton.setTitle ("Finish Jam");
        finishJamButton.setTooltip ("Tap on the next downbeat to close the loop");
        finishJamButton.getProperties().set ("jamStopPad", true);
        finishJamButton.setAppearance (Palette::recording, {}, {});
        finishJamButton.onClick = [this] { if (jamTarget) toggleJam(); };
        addChildComponent (footerJamPad);
        footerJamPad.setButtonText ("STOP");
        footerJamPad.setTitle (finishJamButton.getTitle());
        footerJamPad.setTooltip (finishJamButton.getTooltip());
        footerJamPad.setAppearance (Palette::recording, {}, {});
        footerJamPad.onClick = finishJamButton.onClick;
        showPads.getProperties().set ("icon", "pads");
        showPads.setTitle ("Show pads");
        showPads.onClick = [this]
        {
            preview.cancelGesture();
            if (layout.compact && panel == Panel::generate)
            {
                showPanel (Panel::none);
                return;
            }
            if (layout.phoneRowControls)
                grid.cancelGesture();
            sync();
        };
        metronome.getProperties().set ("icon", "metronome");
        metronome.getProperties().set ("transport", true);
        metronome.setTitle ("Metronome");
        metronome.setTooltip ("Right-click or hold for metronome settings");
        metronome.openSettingsOnHold = true;
        metronome.showSettings = [this] { metronomeMenu (metronome); };
        metronome.onClick = [this] { controller.processor.setStandaloneMetronomeEnabled (metronome.getToggleState()); };
        if (processor.hasStandaloneTransport())
        {
            processor.setMetronomeSound (static_cast<PluginProcessor::MetronomeSound> (juce::jlimit (0, 2, uiPreferences->getIntValue ("metronomeSound", 0))));
            processor.setMetronomePitch (uiPreferences->getIntValue ("metronomePitch", 0));
            processor.setMetronomeVolume (uiPreferences->getIntValue ("metronomeVolume", 100));
        }
        addAndMakeVisible (selectMode);
        addAndMakeVisible (stop);
        for (auto* button : { &play, &stop })
        {
            button->getProperties().set ("transport", true);
            button->getProperties().set ("icon", button == &play ? "play" : "stop");
            button->setTitle (button == &play ? "Play" : "Stop");
            button->setTooltip ("Space to play or stop");
        }
        selectMode.getProperties().set ("selectMode", true);
        selectMode.setTitle ("Select modifier");
        selectMode.onClick = [this] { controller.selectLatched = !controller.selectLatched; controller.notify(); };
        configureSelectButton();
        for (int i = 0; i < 16; ++i)
        {
            auto& b = patterns[static_cast<size_t> (i)];
            addAndMakeVisible (b);
            b.setButtonText (juce::String (i + 1));
            b.getProperties().set ("patternNumber", true);
            b.onStateChange = [this, &b]
            {
                if (b.copyDestination)
                    repaint (b.getBounds().expanded (patternGlowExtent));
            };
            b.setTooltip ("Shift-click to copy, then pick a slot");
            b.onClick = [this, i]
            {
                if (controller.selectHeld)
                    controller.selectUsed = true;
                controller.selectSource (i, controller.keyboardModifiersEnabled
                                             && juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown());
            };
           #if !JUCE_IOS && !JUCE_ANDROID
            b.setTooltip (b.getTooltip() + ". Drag to DAW to export.");
            b.onDrag = [this, i] { dragPattern (i); };
           #endif
        }
        velocityLayer.getProperties().set ("layerSegment", 0);
        chanceLayer.getProperties().set ("layerSegment", 1);
        undoButton.getProperties().set ("historySegment", 0);
        redoButton.getProperties().set ("historySegment", 1);
        const std::array<std::pair<juce::TextButton*, const char*>, 8> icons { { { &clear, "clear" }, { &record, "record" }, { &left, "previous" }, { &right, "next" }, { &undoButton, "undo" }, { &redoButton, "redo" }, { &projectButtons[3], "projectPrevious" }, { &projectButtons[4], "projectNext" } } };
        for (auto [button, icon] : icons)
        {
            button->getProperties().set ("icon", icon);
            button->setTitle (icon);
        }
        for (auto* button : { &record, &left, &right, &clear })
            button->getProperties().set ("outlinedIcon", true);
        mutePattern.onClick = [this] { controller.selectSource (-1); };
        mutePattern.getProperties().set ("patternNumberFont", true);
        mutePattern.setTooltip ("Mute pattern playback");
        for (juce::TextButton* button : { &velocityLayer, static_cast<juce::TextButton*> (&chanceLayer) })
            button->onClick = [this, button]
            {
                controller.commit();
                controller.selected.reset();
                controller.layer = button == &velocityLayer ? Layer::velocity : Layer::chance;
                controller.selectLatched = false;
                sync();
            };
        chanceLayer.pressed = [this] (const juce::MouseEvent& e)
        {
            if (!e.mods.isLeftButtonDown() || e.mods.isPopupMenu())
                return;
            controller.chanceHeld = true;
            controller.chanceUsed = false;
            controller.notify();
        };
        chanceLayer.released = [this] (const juce::MouseEvent& e)
        {
            if (!std::exchange (controller.chanceHeld, false))
                return;
            if (!controller.chanceUsed && e.getLengthOfMousePress() < 300
                && chanceLayer.getLocalBounds().contains (e.getPosition()))
                chanceLayer.onClick();
            else
                controller.notify();
        };
        grid.layerChanged = [this] { updateLayerHeading(); };
        addAndMakeVisible (layerHint);
        layerHint.setFont (font (12, true));
        layerHint.setColour (juce::Label::textColourId, Palette::accent);
        layerHint.setBorderSize ({});
        layerHint.setJustificationType (juce::Justification::centredLeft);
        layerHint.setInterceptsMouseClicks (false, false);
        chanceLayer.setTooltip ("Hold here or hold Option for temporary Chance");
        clear.setTooltip ("Double-click to clear pattern");
        clear.onClick = [this]
        {
            const int source = controller.source();
            if (source < 0)
                return;
            const auto epoch = controller.processor.getUiBridge().getDocumentEpoch();
            if (source != clearSource || epoch != clearEpoch)
                patternClearFlash.cancel();
            clearSource = source;
            clearEpoch = epoch;
            if (patternClearFlash.confirm())
                controller.clearPattern();
        };
        record.setTitle ("Arm Record");
        record.setTooltip (processor.hasStandaloneTransport() ? "Record MIDI, pads, or " + padShortcutKeys
                                                              : "Record incoming MIDI or play the pads");
        record.onClick = [this] { controller.command ("pattern_record_enabled", { !controller.processor.isPatternRecordingEnabled(), controller.source() + 1 }); };
        addAndMakeVisible (recordQuantize);
        recordQuantize.getProperties().set ("icon", "recordLock");
        recordQuantize.getProperties().set ("outlinedIcon", true);
        recordQuantize.setClickingTogglesState (true);
        recordQuantize.onClick = [this]
        {
            controller.processor.setPatternRecordingQuantized (recordQuantize.getToggleState());
            sync();
        };
        recordQuantize.showSettings = [this] { recordingMenu(); };
        left.onClick = [this] { controller.shift (-1, -1); };
        right.onClick = [this] { controller.shift (-1, 1); };
        left.setTooltip ("Shift rows left within their loops");
        right.setTooltip ("Shift rows right within their loops");
        undoButton.onClick = [this] { controller.undo(); };
        redoButton.onClick = [this] { controller.redo(); };
        undoButton.setTooltip (juce::String::fromUTF8 ("Undo • Cmd-Z"));
        redoButton.setTooltip (juce::String::fromUTF8 ("Redo • Cmd-Shift-Z"));
        rate.getProperties().set ("dropdown", true);
        rate.getProperties().set ("gridHeaderValue", true);
        rate.setTitle ("Pattern rate");
        rate.onClick = [this] { rateMenu(); };
        play.onClick = [this] { controller.command ("standalone_transport_playing", { true }); };
        stop.onClick = [this]
        {
            jamPlayback = JamPlayback::stopped;
            globalFocusChanged (getCurrentlyFocusedComponent());
            controller.command ("standalone_transport_playing", { false });
        };
        dc.onClick = [this] { controller.drumColors = ! controller.drumColors; sync(); };
        configureValue (swing, "Amount", 0, maxFeelPercent, "swing");
        configureValue (subdivision, "Target", 0, Constants::swingSubdivisionCount - 1, "swing_subdivision");
        swing.setTitle ("Swing amount");
        swing.setTooltip (juce::String::fromUTF8 ("Double-click to type • Right-click or touch and hold to choose Regular or MPC scale"));
        swing.onContextMenu = [this] { swingMenu(); };
        updateSwingScale();
        addAndMakeVisible (showSwing);
        showSwing.getProperties().set ("icon", "eye");
        showSwing.setTitle ("Show swing on grid");
        showSwing.setTooltip ("Grid display only");
        showSwing.setClickingTogglesState (true);
        showSwing.onClick = [this]
        {
            layout.showSwing = showSwing.getToggleState();
            grid.cancelGesture();
            uiPreferences->setValue ("showSwing", layout.showSwing);
            uiPreferences->saveIfNeeded();
            sync();
        };
        subdivision.setTitle ("Swing target");
        subdivision.setTooltip ("Swing groups of 1 or 2 steps");
        subdivision.format = [] (int v) { return juce::String (static_cast<int> (swingSubdivisionValueForIndex (v))) + "x"; };
        subdivision.resetValue = Constants::defaultSwingSubdivisionIndex;
        configureValue (velocityHumanize, "Velocity", 0, maxFeelPercent, "velocity_humanize");
        configureValue (timingHumanize, "Timing", 0, maxFeelPercent, "timing_humanize");
        velocityHumanize.setTitle ("Velocity humanization");
        timingHumanize.setTitle ("Timing humanization");
        velocityHumanize.setTooltip ("Random velocity variation per hit");
        timingHumanize.setTooltip ("Random timing variation up to 1/5 of a step");
        for (auto* control : { &swing, &velocityHumanize, &timingHumanize })
        {
            control->suffix = "%";
            control->reserveDigitSlots = false;
            control->limitDisplayedValue = true;
        }
        configureValue (steps, "Grid Length", 1, Constants::maxSteps, "source_steps");
        steps.getProperties().set ("gridHeaderValue", true);
        steps.captionFontSize = 12;
        steps.captionColour = Palette::muted;
        steps.resetValue = 16;
        addChildComponent (scale);
        scale.setTitle ("UI scale percent");
        scale.setTooltip ("Double-click to enter a scale");
        scale.minimum = UiScale::minimumPercent;
        scale.maximum = UiScale::maximumPercent;
        scale.resetValue = 100;
        scale.suffix = "%";
        scale.valueFontSize = 18;
        addChildComponent (linkSettings);
        linkSettings.getProperties().set ("footerControl", true);
        linkSettings.setTitle ("Ableton Link settings");
        linkSettings.onClick = [this]
        {
            if (controller.processor.showStandaloneLinkSettings)
                controller.processor.showStandaloneLinkSettings (linkSettings);
        };
        addAndMakeVisible (settings);
        settings.setTitle ("Settings");
        settings.setTooltip ("Settings");
        settings.getProperties().set ("icon", "gear");
        settings.onClick = [this] { settingsMenu(); };
        dc.setTitle ("Channel colors");
        settings.getProperties().set ("footerControl", true);
        scale.getProperties().set ("footerControl", true);
        scale.onChange = [this] (int v) { controller.processor.setProjectUiScalePercent (v); if (preferredSizeChanged) preferredSizeChanged(); };
        controller.changed = [this] { sync(); };
        setupCompactControls();
        uiPreferences->addChangeListener (this);
        Palette::changes().addChangeListener (this);
        changeListenerCallback (&Palette::changes());
        sync();
        if (layout.standalone)
            juce::Desktop::getInstance().addFocusChangeListener (this);
        startTimerHz (60);
    }
    NativeEditor::~NativeEditor()
    {
        stopTimer();
        juce::Desktop::getInstance().removeFocusChangeListener (this);
        if (jamTarget)
            controller.processor.cancelJamCapture();
        jamTarget.reset();
        jamPlayback = JamPlayback::stopped;
        heldJamStopKey.reset();
        globalFocusChanged (nullptr);
        uiPreferences->removeChangeListener (this);
        Palette::changes().removeChangeListener (this);
        removeMouseListener (this);
        juce::PopupMenu::dismissAllActiveMenus();
        trackColourPopup.reset();
        trackColourContent.reset();
        controller.changed = {};
        controller.onPlaybackHit = {};
        controller.processor.getUiBridge().onMidiInputNote = {};
        setLookAndFeel (nullptr);
    }
    NativeEditor::Theme NativeEditor::currentTheme()
    {
        return { Palette::blueAccentSelected(), { Palette::accentOverride (false), Palette::accentOverride (true) },
                 Palette::backgroundTone(), Palette::textBrightness() };
    }
    void NativeEditor::applyTheme (const Theme& theme, juce::PropertiesFile& preferences)
    {
        const auto previous = currentTheme();
        if (previous.blue != theme.blue)
            preferences.setValue ("blueAccent", theme.blue);
        for (size_t i = 0; i < theme.colours.size(); ++i)
        {
            if (previous.colours[i] == theme.colours[i])
                continue;
            if (theme.colours[i])
                preferences.setValue (accentColourKeys[i], theme.colours[i]->toString());
            else
                preferences.removeValue (accentColourKeys[i]);
            preferences.removeValue (legacyAccentModeKeys[i]);
            preferences.removeValue (legacyAccentShadeKeys[i]);
        }
        Palette::setAccentPreferences (theme.blue, theme.colours);
        Palette::setBackgroundTone (theme.backdrop);
        Palette::setTextBrightness (theme.textBrightness);
        if (previous.textBrightness != Palette::textBrightness())
            preferences.setValue (textBrightnessPreference, Palette::textBrightness());
        const auto appliedTone = Palette::backgroundTone();
        if (previous.backdrop.brightness != appliedTone.brightness)
            preferences.setValue (backgroundBrightnessPreference, appliedTone.brightness);
        if (previous.backdrop.tint != appliedTone.tint)
            preferences.setValue (backgroundTintPreference, appliedTone.tint);
    }
    void NativeEditor::editTheme (const Theme& theme)
    {
        const bool singleEdit = !themeBeforeGesture;
        if (singleEdit)
            themeBeforeGesture = currentTheme();
        applyTheme (theme, *uiPreferences);
        if (singleEdit)
            finishThemeGesture();
    }
    void NativeEditor::finishThemeGesture()
    {
        const auto startingTheme = std::exchange (themeBeforeGesture, std::nullopt);
        const auto after = currentTheme();
        uiPreferences->saveIfNeeded();
        if (!startingTheme || *startingTheme == after)
            return;
        controller.recordUiEdit ("Change interface colors", [prefs = uiPreferences, before = *startingTheme, after] (bool redoing)
        {
            const auto& from = redoing ? before : after;
            const auto& to = redoing ? after : before;
            auto theme = currentTheme();
            const auto merge = [] (auto& value, const auto& original, const auto& replacement)
            {
                if (value == original)
                    value = replacement;
            };
            merge (theme.blue, from.blue, to.blue);
            for (size_t i = 0; i < theme.colours.size(); ++i)
                merge (theme.colours[i], from.colours[i], to.colours[i]);
            merge (theme.backdrop.brightness, from.backdrop.brightness, to.backdrop.brightness);
            merge (theme.backdrop.tint, from.backdrop.tint, to.backdrop.tint);
            merge (theme.textBrightness, from.textBrightness, to.textBrightness);
            applyTheme (theme, *prefs);
            prefs->saveIfNeeded();
        });
    }
    void NativeEditor::setZenMode (bool enabled)
    {
        zenMode = enabled;
        if (!enabled)
            scale.cancelGesture();
        scale.minimum = UiScale::minimumPercent;
        scale.maximum = UiScale::maximumPercent;
        scale.setValue (controller.processor.getProjectUiScalePercent());
       #if JUCE_MAC
        sync();
       #endif
        resized();
        repaint (0, getHeight() - Layout::footerHeight, getWidth(), Layout::footerHeight);
    }
   #if JUCE_MAC
    void NativeEditor::setTallMode (bool enabled)
    {
        if (!tallModeAvailable() || enabled == layout.tallMode)
            return;
        cancelCompactGestures();
        noteChooser.dismiss();
        uiPreferences->setValue ("tallMode", enabled);
        uiPreferences->saveIfNeeded();
        sync();
        repaint();
    }
   #endif
    void NativeEditor::setZenScaleLimit (int maximumPercent)
    {
        scale.maximum = juce::jlimit (1, UiScale::maximumPercent, maximumPercent);
        scale.minimum = juce::jmin (UiScale::minimumPercent, scale.maximum);
        scale.setValue (juce::jmin (controller.processor.getProjectUiScalePercent(), scale.maximum));
    }
    void NativeEditor::showNoteChooser (int ch, juce::Component& input)
    {
        cancelCompactGestures();
        controller.commit();
        controller.refresh();
        if (!juce::isPositiveAndBelow (ch, controller.state.playback.channelCount))
            return;
        const auto& channel = controller.state.playback.channels[static_cast<size_t> (ch)];
        if (layout.phoneRowControls)
        {
            setTempoOpen (false);
            selectedChannel = ch;
            panel = Panel::channel;
        }
        noteChooserChannel = ch;
        noteChooserOriginal = channel;
        noteChooserEpoch = controller.processor.getUiBridge().getDocumentEpoch();
        noteChooser.setRowOutput (channel);
        noteChooser.open (juce::String (ch + 1), channel.note, &input);
        grid.setRowOutlinesVisible (false);
        syncNoteChooserControls();
        if (showingPhoneNoteChooser())
        {
            sync();
            repaint();
        }
    }
    void NativeEditor::syncNoteChooserGrid()
    {
        if (layout.phoneRowControls)
            return;
        juce::Rectangle<float> cells, dimBounds;
        if (grid.isVisible() || layout.compact)
        {
            const int rows = layout.visibleRows();
            cells = { layout.gridLeft(), layout.cellsY(), layout.gridWidth(),
                      static_cast<float> (rows - 1) * layout.rowHeight() + layout.cellHeight() };
            if (grid.isVisible())
            {
                const auto bounds = grid.getLocalBounds().toFloat();
                auto dim = cells;
                if (!layout.compact && controller.source() >= 0)
                    dim = dim.expanded (static_cast<float> (Layout::loopEdgeSpace), (layout.rowHeight() - layout.cellHeight()) * 0.5f);
                cells = getLocalArea (&grid, cells.getIntersection (bounds));
                dimBounds = getLocalArea (&grid, dim.getIntersection (bounds));
            }
            else
                cells.translate (0.0f, layout.gridY());
        }
        noteChooser.setGridCells (cells, dimBounds);
    }
    void NativeEditor::syncNoteChooserControls()
    {
        const auto accent = Palette::channel (noteChooserChannel, controller.drumColors);
        noteChooser.setAccent (accent);
        if (layout.phoneRowControls)
            channelNote.setHighlightAccent (accent);
        const auto& channels = controller.state.playback.channels;
        int lowest = 127, highest = 0;
        for (const auto& row : channels)
            if (!row.controller)
            {
                lowest = juce::jmin (lowest, row.note);
                highest = juce::jmax (highest, row.note);
            }
        noteChooser.setTransposeRange (lowest, highest);
    }
    bool NativeEditor::noteChooserMatches() const
    {
        if (!noteChooser.isVisible() || noteChooserEpoch != controller.processor.getUiBridge().getDocumentEpoch()
            || !juce::isPositiveAndBelow (noteChooserChannel, controller.state.playback.channelCount))
            return false;
        const auto output = [] (const Channel& row)
        {
            return std::tie (row.note, row.controller, row.ccNumber, row.ccChannel);
        };
        return output (controller.state.playback.channels[static_cast<size_t> (noteChooserChannel)]) == output (noteChooserOriginal);
    }
    void NativeEditor::applyNoteChooserPreset (const NoteChooser::RowNotes& notes, const juce::String& undoLabel)
    {
        if (!noteChooserMatches() || noteChooserOriginal.controller)
            return;

        auto noteAssignments = notes;
        const auto& channels = controller.state.playback.channels;
        for (size_t row = 0; row < channels.size(); ++row)
            if (channels[row].controller)
                noteAssignments[row] = channels[row].note;
        if (std::ranges::any_of (noteAssignments, [] (int note) { return !juce::isPositiveAndBelow (note, 128); }))
            return;
        const juce::ScopedValueSetter<bool> editing (noteChooserEditing, true);
        controller.command ("channel_notes", noteAssignments, undoLabel);
        noteChooserOriginal.note = controller.state.playback.channels[static_cast<size_t> (noteChooserChannel)].note;
        noteChooser.setAssignedNote (noteChooserOriginal.note);
        syncNoteChooserControls();
    }
    void NativeEditor::configureValue (ValueControl& control, const juce::String& caption, int minimum, int maximum, const std::string& command)
    {
        addAndMakeVisible (control);
        control.caption = caption;
        control.minimum = minimum;
        control.maximum = maximum;
        control.setTitle (caption.isEmpty() ? juce::String (command) : caption);
        control.setTooltip (juce::String::fromUTF8 ("Double-click to type • Right-click to reset"));
        control.onBegin = [this, command] { controller.begin ("Change " + juce::String (command), command == "source_steps" ? juce::String() : juce::String (command)); };
        control.onChange = [this, command] (int v) { controller.command (command, command == "source_steps" ? nlohmann::json::array ({ controller.source() + 1, v }) : nlohmann::json::array ({ v })); };
        control.onEnd = [this] { controller.commit(); };
        control.onCancel = [this] { controller.cancel(); };
    }
    void NativeEditor::sync()
    {
        if (syncing)
            return;
        syncing = true;
        const auto& p = controller.state.playback;
        const auto oldHeight = layout.height(), oldWidth = layout.width();
        const int requestedScale = controller.processor.getUserUiScalePercent();
        const bool scaleChanged = previousScale != requestedScale;
        previousScale = requestedScale;
        const bool expandedRows = uiPreferences->getBoolValue (show16RowsPreference);
        const int rowLimit = expandedRows ? Constants::maxChannels : Layout::defaultVisibleRows;
        const bool rowLimitChanged = rowLimit != layout.rowLimit;
        if (rowLimitChanged)
        {
            cancelCompactGestures();
            noteChooser.dismiss();
            trackColourPopup.reset();
            trackColourContent.reset();
        }
        layout.rowLimit = rowLimit;
        layout.channels = juce::jmin (p.channelCount, rowLimit);
        layout.steps = p.stepCount;
        if (layout.firstStep >= layout.steps)
            layout.firstStep = 0;
        layout.standalone = controller.processor.hasStandaloneTransport();
       #if JUCE_MAC
        const bool tall = tallModeAvailable() && uiPreferences->getBoolValue ("tallMode");
        if (tall != layout.tallMode)
        {
            layout.tallMode = tall;
            if (tall)
                firstRowBeforeTallMode = layout.firstRow;
            else
                layout.firstRow = firstRowBeforeTallMode;
        }
       #endif
        if (rowLimitChanged)
        {
            layout.firstRow = layout.reverseRows ? layout.maxFirstRow() : 0;
           #if JUCE_MAC
            firstRowBeforeTallMode = layout.reverseRows ? juce::jmax (0, layout.channels - Layout::defaultVisibleRows) : 0;
           #endif
        }
        if (controller.selected.any())
            for (int key = layout.channels * Constants::maxSteps; key < Constants::cellsPerPattern; ++key)
                controller.selected.reset (static_cast<size_t> (key));
        show16Rows.setToggleState (expandedRows, juce::dontSendNotification);
        randomPanel.setGeneratorRowLimit (rowLimit);
        if (jamTarget && (jamTarget->source != controller.source() || jamTarget->epoch != controller.state.documentEpoch))
            cancelJam();
        syncJamButton();
        generate.setEnabled (!jamTarget);
        record.setEnabled (controller.source() >= 0 && !jamTarget);
        record.setToggleState (controller.processor.isPatternRecordingEnabled(), juce::dontSendNotification);
        syncMidiOutput();
        midiOutput.setVisible (layout.standalone && controller.processor.showStandaloneMidiOutputMenu && !layout.compact);
        linkSettings.setVisible (layout.standalone && controller.processor.showStandaloneLinkSettings && !layout.compact);
        settings.setVisible (!layout.compact);
        if (!layout.compact)
            for (auto* button : { &dc, &cellDisplay, &rowOrder })
                button->setVisible (false);
        layout.firstRow = juce::jlimit (0, layout.maxFirstRow(), layout.firstRow);
        grid.layout = layout;
        visibleRows.setButtonText ("Rows: " + juce::String (layout.compactVisibleRows));
        rowScroll.setRangeLimits (0, layout.channels, juce::dontSendNotification);
        rowScroll.setCurrentRange (layout.firstRow, layout.visibleRows(), juce::dontSendNotification);
        preview.reverseRows = randomPanel.reverseRows = layout.reverseRows;
        const int source = controller.source();
        const auto slot = juce::String (source + 1);
        const auto patternLabel = layout.phoneRowControls
            ? (source < 0 ? juce::String::fromUTF8 (layout.compactOnPhone ? u8"M" : u8"–") : slot)
            : (source < 0 ? juce::String ("Muted  v") : "Pattern " + slot + "  v");
        compactPattern.setButtonText (patternLabel);
        compactPattern.setToggleState (patternPicker.isVisible(), juce::dontSendNotification);
        if (clearSource != controller.source() || clearEpoch != controller.processor.getUiBridge().getDocumentEpoch())
        {
            confirmPatternClear = false;
            patternClearFlash.cancel();
            compactClearFlash.cancel();
        }
        compactClear.setButtonText (confirmPatternClear ? "Confirm clear P" + juce::String (controller.source() + 1) : "Clear pattern...");
        compactClear.setEnabled (controller.source() >= 0);
        recordOptions.setEnabled (controller.source() >= 0);
        if (patternPicker.isVisible())
            patternPicker.sync();
        const int pageIndex = layout.firstStep / Layout::compactStepsPerPage;
        const int pageCount = (layout.steps + Layout::compactStepsPerPage - 1) / Layout::compactStepsPerPage;
        page.setButtonText (juce::String (pageIndex + 1) + ":" + juce::String (pageCount));
        page.setDescription ("Showing steps " + juce::String (layout.firstStep + 1) + " to "
                             + juce::String (layout.firstStep + layout.visibleSteps()));
        duplicate.setToggleState (grid.isCopyMode(), juce::dontSendNotification);
        for (size_t i = 0; i < patterns.size(); ++i)
        {
            const auto& pattern = p.sources[i];
            const auto stepsInPattern = p.sourceSettings[i].stepCount;
            const auto channels = std::span (pattern).first (static_cast<size_t> (p.channelCount));
            patterns[i].setOccupied (std::ranges::any_of (channels, [stepsInPattern] (const auto& row)
            {
                return std::ranges::any_of (std::span (row).first (static_cast<size_t> (stepsInPattern)), &Cell::enabled);
            }));
        }
        updatePatternButtons();
        updateLayerHeading();
        for (auto* b : { &clear, &left, &right, &rate, static_cast<juce::TextButton*> (&recordQuantize) })
            b->setEnabled (controller.source() >= 0);
        play.setEnabled (!jamTarget);
        steps.setEnabled (controller.source() >= 0);
        undoButton.setEnabled (controller.canUndo());
        redoButton.setEnabled (controller.canRedo());
        swing.setValue (p.swing);
        showSwing.setToggleState (layout.showSwing, juce::dontSendNotification);
        subdivision.setValue (p.swingSubdivisionIndex);
        velocityHumanize.setValue (p.velocityHumanize);
        timingHumanize.setValue (p.timingHumanize);
        steps.setValue (p.stepCount);
        syncTempo();
        scale.setValue (zenMode ? juce::jmin (controller.processor.getProjectUiScalePercent(), scale.maximum) : controller.processor.getProjectUiScalePercent());
        static const std::array<std::string, 8> rates { "4n", "4nt", "8n", "8nt", "16n", "16nt", "32n", "32nt" };
        auto value = p.sourceSettings[static_cast<size_t> (juce::jmax (0, controller.source()))].rate;
        auto found = std::find (rates.begin(), rates.end(), value);
        rate.setButtonText (found == rates.end() ? "16th" : rateNames[static_cast<size_t> (found - rates.begin())]);
        const bool playing = controller.processor.isStandaloneTransportPlaying();
        if (jamPlayback != JamPlayback::stopped && (playing || jamPlayback == JamPlayback::playing))
        {
            jamPlayback = playing ? JamPlayback::playing : JamPlayback::stopped;
            if (!playing)
                globalFocusChanged (getCurrentlyFocusedComponent());
        }
        lastStandalonePlaying = playing;
        lastStandaloneLaunchPending = controller.processor.isStandaloneLaunchPending();
        stop.getProperties().set ("launchPending", lastStandaloneLaunchPending);
        stop.setTitle (lastStandaloneLaunchPending ? "Cancel Link launch" : "Stop");
        play.setVisible (layout.standalone && !playing);
        stop.setVisible (layout.standalone && playing);
        stop.setTooltip (lastStandaloneLaunchPending ? "Waiting for the next Link bar. Click to cancel."
                                                    : jamPlayback != JamPlayback::stopped ? "Space or Esc to stop" : "Space to stop");
        if (!layout.compact)
            tempoButton.setVisible (layout.standalone);
        if (!layout.compact)
            metronome.setVisible (layout.standalone);
        metronome.setToggleState (controller.processor.isStandaloneMetronomeEnabled(), juce::dontSendNotification);
        play.setToggleState (playing, juce::dontSendNotification);
        stop.setToggleState (!playing, juce::dontSendNotification);
        if (!layout.phoneRowControls)
        {
            const bool padsVisible = showingRecordingPads();
            recordingStrip.setVisible (!layout.compact && padsVisible);
            preview.setVisible (layout.compact || !padsVisible);
        }
        syncRecordingPads();
        const int recordStrength = controller.processor.getPatternRecordingQuantizeStrength();
        recordQuantize.setToggleState (recordStrength == 100, juce::dontSendNotification);
        recordQuantize.setTitle (recordStrength == 100 ? "Record quantization on" : recordStrength == 0 ? "Record quantization off" : "Record quantization partial");
        recordQuantize.setTooltip ((recordStrength == 100 ? juce::String ("Grid locked") : recordStrength == 0 ? juce::String ("Preserve played timing") : "Record quantization " + juce::String (recordStrength) + "%")
                                  + (layout.phoneRowControls ? "" : ". Right-click or Shift-F10 for timing options"));
        dc.setToggleState (controller.drumColors, juce::dontSendNotification);
        cellDisplay.setToggleState (!controller.graphicalCellValues, juce::dontSendNotification);
        modifierToggle.setToggleState (controller.keyboardModifiersEnabled, juce::dontSendNotification);
        rowOrder.setToggleState (layout.reverseRows, juce::dontSendNotification);
        projectHeader.setProjectName (controller.processor.hasUnsupportedProjectVersion() ? juce::String ("Unsupported project version")
                                                                                         : controller.processor.getProjectName());
        projectBrowser.refreshCurrentProject();
        if (noteChooser.isVisible() && !noteChooserEditing)
        {
            if (!noteChooserMatches())
                noteChooser.dismiss();
            else
            {
                syncNoteChooserControls();
                noteChooser.toFront (false);
            }
        }
        grid.sync();
        preview.sync();
        randomPanel.sync();
        if (layout.compact)
            syncChannel();
        // The compact layout also decides visibility from editor state, so it always re-runs.
        if (layout.compact || laidOut != std::pair { layout, getLocalBounds() })
            resized();
        if (projectBrowser.isVisible())
            projectBrowser.toFront (false);
        if (discardChanges.isVisible())
        {
            if (discardEpoch != controller.processor.getUiBridge().getDocumentEpoch())
                discardChanges.dismiss();
            else
                discardChanges.toFront (false);
        }
        if (!layout.compact)
            repaint (getWidth() / 2, juce::roundToInt (layout.toolbarY()), getWidth() - getWidth() / 2, 68);
        if (!layout.settingsInHeader)
            repaint (0, getHeight() - Layout::footerHeight, getWidth(), Layout::footerHeight);
        syncing = false;
        if ((scaleChanged || std::abs (oldHeight - layout.height()) > 0.01f || std::abs (oldWidth - layout.width()) > 0.01f) && preferredSizeChanged)
            preferredSizeChanged();
    }
    void NativeEditor::resized()
    {
        laidOut = { layout, getLocalBounds() };
        if (!layout.phoneRowControls)
            noteChooser.setBounds (getLocalBounds());
        const float projectTop = layout.compact ? layout.gridY() : layout.toolbarY();
        const auto projectArea = layout.phoneRowControls ? getLocalBounds()
            : getLocalBounds().withTop (juce::roundToInt (projectTop)).withBottom (juce::roundToInt (layout.previewY()));
        // Short projects still need room for the browser's touch targets and save form.
        const int browserBottom = layout.compact ? projectArea.getBottom()
            : juce::jmin (getHeight(), juce::jmax (projectArea.getBottom(), projectArea.getY() + 404));
        projectBrowser.setRowArea (projectArea.withBottom (browserBottom));
        projectBrowser.setBounds (getLocalBounds());
        discardChanges.setBounds (projectArea);
        constexpr int target = Layout::touchTarget;
        const bool phoneMenu = layout.phoneRowControls && panel >= Panel::project && panel <= Panel::help;
        steps.valueWidth = phoneMenu ? 0 : target;
        steps.valueFontSize = layout.compact ? 13.0f : gridHeaderValueFontSize;
        steps.captionGap = layout.compact ? 2 : 5;
        rate.getProperties().set ("dropdownFontSize", steps.valueFontSize);
        if (layout.compact)
            steps.captionFontSize = layout.phoneRowControls ? 10.0f : 12.0f;
        for (auto* value : { &swing, &subdivision, &velocityHumanize, &timingHumanize })
        {
            value->valueWidth = phoneMenu ? 0 : layout.compact ? 42 : target;
            value->valueFontSize = layout.compact && !phoneMenu ? 12.0f : 18.0f;
            value->captionInset = 3;
        }
        for (auto* button : { &midiOutput, &linkSettings })
            button->getProperties().set ("insetFooterArtwork", !layout.compact);
        settings.getProperties().set ("footerControl", !layout.settingsInHeader);
        settings.getProperties().set ("insetFooterArtwork", !layout.compact && !layout.settingsInHeader);
        scale.getProperties().set ("insetFooterArtwork", !layout.compact);
        for (auto* button : std::array<juce::TextButton*, 8> { &generate, &jamButton, &showPads, &record, &recordQuantize, &left, &right, &clear })
            button->getProperties().set ("gridToolbarAction", !layout.compact);
        scale.setVisible (layout.standalone && !layout.compact && zenMode);
        if (layout.compact)
        {
            resizedCompact();
            return;
        }
        linkSettings.setButtonText ("Link");
        const int rightEdge = getWidth() - Layout::outerGutter;
        constexpr int headerTop = Layout::headerPadding;
        constexpr int secondRowY = headerTop + target + Layout::headerRowGap;
        // Project actions and navigation share the left header column.
        projectButtons[0].setBounds (12, headerTop, 74, target);
        projectButtons[1].setBounds (92, headerTop, 74, target);
        projectButtons[2].setBounds (172, headerTop, 74, target);
        projectButtons[5].setBounds (252, headerTop, 94, target);
        projectButtons[3].setBounds (12, secondRowY, target, target);
        projectButtons[4].setBounds (projectButtons[5].getRight() - target, secondRowY, target, target);
        projectHeader.setBounds (82, secondRowY, projectButtons[4].getX() - 88, target);
        constexpr int macroBottomPadding = 8;
        constexpr int macroCaptionGap = 6;
        constexpr int macroHeight = target + 18 + macroCaptionGap;
        constexpr int macroY = secondRowY + target - macroBottomPadding - macroHeight;
        swing.setBounds (366, macroY, 72, macroHeight);
        subdivision.setBounds (444, macroY, 72, macroHeight);
        showSwing.setBounds (subdivision.getRight() + 6 - target, 0, target, target);
        velocityHumanize.setBounds (538, macroY, 72, macroHeight);
        timingHumanize.setBounds (616, macroY, 72, macroHeight);
        for (auto* value : { &swing, &subdivision, &velocityHumanize, &timingHumanize })
        {
            value->captionFontSize = labelFontSize;
            value->captionGap = macroCaptionGap;
        }
        constexpr int patternGap = Layout::headerRowGap;
        const int muteX = rightEdge - target - (layout.standalone || layout.settingsInHeader ? target + patternGap : 0);
        const int patternRight = muteX - patternGap;
        const int patternLeft = patternRight - (8 * target + 7 * patternGap);
        for (int i = 0; i < 16; ++i)
            patterns[static_cast<size_t> (i)].setBounds (patternLeft + (i % 8) * (target + patternGap), headerTop + (i / 8) * (target + Layout::headerRowGap), target, target);
        mutePattern.setBounds (muteX, layout.standalone ? secondRowY : (headerTop + secondRowY) / 2, target, target);
        if (layout.settingsInHeader)
            settings.setBounds (rightEdge - target, mutePattern.getY(), target, target);
        if (layout.standalone)
        {
            play.setBounds (rightEdge - target, headerTop, target, target);
            stop.setBounds (play.getBounds());
            tempoButton.setBounds (muteX, headerTop, target, target);
            metronome.setBounds (rightEdge - target, secondRowY, target, target);
            if (tempoOpen)
                layoutTempoPopup();
        }
        const int inputY = juce::roundToInt (layout.toolbarY()) + Layout::toolbarPadding + Layout::toolbarCaptionSpace;
        velocityLayer.setBounds (12, inputY, 104, target);
        chanceLayer.setBounds (115, inputY, 104, target);
        selectMode.setBounds (218, inputY, 104, target);
        layerHint.setBounds (velocityLayer.getBounds().getUnion (selectMode.getBounds())
                                 .withTrimmedLeft (60).withY (inputY - 22).withHeight (20));
        undoButton.setBounds (336, inputY, target, target);
        redoButton.setBounds (399, inputY, target, target);
        const int divisionWidth = juce::jmax (144, rateControlWidth());
        rate.setBounds (rightEdge - divisionWidth, inputY, divisionWidth, target);
        steps.captionFontSize = labelFontSize;
        steps.setBounds (rate.getX() - 100, inputY - 23, 90, target + 23);
        clear.setBounds (steps.getX() - 78, inputY, target, target);
        right.setBounds (clear.getX() - 74, inputY, target, target);
        left.setBounds (right.getX() - 66, inputY, target, target);
        constexpr int recordGap = 2;
        constexpr int redoClearance = 8;
        const int actionCount = layout.standalone ? 5 : 4;
        const int actionSpace = left.getX() - redoButton.getRight() - actionCount * target - recordGap - redoClearance;
        const int actionGap = juce::jmax (0, actionSpace / (actionCount + 1));
        recordQuantize.setBounds (left.getX() - actionGap - target, inputY, target, target);
        record.setBounds (recordQuantize.getX() - recordGap - target, inputY, target, target);
        showPads.setBounds (record.getX() - actionGap - target, inputY, target, target);
        jamButton.setBounds (showPads.getX() - actionGap - target, inputY, target, target);
        generate.setBounds ((layout.standalone ? jamButton : showPads).getX() - actionGap - target, inputY, target, target);
        const int randomY = juce::roundToInt (layout.gridY());
        const int aboutHeight = juce::roundToInt (layout.previewY()) - randomY;
        constexpr int minRandomizerHeight = 540;
        const int scrollWidth = aboutHeight < minRandomizerHeight ? Layout::outerGutter : 0;
        aboutViewport.setScrollBarThickness (Layout::outerGutter);
        aboutViewport.setBounds (0, randomY, getWidth(), aboutHeight);
        randomPanel.setDesktopRandomizerSize (getWidth() - scrollWidth, juce::jmax (minRandomizerHeight, aboutHeight),
                                              Layout::outerGutter - scrollWidth);
        grid.setBounds (0, juce::roundToInt (layout.gridY()), getWidth(), juce::roundToInt (layout.gridHeight()));
        layoutRowScroll();
        rowScroll.setVisible (layout.maxFirstRow() > 0 && !layout.randomVisible);
        syncNoteChooserGrid();
        preview.setBounds (0, juce::roundToInt (layout.previewY()), getWidth(), Layout::previewHeight);
        const int inspectorLeft = juce::roundToInt (layout.gridLeft()) - Layout::loopEdgeSpace;
        const int inspectorRight = juce::roundToInt (layout.gridLeft() + layout.gridWidth()) + Layout::loopEdgeSpace;
        preview.setInspectorBounds ({ inspectorLeft, 8, inspectorRight - inspectorLeft, Layout::previewHeight - 16 });
        recordingStrip.setBounds (preview.getBounds());
        layoutRecordingPads();
        if (layout.settingsInHeader)
            return;
        const int footerControlY = getHeight() - Layout::footerHeight + Layout::footerPadding;
        // Align the inset footer faces with the header while keeping full-size hit targets.
        int footerRight = rightEdge + footerArtworkInset;
        const auto placeFooter = [&] (juce::Component& control, int width = Layout::touchTarget)
        {
            control.setBounds (footerRight - width, footerControlY, width, target);
            footerRight -= width + Layout::footerGap;
        };
        if (scale.isVisible())
            placeFooter (scale, 88);
        if (layout.standalone && controller.processor.showStandaloneMidiOutputMenu)
            placeFooter (midiOutput, 104);
        if (linkSettings.isVisible())
            placeFooter (linkSettings);
        placeFooter (settings);
        footerHelp.setBounds (Layout::outerGutter, footerControlY, settings.getX() - 2 * Layout::outerGutter, target);
        footerJamPad.setBounds (footerHelp.getBounds());
    }
    void NativeEditor::showProjectSaved()
    {
        projectSavedTimer.startTimer (1400);
        repaint();
    }

    void NativeEditor::paintOverChildren (juce::Graphics& g)
    {
        if (isProjectDialogOpen())
        {
            const auto dim = Palette::app.withAlpha (0.75f);
            if (layout.phoneRowControls)
                return;
            if (layout.compact)
            {
                g.setColour (dim);
                g.fillRect (getLocalBounds().withHeight (layout.compactHeaderBottom()));
            }
            else
            {
                const int projectRight = projectButtons[5].getRight();
                g.setGradientFill (juce::ColourGradient (dim.withAlpha (0.0f), static_cast<float> (projectRight), 0.0f,
                                                         dim, static_cast<float> (swing.getX() - 8), 0.0f, false));
                g.fillRect (getLocalBounds().withHeight (Layout::headerHeight).withTrimmedLeft (projectRight));
            }
            return;
        }
        if (!projectSavedTimer.isTimerRunning())
            return;
        const auto bounds = juce::Rectangle<float> (150, 40).withCentre ({ getWidth() * 0.5f, getHeight() - 62.0f });
        controlSurface (g, bounds, true);
        text (g, "Project saved", bounds, 17, Palette::text, true);
    }

    void NativeEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::app);
        if (layout.compact)
        {
            if (isProjectDialogOpen())
                return;
            if (showingCompactPadPreview())
                paintCompactPadPreview (g);
            if (layout.phoneRowControls && panel >= Panel::project && panel <= Panel::help)
            {
                paintPhoneMenu (g);
                return;
            }
            if (panel != Panel::none && panel != Panel::generate && !showingPhoneNoteChooser() && !showingPhoneChannelSettings())
            {
                if (!layout.phoneRowControls || panel == Panel::channel)
                {
                    const juce::String menuTitle = "Settings";
                    const auto heading = panel == Panel::channel ? "Track " + juce::String (selectedChannel + 1)
                                         : panel == Panel::pattern ? menuTitle + (controller.source() < 0 ? " / Muted Pattern" : " / Pattern " + juce::String (controller.source() + 1))
                                                                   : menuTitle;
                    const auto headingColour = panel == Panel::channel && layout.phoneRowControls
                        ? Palette::channel (selectedChannel, controller.drumColors) : Palette::text;
                    text (g, heading, { 16, 2, static_cast<float> (getWidth() - 328), 30 }, 15, headingColour, true, juce::Justification::centredLeft);
                }
                surface (g, { 6, layout.gridY() + 4, static_cast<float> (getWidth() - 12), static_cast<float> (getHeight()) - layout.gridY() - 10 });
                if (panel == Panel::pattern)
                {
                    paintControlGroup (g, { 16, 108, 324, 98 }, "Pattern grid");
                    paintControlGroup (g, { 356, 108, static_cast<float> (getWidth() - 372), 98 }, "Recording");
                    text (g, "Division", { 164, 126, 160, 18 }, 12, Palette::muted, true, juce::Justification::centredLeft);
                    text (g, "Record", { 372, 126, 64, 18 }, 12, Palette::muted, true);
                    text (g, "Quantize", { 438, 126, 72, 18 }, 12, Palette::muted, true);
                    text (g, "Shift steps", { 28, 224, 140, 18 }, 12, Palette::muted, true, juce::Justification::centredLeft);
                    text (g, "Use Undo to revert pattern edits.", { 28, 310, static_cast<float> (getWidth() - 56), 24 }, 12, Palette::muted, false, juce::Justification::centredLeft);
                }
                else if (panel == Panel::feel)
                {
                    paintControlGroup (g, { 12, 104, 336, 110 }, "Swing");
                    paintControlGroup (g, { 356, 104, 392, 110 }, "Humanize");
                    text (g, "Drag up or down. Double-tap to type.", { 22, 218, static_cast<float> (getWidth() - 44), 36 }, 13, Palette::muted, false, juce::Justification::centredLeft);
                }
                else if (panel == Panel::view)
                {
                    text (g, "Appearance", { 28, 104, 300, 18 }, 12, Palette::muted, true, juce::Justification::centredLeft);
                    text (g, "Hardware keyboard", { 28, 198, 300, 18 }, 12, Palette::muted, true, juce::Justification::centredLeft);
                }
                else if (panel == Panel::channel)
                {
                    const auto heading = controller.isControllerRow (selectedChannel) ? "MIDI CC and loop" : "MIDI note and loop";
                    text (g, heading, { 28, 64, 160, 24 }, 16, Palette::channel (selectedChannel, controller.drumColors), true, juce::Justification::centredLeft);
                }
                else if (panel == Panel::help)
                {
                    const std::array<juce::String, 5> lines {
                        "Hold an active step to select it. Keep holding for presets.",
                        "Drag across the grid to paint. Drag up or down to edit a value.",
                        "Turn Select off to move steps. Turn Copy on to duplicate them.",
                        "Drag Velocity or Chance sideways to adjust selected steps.",
                        "Hold the row power button to solo." };
                    for (int i = 0; i < static_cast<int> (lines.size()); ++i)
                        text (g, lines[static_cast<size_t> (i)], { 20, 100.0f + static_cast<float> (i) * 38, static_cast<float> (getWidth() - 284), 34 }, labelFontSize, Palette::text, false, juce::Justification::centredLeft);
                }
            }
            return;
        }
        const auto headerBounds = getLocalBounds().withHeight (Layout::headerHeight).toFloat();
        const auto headerTop = Palette::raised.interpolatedWith (Palette::info, 0.14f).interpolatedWith (Palette::app, 0.66f);
        const auto headerBottom = Palette::shell.interpolatedWith (Palette::app, 0.66f);
        g.setGradientFill (juce::ColourGradient (headerTop, headerBounds.getTopLeft(), headerBottom, headerBounds.getBottomRight(), false));
        g.fillRect (headerBounds);
        const auto toolbarCaption = [&] (const juce::String& label, juce::Rectangle<int> bounds)
        {
            text (g, label, bounds.withY (steps.getY()).withHeight (18).toFloat(),
                  steps.captionFontSize, Palette::muted, true, juce::Justification::centredLeft);
        };
        toolbarCaption ("Mode", velocityLayer.getBounds().getUnion (selectMode.getBounds()));
        toolbarCaption ("Undo", undoButton.getBounds().getUnion (redoButton.getBounds()));
        toolbarCaption ("Generate", generate.getBounds().withRight ((layout.standalone ? jamButton : showPads).getX()));
        if (layout.standalone)
            toolbarCaption ("Jam", jamButton.getBounds());
        toolbarCaption ("Pads", showPads.getBounds().withRight (record.getX() - 4));
        toolbarCaption ("Arm Record", record.getBounds().getUnion (recordQuantize.getBounds()));
        toolbarCaption ("Shift", left.getBounds().getUnion (right.getBounds()));
        toolbarCaption ("Clear", clear.getBounds());
        toolbarCaption ("Grid Division", rate.getBounds());
        g.setColour (Palette::border.withAlpha (0.5f));
        g.drawHorizontalLine (juce::roundToInt (layout.toolbarY()), 0, static_cast<float> (getWidth()));
        g.drawHorizontalLine (static_cast<int> (layout.gridY()), 0, static_cast<float> (getWidth()));
        if (!layout.settingsInHeader)
            g.drawHorizontalLine (static_cast<int> (layout.height()) - Layout::footerHeight, 0, static_cast<float> (getWidth()));
        const auto patternRows = patterns.front().getBounds().getUnion (patterns.back().getBounds());
        const auto groupY = static_cast<float> (patternRows.getY()) + 0.5f;
        const auto groupHeight = static_cast<float> (patternRows.getHeight()) - 1.0f;
        paintControlGroup (g, { static_cast<float> (swing.getX() - 8), groupY, static_cast<float> (subdivision.getRight() - swing.getX() + 16), groupHeight }, "Swing");
        paintControlGroup (g, { static_cast<float> (velocityHumanize.getX() - 8), groupY, static_cast<float> (timingHumanize.getRight() - velocityHumanize.getX() + 16), groupHeight }, "Humanize");
        // Paint in the parent so the halo can extend beyond the button's clip bounds.
        const auto paintPatternGlow = [this, &g] (const juce::TextButton& button, bool copyDestinationHovered = false)
        {
            if (!button.isVisible() || !(button.getToggleState() || copyDestinationHovered))
                return;
            const auto bounds = button.getBounds().toFloat().reduced (0.5f);
            if (!g.clipRegionIntersects (bounds.expanded (static_cast<float> (patternGlowExtent)).getSmallestIntegerContainer()))
                return;
            const auto colour = copyDestinationHovered ? Palette::accent.brighter (0.18f) : Palette::accent;
            patternGlow.setColor (colour.withAlpha ((copyDestinationHovered ? 0.50f : 0.8f) * button.getAlpha()), 0);
            patternGlow.setColor (colour.withAlpha (0.55f * button.getAlpha()), 1);
            juce::Path silhouette;
            silhouette.addRoundedRectangle (bounds, 4);
            patternGlow.render (g, silhouette);
        };
        for (const auto& button : patterns)
            paintPatternGlow (button, button.showsCopyDestination());
        paintPatternGlow (mutePattern);
    }
    void NativeEditor::mouseDown (const juce::MouseEvent& event)
    {
        optionTapStarted.reset();
        if (jamTarget || jamPlayback != JamPlayback::stopped || heldJamStopKey)
            globalFocusChanged (getCurrentlyFocusedComponent());
        if (projectBrowser.isVisible())
        {
            if (event.originalComponent == getParentComponent()) projectBrowser.dismiss();
            return;
        }
        if (tempoOpen && event.originalComponent != this && !isParentOf (event.originalComponent))
        {
            setTempoOpen (false);
            return;
        }
        if (channelOverlay.isVisible())
        {
            const auto* clicked = event.originalComponent;
            const std::array<const juce::Component*, 9> settingsControls {
                &channelOverlay, &grid.rowSettingsButton (selectedChannel), &channelNote, &loopStart, &loopLength, &channelDirection, &channelSolo, &channelClear, &noteChooser
            };
            if (std::ranges::none_of (settingsControls, [clicked] (const auto* control) { return control == clicked || control->isParentOf (clicked); }))
                showPanel (Panel::none);
        }
        if (event.originalComponent == &grid && event.mods.isLeftButtonDown())
            preview.commitPendingCycleEdit();
        if (event.originalComponent != &grid)
            grid.finishVelocityDrawing();
        if (showingCompactPadPreview() || (layout.phoneRowControls && patternPicker.isVisible())
            || grid.handleCellPopupMouseDown (event.originalComponent)
            || !event.mods.isLeftButtonDown() || controller.selected.none()
            || discardChanges.isVisible())
            return;
        // Child listeners receive clicks on nested labels and editors too.
        // Keep selection available to every control that can act on it.
        for (auto* component = event.originalComponent; component != nullptr && component != this && isParentOf (component); component = component->getParentComponent())
            if (component == &grid || dynamic_cast<juce::Button*> (component) != nullptr
                || dynamic_cast<ValueControl*> (component) != nullptr
                || dynamic_cast<juce::TextEditor*> (component) != nullptr
                || dynamic_cast<juce::Slider*> (component) != nullptr
                || dynamic_cast<juce::ComboBox*> (component) != nullptr
                || dynamic_cast<juce::ScrollBar*> (component) != nullptr
                || component->getWantsKeyboardFocus())
                return;
        controller.commit();
        controller.selected.reset();
        controller.notify();
    }
    void NativeEditor::rateMenu()
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&look);
        int selected = 5;
        for (int i = 0; i < 8; ++i)
        {
            const bool current = rate.getButtonText() == rateNames[static_cast<size_t> (i)];
            menu.addItem (i + 1, rateNames[static_cast<size_t> (i)], true, current);
            if (current)
                selected = i + 1;
        }
        rate.setToggleState (true, juce::dontSendNotification);
        const juce::Component::SafePointer<NativeEditor> safe (this);
        auto target = rate.getScreenBounds();
        const int gap = juce::roundToInt (4.0f * juce::Component::getApproximateScaleFactorForComponent (&rate));
        target.setBottom (target.getBottom() + gap);
        showAnchoredPopup (menu, editorPopupOptions (rate).withTargetScreenArea (target).withInitiallySelectedItem (selected),
                           [safe] (int result) {
            if (safe == nullptr)
                return;
            safe->rate.setToggleState (false, juce::dontSendNotification);
            if (result < 1)
                return;
            static const std::array<std::string, 8> values { "4n", "4nt", "8n", "8nt", "16n", "16nt", "32n", "32nt" };
            safe->controller.begin ("Change rate", "rate");
            safe->controller.command ("source_rate", { safe->controller.source() + 1, values[static_cast<size_t> (result - 1)] });
            safe->controller.commit();
        });
    }
    void NativeEditor::showDesktopRandomizer (bool visible)
    {
        randomPanel.cancelGesture();
        controller.commit();
        grid.cancelGesture();
        layout.randomVisible = visible;
        generate.setToggleState (visible, juce::dontSendNotification);
        aboutViewport.setVisible (layout.randomVisible);
        rowScroll.setVisible (layout.maxFirstRow() > 0 && !layout.randomVisible);
        if (layout.randomVisible)
        {
            aboutViewport.setViewPosition (0, 0);
            aboutViewport.toFront (false);
        }
        if (isShowing())
            grabKeyboardFocus();
        repaint();
    }
    bool NativeEditor::textFocused() const { return dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr; }
    void NativeEditor::showProjectBrowser (juce::File root, juce::File current, std::optional<ProjectBrowser::Details> details,
                                          ProjectBrowser::ListProjects list, ProjectBrowser::Submit submit,
                                          std::function<void()> importExternal, bool confirmOpen, ProjectBrowser::ReadDetails readDetails,
                                          juce::File lastFolder, ProjectBrowser::StorageLocations locations)
    {
        cancelCompactGestures();
        setTempoOpen (false);
        noteChooser.dismiss();
        if (patternPicker.isVisible())
            patternPicker.dismiss();
        juce::PopupMenu::dismissAllActiveMenus();
        projectBrowser.open (std::move (root), std::move (current), std::move (details),
                             std::move (list), std::move (submit), std::move (importExternal), confirmOpen, std::move (readDetails),
                             std::move (lastFolder), std::move (locations));
        if (layout.compact)
            resized();
        repaint();
    }

    void NativeEditor::confirmDiscardingChanges (std::function<void()> proceed)
    {
        cancelCompactGestures();
        discardEpoch = controller.processor.getUiBridge().getDocumentEpoch();
        discardChanges.open (controller.processor.getProjectName(), std::move (proceed));
        if (layout.compact)
            resized();
        repaint();
    }
    bool NativeEditor::keyPressed (const juce::KeyPress& key)
    {
        optionTapStarted.reset();
        if (handleJamStopKey (key))
            return true;
        if (projectBrowser.isVisible())
            return projectBrowser.keyPressed (key);
        if (patternPicker.isVisible())
            return patternPicker.keyPressed (key);
        if (noteChooser.isVisible())
            return noteChooser.keyPressed (key);
        if (discardChanges.isVisible())
            return discardChanges.keyPressed (key);
        if (textFocused())
            return false;
        if (key.getModifiers().isCommandDown() && key.isKeyCode ('Z'))
        {
            grid.finishVelocityDrawing();
            if (key.getModifiers().isShiftDown())
                controller.redo();
            else
                controller.undo();
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
        {
            if (tempoOpen)
            {
                setTempoOpen (false);
                return true;
            }
            if (showingPhoneChannelSettings())
            {
                showPanel (Panel::none);
                return true;
            }
            if (controller.exitSolo())
                return true;
            const bool exitZen = zenMode && zenModeChanged;
            const bool closeCompactPanel = layout.compact && panel != Panel::none;
            const bool handled = exitZen || closeCompactPanel || layout.randomVisible
                || controller.selectHeld || controller.selectLatched || controller.copySource >= 0
                || grid.hasActiveGesture() || controller.editing();
            if (!handled)
                return false;
            if (exitZen)
                zenModeChanged (false);
            if (layout.compact)
                closePanel.onClick();
            controller.cancelSelect();
            controller.copySource = -1;
            if (layout.randomVisible)
                showDesktopRandomizer (false);
            grid.cancelGesture();
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            controller.deleteSelection();
            return true;
        }
        if (key == juce::KeyPress::spaceKey && layout.standalone)
        {
            controller.command ("standalone_transport_playing", { !controller.processor.isStandaloneTransportPlaying() });
            return true;
        }
        if (layout.standalone && !key.getModifiers().isCommandDown())
        {
            const auto character = juce::CharacterFunctions::toUpperCase (key.getTextCharacter());
            const int index = padShortcutKeys.indexOfChar (character);
            if (juce::isPositiveAndBelow (index, layout.channels))
            {
                if (!heldPads.test (static_cast<size_t> (index)))
                {
                    heldPads.set (static_cast<size_t> (index));
                    triggerPad (index);
                }
                return true;
            }
        }
        return !layout.randomVisible && grid.isVisible() && hasKeyboardFocus (false) && grid.keyPressed (key);
    }
    bool NativeEditor::keyStateChanged (bool down)
    {
        if (!down)
        {
            if (heldJamStopKey && !juce::KeyPress::isKeyCurrentlyDown (*heldJamStopKey))
            {
                heldJamStopKey.reset();
                globalFocusChanged (getCurrentlyFocusedComponent());
            }
            if (layout.standalone)
                for (int i = 0; i < padShortcutKeys.length(); ++i)
                    if (!juce::KeyPress::isKeyCurrentlyDown (padShortcutKeys[i]))
                        heldPads.reset (static_cast<size_t> (i));
        }
        return false;
    }
    void NativeEditor::focusLost (FocusChangeType)
    {
        heldPads.reset();
        if (!hasKeyboardFocus (true))
        {
            if (layout.compact)
                cancelCompactGestures();
            controller.cancelSelect();
            grid.cancelGesture();
        }
    }
    void NativeEditor::updatePatternButtons()
    {
        const bool muted = controller.source() < 0;
        if (mutePattern.getToggleState() != muted)
            repaint (mutePattern.getBounds().expanded (patternGlowExtent));
        mutePattern.setToggleState (muted, juce::dontSendNotification);
        // Keep the armed source highlighted; pulse smoothly between 70% and full opacity.
        const auto phase = static_cast<double> (juce::Time::getMillisecondCounter() % 200u) / 200.0;
        const float copyAlpha = 0.85f + 0.15f * static_cast<float> (std::cos (juce::MathConstants<double>::twoPi * phase));
        for (int i = 0; i < 16; ++i)
        {
            auto& button = patterns[static_cast<size_t> (i)];
            const bool active = controller.copySource == i || controller.source() == i;
            const bool copyDestination = controller.copySource >= 0 && controller.copySource != i;
            const bool copyDestinationChanged = std::exchange (button.copyDestination, copyDestination) != copyDestination;
            const float alpha = controller.copySource == i ? copyAlpha : 1.0f;
            if (copyDestinationChanged || button.getToggleState() != active || !juce::approximatelyEqual (button.getAlpha(), alpha))
                repaint (button.getBounds().expanded (patternGlowExtent));
            button.setToggleState (active, juce::dontSendNotification);
            button.setAlpha (alpha);
        }
    }
    void NativeEditor::updateLayerHeading()
    {
        const auto layer = visibleGridLayer (grid.displayLayer());
        preview.setDisplayedLayer (layer);
        layerHint.setVisible (!layout.compact && overviewLayer (layer));
        layerHint.setText (overviewLayer (layer) ? juce::String (layer == Layer::nudge ? "Nudge" : "Split") + " / overview" : juce::String(), juce::dontSendNotification);
        const bool selecting = grid.isSelectMode() || controller.selected.any();
        const char* selectTip = layout.phoneRowControls
            ? "Tap a step or drag across steps to select. Drag selected steps to move them. Use the inspector to edit the selection."
            : controller.keyboardModifiersEnabled
                ? "Click a step or drag across steps to select. Drag selected steps to move them. Option-drag to copy. Use the inspector to edit the selection."
                : layout.compact
                    ? "Click a step or drag across steps to select. Drag selected steps to move them. Turn on Copy and drag to duplicate. Use the inspector to edit the selection."
                    : "Click a step or drag across steps to select. Drag selected steps to move them. Use the inspector to edit the selection.";
        grid.setTooltip (selecting ? selectTip : "Drag an active step edge for a velocity line. Double-click or double-tap the edge to draw velocity.");
        selectMode.setTooltip (selecting ? selectTip : layout.compactOnPhone ? "Tap to toggle Select" : "Hold for temporary Select");
        duplicate.setTooltip ("Turn on Copy, then drag selected steps to duplicate them.");
        const bool chance = layer == Layer::chance;
        velocityLayer.setToggleState (!selecting && layer == Layer::velocity, juce::dontSendNotification);
        chanceLayer.setToggleState (!selecting && chance, juce::dontSendNotification);
        selectMode.setToggleState (selecting, juce::dontSendNotification);
        // Shared seams belong to the active segment; inactive neighbors must
        // not paint over its orange edge.
        if (selecting)
            selectMode.toFront (false);
        else if (chance)
            chanceLayer.toFront (false);
        else
            velocityLayer.toFront (false);
        velocityLayer.setTitle ("Velocity editing layer");
        chanceLayer.setTitle (layout.phoneRowControls ? "Prob editing layer" : "Chance editing layer");
        const auto setShortcutHint = [this] (juce::TextButton& button, const char* hint)
        {
            const juce::String label = controller.keyboardModifiersEnabled && !layout.phoneRowControls ? hint : "";
            if (button.getProperties().set ("shortcutHint", label))
                button.repaint();
        };
        setShortcutHint (chanceLayer, "Option");
        setShortcutHint (selectMode, "Shift");
    }
    void NativeEditor::modifierKeysChanged (const juce::ModifierKeys& mods)
    {
        const bool wasOptionDown = std::exchange (lastOptionDown, mods.isAltDown());
        const bool optionAlone = !mods.isShiftDown() && !mods.isCtrlDown() && !mods.isCommandDown() && !mods.isAnyMouseButtonDown();
        if (!optionAlone || !controller.keyboardModifiersEnabled || controller.layer != Layer::chance
            || !hasKeyboardFocus (true) || isProjectDialogOpen() || noteChooser.isVisible() || textFocused())
            optionTapStarted.reset();
        else if (mods.isAltDown() && !wasOptionDown)
            optionTapStarted = juce::Time::getMillisecondCounterHiRes();
        else if (!mods.isAltDown() && wasOptionDown)
        {
            const auto started = std::exchange (optionTapStarted, std::nullopt);
            if (started && juce::Time::getMillisecondCounterHiRes() - *started < 300.0)
            {
                controller.layer = Layer::velocity;
                controller.notify();
            }
        }
        grid.updateModifiers (mods);
        duplicate.setToggleState (grid.isCopyMode(), juce::dontSendNotification);
        updateLayerHeading();
    }
    void NativeEditor::metronomeMenu (juce::Component& anchor)
    {
        constexpr int soundId = 1, pitchId = 10, volumeId = 100;
        const auto& current = controller.processor;
        juce::PopupMenu menu, pitch, volume;
        menu.setLookAndFeel (&look);
        const std::array<const char*, 3> sounds { "Classic", "Woodblock", "Claves" };
        for (int index = 0; index < static_cast<int> (sounds.size()); ++index)
            menu.addItem (soundId + index, sounds[static_cast<size_t> (index)], true, static_cast<int> (current.getMetronomeSound()) == index);
        for (int offset = -1; offset <= 1; ++offset)
            pitch.addItem (pitchId + offset, offset < 0 ? "Low" : offset > 0 ? "High" : "Normal", true, offset == current.getMetronomePitch());
        for (const int percent : { 0, 10, 25, 50, 75, 100 })
            volume.addItem (volumeId + percent, juce::String (percent) + "%", true, percent == current.getMetronomeVolume());
        menu.addSubMenu ("Pitch", pitch);
        menu.addSubMenu ("Volume: " + juce::String (current.getMetronomeVolume()) + "%", volume);
        const auto options = editorPopupOptions (anchor);
        const juce::Component::SafePointer<NativeEditor> safe (this);
        showAnchoredPopup (menu, options, [safe] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto& processor = safe->controller.processor;
            if (result >= volumeId)
            {
                processor.setMetronomeVolume (result - volumeId);
                safe->uiPreferences->setValue ("metronomeVolume", processor.getMetronomeVolume());
            }
            else if (result >= pitchId - 1)
            {
                processor.setMetronomePitch (result - pitchId);
                safe->uiPreferences->setValue ("metronomePitch", processor.getMetronomePitch());
            }
            else
            {
                processor.setMetronomeSound (static_cast<PluginProcessor::MetronomeSound> (result - soundId));
                safe->uiPreferences->setValue ("metronomeSound", static_cast<int> (processor.getMetronomeSound()));
            }
            safe->uiPreferences->saveIfNeeded();
        });
    }

    void NativeEditor::settingsMenu()
    {
        enum Item { keepOpen = 1, tall, zen, audioMidi };
        juce::PopupMenu menu;
        menu.setLookAndFeel (&look);
        const juce::Component::SafePointer<NativeEditor> safe (this);
        const auto addRow = [&menu] (int id, const juce::String& label, std::optional<bool> checked, std::function<void()> action = {})
        {
            auto row = std::make_unique<SettingsMenuRow> (label, checked, std::move (action));
            auto* result = row.get();
            menu.addCustomItem (id, std::move (row), nullptr, label);
            return result;
        };
        SettingsMenuRow* first = nullptr;
        if (!layout.settingsInHeader)
            first = addRow (keepOpen, "Footer tooltips", footerHelp.isHelpEnabled(), [safe]
            {
                if (safe == nullptr)
                    return;
                auto& help = safe->footerHelp;
                help.setHelpEnabled (!help.isHelpEnabled());
                help.refresh();
                safe->uiPreferences->setValue ("footerTooltips", help.isHelpEnabled());
                safe->uiPreferences->saveIfNeeded();
            });
        const auto addToggle = [&] (juce::TextButton NativeEditor::* control, const juce::String& label, bool checked)
        {
            auto* item = addRow (keepOpen, label, checked, [safe, control]
            {
                if (auto* editor = safe.getComponent())
                    (editor->*control).onClick();
            });
            if (first == nullptr)
                first = item;
        };
        addToggle (&NativeEditor::rowOrder, rowOrder.getTitle(), layout.reverseRows);
        addToggle (&NativeEditor::show16Rows, show16Rows.getTitle(), show16Rows.getToggleState());
        addToggle (&NativeEditor::cellDisplay, cellDisplay.getTitle(), !controller.graphicalCellValues);
        addToggle (&NativeEditor::dc, dc.getTitle(), controller.drumColors);
        addToggle (&NativeEditor::modifierToggle, modifierToggle.getButtonText(), controller.keyboardModifiersEnabled);
       #if JUCE_MAC
        if (tallModeAvailable())
            addRow (tall, "Tall Mode", layout.tallMode);
       #endif
       #if !JUCE_IOS
        if (layout.standalone && zenModeChanged)
            addRow (zen, "Zen mode", zenMode);
       #endif
        if (layout.standalone && controller.processor.showStandaloneAudioSettings)
        {
            menu.addSeparator();
            addRow (audioMidi, juce::String::fromUTF8 (u8"Audio/MIDI Settings…"), std::nullopt);
        }
        auto options = footerMenuOptions (settings);
        if (layout.settingsInHeader)
            options = options.withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::downwards);
        showAnchoredPopup (menu, options, [safe] (int result)
        {
            if (safe == nullptr)
                return;
           #if JUCE_MAC
            if (result == tall)
                safe->setTallMode (!safe->layout.tallMode);
           #endif
            if (result == zen && safe->zenModeChanged)
                safe->zenModeChanged (!safe->zenMode);
            else if (result == audioMidi && safe->controller.processor.showStandaloneAudioSettings)
                safe->controller.processor.showStandaloneAudioSettings();
        }, layout.touchRows);
        first->focusButton();
    }

    void NativeEditor::toggleKeyboardModifiers()
    {
        cancelCompactGestures();
        controller.keyboardModifiersEnabled = !controller.keyboardModifiersEnabled;
        uiPreferences->setValue ("keyboardModifiersEnabled", controller.keyboardModifiersEnabled);
        uiPreferences->saveIfNeeded();
        modifierKeysChanged (juce::ModifierKeys::getCurrentModifiersRealtime());
        sync();
        grid.repaint();
    }

    void NativeEditor::updateSwingScale()
    {
        const bool mpc = uiPreferences->getBoolValue ("mpcSwingScale", false);
        swing.maximum = mpc ? 100 : maxFeelPercent;
        swing.displayScale = mpc ? 0.25 : 1.0;
        swing.displayOffset = mpc ? 50.0 : 0.0;
        swing.displayDecimalPlaces = mpc ? 2 : 0;
        swing.repaint();
        if (auto* handler = swing.getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    }

    void NativeEditor::swingMenu()
    {
        const bool mpc = uiPreferences->getBoolValue ("mpcSwingScale", false);
        juce::PopupMenu menu;
        menu.setLookAndFeel (&look);
        menu.addItem (1, "Regular", true, !mpc);
        menu.addItem (2, "MPC", true, mpc);
        const auto options = editorPopupOptions (swing);
        const juce::Component::SafePointer<NativeEditor> safe (this);
        menu.showMenuAsync (options, [safe] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            safe->uiPreferences->setValue ("mpcSwingScale", result == 2);
            safe->uiPreferences->saveIfNeeded();
            safe->updateSwingScale();
        });
    }

    void NativeEditor::recordingMenu()
    {
        juce::PopupMenu menu, offsets;
        menu.setLookAndFeel (&look);
        menu.addSectionHeader ("Recording quantization");
        const auto strength = controller.processor.getPatternRecordingQuantizeStrength();
        for (const int value : { 0, 25, 50, 75, 100 })
            menu.addItem (value + 1, value == 0 ? "Off - preserve feel" : value == 100 ? "100% - grid locked" : juce::String (value) + "%", true, strength == value);
        offsets.addSectionHeader ("Negative advances; positive delays");
        const auto offset = controller.processor.getPatternRecordingOffsetMs();
        for (const int value : { -100, -50, -25, -20, -15, -10, -5, -2, -1, 0, 1, 2, 5, 10, 15, 20, 25, 50, 100 })
            offsets.addItem (1000 + value, (value > 0 ? "+" : "") + juce::String (value) + " ms", true, offset == value);
        menu.addSubMenu ("Recording offset: " + juce::String (offset) + " ms", offsets);
        const juce::Component::SafePointer<NativeEditor> safe (this);
        const auto options = editorPopupOptions (layout.phoneRowControls ? recordOptions : static_cast<juce::TextButton&> (recordQuantize));
        menu.showMenuAsync (options, [safe] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            if (result >= 900)
                safe->controller.processor.setPatternRecordingOffsetMs (result - 1000);
            else
                safe->controller.processor.setPatternRecordingQuantizeStrength (result - 1);
            safe->sync();
        });
    }

    void NativeEditor::changeListenerCallback (juce::ChangeBroadcaster* broadcaster)
    {
        if (broadcaster == uiPreferences.get())
        {
            if (uiPreferences->getBoolValue (show16RowsPreference) != show16Rows.getToggleState())
                sync();
            return;
        }
        look.refreshPalette();
        rowScroll.setColour (juce::ScrollBar::thumbColourId, Palette::white.withAlpha (0.10f));
        footerHelp.refreshInk();
        steps.captionColour = Palette::muted;
        layerHint.setColour (juce::Label::textColourId, Palette::accent);
        tempoFader.setColour (juce::Slider::trackColourId, Palette::accent);
        tempoFader.setColour (juce::Slider::thumbColourId, Palette::text);
        const bool blue = Palette::blueAccentSelected();
        compactAccent.setButtonText (Palette::accentOverride (blue) ? (blue ? "Accent: H" : "Accent: K")
                                                                   : (blue ? "Accent: Blue" : "Accent: Orange"));
        grid.refreshAccent();
        preview.sync();
        randomPanel.sync();
        syncRecordingPads();
        if (noteChooser.isVisible())
            syncNoteChooserControls();
        sendLookAndFeelChange();
        repaint();
    }
    void NativeEditor::triggerPad (int channel, int velocity, std::optional<juce::Time> touchTime)
    {
        auto timestampMs = juce::Time::getMillisecondCounterHiRes();
        if (touchTime)
        {
            // JUCE dispatches each finger in the same iOS touch event separately.
            if (!lastPadTouch || lastPadTouch->first != *touchTime)
                lastPadTouch.emplace (*touchTime, timestampMs);
            timestampMs = lastPadTouch->second;
        }
        if (controller.processor.triggerRowFromUi (channel, velocity, timestampMs) && recordingStrip.isVisible()
            && juce::isPositiveAndBelow (channel, layout.channels))
            recordingPads[static_cast<size_t> (channel)].flash (velocity, timestampMs);
    }

    void NativeEditor::globalFocusChanged (juce::Component* focused)
    {
        // A focused control can consume these keys before they bubble to the editor.
        juce::Component* target = nullptr;
        if (jamTarget || jamPlayback != JamPlayback::stopped || heldJamStopKey)
        {
            auto* root = getTopLevelComponent();
            auto* recipient = focused != nullptr ? focused : root;
            if (recipient->getTopLevelComponent() == root)
            {
                // JUCE sends keys to a blocking popup even when focus stays in the editor.
                if (recipient->isCurrentlyBlockedByAnotherModalComponent())
                    target = getCurrentlyModalComponent();
                else if (recipient != this)
                    target = recipient;
            }
        }
        if (jamKeyComponent != target)
        {
            if (jamKeyComponent != nullptr)
            {
                jamKeyComponent->removeKeyListener (this);
                if (restoreJamTextInput)
                    static_cast<juce::TextEditor*> (jamKeyComponent.getComponent())->setReadOnly (false);
            }
            restoreJamTextInput = false;
            jamKeyComponent = target;
            if (target != nullptr)
                target->addKeyListener (this);
        }
        // Native text input can consume Space before JUCE dispatches a key press.
        if (auto* field = dynamic_cast<juce::TextEditor*> (target))
        {
            if ((jamTarget || heldJamStopKey) && !field->isReadOnly())
            {
                restoreJamTextInput = true;
                field->setReadOnly (true);
            }
            else if (!jamTarget && !heldJamStopKey && std::exchange (restoreJamTextInput, false))
                field->setReadOnly (false);
        }
    }

    bool NativeEditor::keyPressed (const juce::KeyPress& key, juce::Component*)
    {
        return handleJamStopKey (key);
    }

    bool NativeEditor::keyStateChanged (bool down, juce::Component*)
    {
        return keyStateChanged (down);
    }

    bool NativeEditor::handleJamStopKey (const juce::KeyPress& key)
    {
        const int code = key.getKeyCode();
        if (heldJamStopKey == code)
            return true;
        const bool stopKey = code == juce::KeyPress::spaceKey || code == juce::KeyPress::escapeKey || code == juce::KeyPress::stopKey;
        const bool finish = jamTarget && (stopKey || code == juce::KeyPress::returnKey);
        const bool stopPlayback = stopKey && jamPlayback != JamPlayback::stopped;
        if (!finish && !stopPlayback)
            return false;
        // A held Space must not toggle transport again after finishing or stopping Jam.
        if (juce::KeyPress::isKeyCurrentlyDown (code))
            heldJamStopKey = code;
        if (finish)
            toggleJam();
        else
            stop.onClick();
        return true;
    }

    void NativeEditor::configureSelectButton()
    {
        if (layout.compactOnPhone)
        {
            selectMode.setTooltip ("Tap to toggle Select");
            selectMode.pressed = {};
            selectMode.released = {};
        }
        else
        {
            selectMode.setTooltip ("Hold for temporary Select");
            selectMode.pressed = [this] (const juce::MouseEvent& e)
            {
                if (e.mods.isLeftButtonDown())
                    controller.beginSelect();
            };
            selectMode.released = [this] (const juce::MouseEvent& e)
            {
                controller.endSelect (e.getLengthOfMousePress() >= 300, selectMode.getLocalBounds().contains (e.getPosition()));
            };
        }
    }

    void NativeEditor::toggleJam()
    {
        auto& processor = controller.processor;
        const double boundary = juce::Time::getMillisecondCounterHiRes();
        if (const auto target = std::exchange (jamTarget, std::nullopt))
        {
            showPads.setToggleState (target->padsOpen, juce::dontSendNotification);
            globalFocusChanged (getCurrentlyFocusedComponent());
            auto take = processor.finishJamCapture (boundary);
            if (take && target->source == controller.source() && target->epoch == processor.getUiBridge().getDocumentEpoch())
            {
                layout.firstStep = 0;
                if (controller.applyJamTake (*take, target->source, target->epoch))
                {
                    // Link can start playback asynchronously on the audio thread.
                    jamPlayback = JamPlayback::starting;
                    processor.setStandaloneTransportPlaying (true);
                    globalFocusChanged (getCurrentlyFocusedComponent());
                    if (take->droppedHits > 0)
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Jam",
                            juce::String (take->droppedHits) + " hits could not fit in the pattern.");
                }
            }
            sync();
            return;
        }

        if (controller.source() < 0 || !processor.hasStandaloneTransport())
            return;
        cancelCompactGestures();
        controller.commit();
        if (layout.compact && panel != Panel::none)
            showPanel (Panel::none);
        if (layout.randomVisible)
            showDesktopRandomizer (false);
        if (!processor.startJamCapture())
            return;
        jamPlayback = JamPlayback::stopped;
        jamTarget = JamTarget { controller.source(), processor.getUiBridge().getDocumentEpoch(), showPads.getToggleState(),
                                juce::Time::getMillisecondCounterHiRes() };
        controller.selected.reset();
        heldPads.reset();
        showPads.setToggleState (true, juce::dontSendNotification);
        if (isShowing())
            grabKeyboardFocus();
        globalFocusChanged (getCurrentlyFocusedComponent());
        sync();
    }

    void NativeEditor::cancelJam()
    {
        if (const auto target = std::exchange (jamTarget, std::nullopt))
        {
            globalFocusChanged (getCurrentlyFocusedComponent());
            controller.processor.cancelJamCapture();
            showPads.setToggleState (target->padsOpen, juce::dontSendNotification);
        }
    }

    void NativeEditor::syncJamButton()
    {
        const bool active = jamTarget.has_value();
        const bool capturing = active && controller.processor.pollJamCapture();
        jamButton.setEnabled (layout.standalone && (active || controller.source() >= 0));
        if (!layout.compact)
            jamButton.setVisible (layout.standalone);
        jamButton.setToggleState (active, juce::dontSendNotification);
        float waitPulse = 0.0f;
        if (jamTarget && !capturing)
        {
            constexpr double periodMs = 1100.0;
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - jamTarget->armedAtMs;
            waitPulse = 0.675f + 0.325f * static_cast<float> (std::cos (juce::MathConstants<double>::twoPi * elapsed / periodMs));
        }
        if (jamButton.getProperties().set ("jamWaitPulse", waitPulse))
            jamButton.repaint();
        jamButton.setTitle (capturing ? "Jam recording" : active ? "Jam waiting for first hit" : "Jam");
        jamButton.setTooltip (capturing ? "Press Jam, Space, Enter or Esc on the next downbeat to close the loop"
                                       : active ? "Play a pad, MIDI note or drum key to start"
                                                : "Stop playback and capture a new drum loop");
        finishJamButton.setVisible (layout.compactOnPhone && capturing);
        const bool hasFooter = !layout.compact && !layout.settingsInHeader;
        const bool showFooterPad = hasFooter && capturing;
        footerJamPad.setVisible (showFooterPad);
        footerHelp.setVisible (hasFooter && !showFooterPad);
    }

    void NativeEditor::syncRecordingPads()
    {
        const auto& playback = controller.state.playback;
        for (int channel = 0; channel < static_cast<int> (recordingPads.size()); ++channel)
        {
            const auto index = static_cast<size_t> (channel);
            auto& pad = recordingPads[index];
            const bool visible = channel < layout.rowLimit;
            const auto key = layout.standalone && channel < padShortcutKeys.length()
                ? juce::String::charToString (padShortcutKeys[channel]) : juce::String();
            const auto& row = playback.channels[index];
            const auto note = row.controller ? "CC " + juce::String (row.ccNumber)
                                             : juce::MidiMessage::getMidiNoteName (row.note, true, true, 4);
            pad.setTitle ("Trigger channel " + juce::String (channel + 1));
            pad.setVisible (visible);
            pad.setAppearance (Palette::channel (channel, controller.drumColors), key, note);
            pad.setEnabled (channel < layout.channels && playback.deviceActive && !row.controller);
            pad.positionSensitive = padVelocityMode.getToggleState();
        }
    }

    void NativeEditor::layoutRecordingPads()
    {
        const int count = layout.rowLimit;
        if (layout.phoneRowControls)
        {
            constexpr int gap = 8, switchWidth = 56, controlWidth = 68;
            const int columns = count / 2;
            const auto bounds = recordingStrip.getLocalBounds().reduced (12, 6);
            const int widthLimitedSide = (bounds.getWidth() - columns * gap - controlWidth) / columns;
            const int side = juce::jmax (0, juce::jmin (widthLimitedSide, (bounds.getHeight() - gap) / 2));
            const int totalWidth = columns * (side + gap) + controlWidth;
            const int x = bounds.getCentreX() - totalWidth / 2;
            const int y = bounds.getCentreY() - side - gap / 2;
            for (int index = 0; index < count; ++index)
            {
                const int row = index / columns;
                const int displayRow = layout.compactOnPhone ? 1 - row : row;
                recordingPads[static_cast<size_t> (index)].setBounds (x + (index % columns) * (side + gap), y + displayRow * (side + gap), side, side);
            }
            const int controlX = x + columns * (side + gap);
            if (layout.compactOnPhone)
            {
                const juce::Rectangle<int> topControl { controlX, y, bounds.getRight() - controlX, side };
                padVelocityMode.setBounds (topControl);
                finishJamButton.setBounds (topControl.translated (0, side + gap));
            }
            else
                padVelocityMode.setBounds (controlX + (controlWidth - switchWidth) / 2, bounds.getCentreY() - switchWidth / 2, switchWidth, switchWidth);
            return;
        }
        constexpr int gap = 6;
        constexpr int verticalPadding = 2;
        const int switchWidth = layout.compact ? 30 : 64;
        const int available = recordingStrip.getWidth() - 2 * Layout::outerGutter - count * gap - switchWidth;
        const int side = juce::jmax (0, juce::jmin (recordingStrip.getHeight() - 2 * verticalPadding, available / count));
        const int leftEdge = (recordingStrip.getWidth() - count * (side + gap) - switchWidth) / 2;
        const int y = (recordingStrip.getHeight() - side) / 2;
        for (int index = 0; index < count; ++index)
            recordingPads[static_cast<size_t> (index)].setBounds (leftEdge + index * (side + gap), y, side, side);
        padVelocityMode.setBounds (leftEdge + count * (side + gap), y, switchWidth, side);
    }

    void NativeEditor::syncMidiOutput()
    {
        const auto outputName = controller.processor.standaloneMidiOutputName ? controller.processor.standaloneMidiOutputName() : juce::String();
        midiOutput.setButtonText (layout.compact ? "MIDI Output: " + outputName : "MIDI Out");
        midiOutput.setTooltip ("MIDI out: " + outputName);
    }

    void NativeEditor::timerCallback()
    {
        if (!isShowing())
            return;
        const bool unsupported = controller.processor.hasUnsupportedProjectVersion();
        if (unsupported && !warnedUnsupportedProjectVersion)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Unsupported KSH project version",
                                                  "This build of KSH cannot open this project version. The original state will be kept when you save the session. Open it in a compatible version of KSH, or choose New or Load to replace it.",
                                                  {}, this);
        warnedUnsupportedProjectVersion = unsupported;
        if (jamTarget || jamPlayback != JamPlayback::stopped || heldJamStopKey)
            globalFocusChanged (getCurrentlyFocusedComponent());
        if (jamTarget)
            syncJamButton();
        footerHelp.refresh();
        syncMidiOutput();
        if (controller.processor.standaloneLinkStatus)
        {
            if (lastStandalonePlaying != controller.processor.isStandaloneTransportPlaying()
                || lastStandaloneLaunchPending != controller.processor.isStandaloneLaunchPending())
                sync();

            if (linkSettings.isVisible())
            {
                const auto [enabled, peers] = controller.processor.standaloneLinkStatus();
                const auto separator = juce::String::fromUTF8 (" · ");
                const auto state = "Ableton Link" + separator + (enabled ? "Enabled" : "Disabled");
                linkSettings.setTooltip (enabled ? state + separator + juce::String (peers) + (peers == 1 ? " peer" : " peers") : state);
                if (layout.compact)
                    linkSettings.setButtonText (state);
                linkSettings.setToggleState (enabled, juce::dontSendNotification);
            }
            syncTempo();
            if (lastStandaloneLaunchPending && stop.isVisible())
                stop.repaint();
        }
        if (midiOutput.isVisible())
        {
            constexpr double pulseLengthMs = 150.0;
            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto activity = controller.processor.getStandaloneMidiActivity();
            if (lastMidiActivity != activity)
            {
                lastMidiActivity = activity;
                lastMidiActivityMs = now;
            }
            const float pulse = juce::jmax (0.0f, static_cast<float> (1.0 - (now - lastMidiActivityMs) / pulseLengthMs));
            if (midiOutput.getProperties().set ("midiActivity", pulse * pulse))
                midiOutput.repaint();
        }
        if (layout.standalone)
        {
            float pulse = 0.0f;
            if (controller.processor.isStandaloneTransportPlaying() && !controller.processor.isStandaloneLaunchPending())
            {
                const double position = controller.processor.getStandaloneTransportPpqForUi();
                const double beatAgeMs = (position - std::floor (position)) * 60000.0 / tempoFader.getValue();
                pulse = juce::jmax (0.0f, static_cast<float> (1.0 - beatAgeMs / 150.0));
            }
            if (tempoButton.getProperties().set ("tempoPulse", pulse * pulse))
                tempoButton.repaint();
        }
        // Drain hits immediately before repainting, without an audio-thread wakeup
        // or an additional frame of delay between the processor and editor timers.
        controller.processor.emitPendingNoteHitsForUi();
        controller.poll();
        updatePatternButtons();
        if (record.getToggleState())
        {
            record.getProperties().set ("recordPulse", controller.processor.getRecordingPulseForUi());
            record.repaint();
        }
        modifierKeysChanged (juce::ModifierKeys::getCurrentModifiersRealtime());
        if (showingCompactPadPreview())
            repaint (compactPadPreviewBounds());
        else if (grid.isVisible())
            grid.updatePlayback();
        if (recordingStrip.isVisible())
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            for (auto& pad : recordingPads)
                pad.updateFlash (now);
        }
        preview.updatePlayback();
        if (layout.randomVisible || (layout.compact && panel == Panel::generate))
            randomPanel.updatePlayback();
        keyStateChanged (false);
    }
}
