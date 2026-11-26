#include "KshNativeEditor.h"
#include <numeric>

namespace ksh::ui
{
    namespace
    {
        constexpr std::array<phoneMenu::Icon, 5> menuTabIcons {
            phoneMenu::folder, phoneMenu::grid, phoneMenu::timing, phoneMenu::gear, phoneMenu::info
        };

        struct PhoneMenuLayout
        {
            PhoneMenuLayout (juce::Rectangle<int> bounds, bool patternPage)
                : tabs (bounds.withTrimmedTop (42).reduced (16, 0).withHeight (44)),
                  body (bounds.withTrimmedTop (patternPage ? 48 : 100).withTrimmedBottom (16).reduced (16, 0)),
                  left (body.withWidth ((body.getWidth() - 12) / 2)),
                  right (body.withTrimmedLeft (left.getWidth() + 12))
            {
            }

            static juce::Rectangle<int> controls (juce::Rectangle<int> card)
            {
                return card.reduced (14, 0).withTrimmedTop (44);
            }

            juce::Rectangle<float> aboutBrandColumn() const
            {
                return body.toFloat().withWidth (static_cast<float> (body.getWidth()) * 0.52f);
            }

            juce::Rectangle<int> tabs, body, left, right;
        };
    }

    void NativeEditor::setupCompactControls()
    {
        for (auto* button : { &compactAccent, &compactClear, &recordOptions, &compactPattern, &controls, &closePanel, &deselect, &page, &duplicate,
                              static_cast<juce::TextButton*> (&tempoButton), &metronomeOptions, &channelReverse, &channelPing, &channelSolo })
            addChildComponent (*button);
        addChildComponent (phoneAboutWordmark);
        phoneAboutWordmark.onAccentSelected = preview.onAccentSelected;
        phoneAboutWordmark.onThemeGesture = preview.onThemeGesture;
        phoneAboutWordmark.onAccentColourEdited = preview.onAccentColourEdited;
        phoneAboutWordmark.onBackgroundToneEdited = preview.onBackgroundToneEdited;
        phoneAboutWordmark.onTextBrightnessEdited = preview.onTextBrightnessEdited;
        tempoButton.setTitle ("Set tempo");
        tempoButton.getProperties().set ("transport", true);
        tempoButton.getProperties().set ("tempoReadout", true);
        tempoButton.setTooltip ("Click to open tempo controls. Drag up or down to change BPM.");
        tempoButton.onClick = [this] { setTempoOpen (!tempoOpen); };
        tempoButton.pressed = [this] (const juce::MouseEvent& e)
        {
            if (tempoScrub || !e.mods.isLeftButtonDown() || e.mods.isPopupMenu())
                return;
            const int bpm = juce::roundToInt (controller.processor.getStandaloneTempoBpm());
            tempoScrub = TempoScrub { e.source, bpm, bpm };
            tempoButton.setState (juce::Button::buttonDown);
        };
        tempoButton.dragged = [this] (const juce::MouseEvent& e)
        {
            if (!tempoScrub || tempoScrub->pointer != e.source)
                return;
            const float delta = e.position.y - e.mouseDownPosition.y;
            if (!tempoScrub->dragging)
            {
                if (std::abs (delta) < 4.0f)
                    return;
                tempoScrub->dragging = true;
                tempoTaps = {};
                controller.begin ("Change tempo");
            }
            const int bpm = juce::jlimit (minimumTempoBpm, maximumTempoBpm, tempoScrub->start - juce::roundToInt (delta * 0.5f));
            if (std::exchange (tempoScrub->requested, bpm) != bpm)
                controller.command ("standalone_tempo", { bpm });
        };
        tempoButton.released = [this] (const juce::MouseEvent& e)
        {
            if (!tempoScrub || tempoScrub->pointer != e.source)
                return;
            if (tempoScrub->dragging || e.getDistanceFromDragStart() >= 4)
                finishTempoScrub();
            else
            {
                tempoScrub.reset();
                const bool inside = tempoButton.getLocalBounds().contains (e.getPosition());
                tempoButton.setState (inside && e.source.canHover() ? juce::Button::buttonOver : juce::Button::buttonNormal);
                if (inside && tempoButton.isEnabled())
                    tempoButton.onClick();
            }
        };
        addChildComponent (tempoOverlay);
        tempoOverlay.dismiss = [this] { setTempoOpen (false); };
        tempoOverlay.setTitle ("Tempo");
        tempoOverlay.setWantsKeyboardFocus (true);
        for (auto* component : std::array<juce::Component*, 4> { &tempoFader, &tempoDown, &tempoUp, &tapTempo })
            tempoOverlay.addAndMakeVisible (*component);
        tempoFader.setTitle ("Tempo BPM fader");
        tempoFader.getProperties().set ("tempoReadout", true);
        tempoFader.setRange (minimumTempoBpm, maximumTempoBpm, 1);
        tempoFader.setTextValueSuffix (" BPM");
        tempoFader.setColour (juce::Slider::thumbColourId, Palette::text);
        tempoFader.onDragStart = [this] { controller.begin ("Change tempo"); };
        tempoFader.onDragEnd = [this] { controller.commit(); };
        tempoFader.onValueChange = [this]
        {
            tempoTaps = {};
            controller.command ("standalone_tempo", { juce::roundToInt (tempoFader.getValue()) }, "Change tempo");
        };
        tempoDown.setTitle ("Decrease tempo by 1 BPM");
        tempoUp.setTitle ("Increase tempo by 1 BPM");
        for (auto* stepper : { &tempoDown, &tempoUp })
        {
            stepper->getProperties().set ("tempoStepper", false);
            stepper->setRepeatSpeed (400, 120, 60);
        }
        tempoDown.onClick = [this] { tempoFader.setValue (tempoFader.getValue() - 1, juce::sendNotificationSync); };
        tempoUp.onClick = [this] { tempoFader.setValue (tempoFader.getValue() + 1, juce::sendNotificationSync); };
        tapTempo.setTooltip ("Tap once per beat. Tempo updates after three taps.");
        tapTempo.onClick = [this] { registerTempoTap(); };
        metronomeOptions.onClick = [this] { metronomeMenu (metronomeOptions); };
        addChildComponent (visibleRows);
        visibleRows.setTitle ("Visible rows");
        visibleRows.setTooltip ("Scroll to reach hidden rows");
        visibleRows.onClick = [this] { visibleRowsMenu(); };
        addChildComponent (rowScroll);
        rowScroll.setTitle ("Scroll rows");
        rowScroll.setSingleStepSize (1);
        rowScroll.addListener (this);
        grid.scrollRows = [this] (int delta) { scrollCompactRows (delta); };
        grid.revealKeyboardCell = [this] (Location location)
        {
            if (!layout.compact)
                return;

            const int row = layout.displayRow (location.channel);
            layout.firstRow = juce::jlimit (row - layout.compactVisibleRows + 1, row, layout.firstRow);
            layout.firstStep = location.step / Layout::compactStepsPerPage * Layout::compactStepsPerPage;
            sync();
        };
       #if JUCE_IOS
        preferredPhoneRows = juce::jlimit (4, 8, uiPreferences->getIntValue ("iphoneVisibleRows", 8));
       #endif
        addChildComponent (patternPicker);
        patternPicker.onDismiss = [this]
        {
            sync();
            if (isShowing())
                compactPattern.grabKeyboardFocus();
        };
        compactAccent.setTitle ("Switch accent");
        compactAccent.onClick = [this] { preview.onAccentSelected (!Palette::blueAccentSelected()); };
        compactClear.setTitle ("Clear pattern");
        recordOptions.setTitle ("Recording timing options");
        recordOptions.onClick = [this] { recordingMenu(); };
        compactClear.onClick = [this]
        {
            const auto epoch = controller.processor.getUiBridge().getDocumentEpoch();
            if (confirmPatternClear && clearSource == controller.source() && clearEpoch == epoch)
            {
                compactClearFlash.cancel();
                controller.clearPattern();
                confirmPatternClear = false;
                if (layout.phoneRowControls)
                {
                    returnToPhoneGrid();
                    return;
                }
            }
            else
            {
                compactClearFlash.start();
                confirmPatternClear = true;
                clearSource = controller.source();
                clearEpoch = epoch;
            }
            sync();
        };
        compactPattern.setTitle ("Pattern bank");
        controls.setTitle ("Open controls");
        deselect.setTitle ("Deselect cells");
        deselect.setButtonText ("Done");
        duplicate.setTitle ("Copy selected cells when dragging");
        page.setTitle ("Switch step page");
        page.setTooltip ("Show the next step page");
        page.getProperties().set ("stepPage", true);
        compactPattern.onClick = [this]
        {
            if (patternPicker.isVisible())
                patternPicker.dismiss();
            else
            {
                cancelCompactGestures();
                patternPicker.open (layout.phoneRowControls);
                sync();
            }
        };
        controls.onClick = [this]
        {
            const auto firstPage = layout.phoneRowControls && lastSettingsPanel == Panel::pattern ? Panel::project : lastSettingsPanel;
            showPanel (panel == Panel::none || panel == Panel::generate ? firstPage : Panel::none);
        };
        closePanel.onClick = [this] { showPanel (Panel::none); };
        addChildComponent (noteChooserBack);
        noteChooserBack.setTitle ("Back from MIDI chooser");
        noteChooserBack.getProperties().set ("phoneMenuIcon", static_cast<int> (phoneMenu::previous));
        noteChooserBack.onClick = [this] { noteChooser.dismiss(); };
        deselect.onClick = [this]
        {
            grid.cancelGesture();
            controller.selected.reset();
            controller.cancelSelect();
            grid.copySelection = false;
            sync();
        };
        duplicate.onClick = [this] { grid.copySelection = !grid.copySelection; sync(); };
        page.onClick = [this]
        {
            grid.cancelGesture();
            const auto next = layout.firstStep + Layout::compactStepsPerPage;
            layout.firstStep = next < layout.steps ? next : 0;
            sync();
        };
        const std::array<juce::String, 5> names { "Project", "Pattern", "Feel", "View", "Help" };
        for (int i = 0; i < static_cast<int> (names.size()); ++i)
        {
            auto& tab = panelTabs[static_cast<size_t> (i)];
            addChildComponent (tab);
            tab.setButtonText (names[static_cast<size_t> (i)]);
            tab.onClick = [this, i]
            {
                const auto destination = static_cast<Panel> (i + static_cast<int> (Panel::project));
                showPanel (layout.phoneRowControls && destination == Panel::pattern && panel == destination ? Panel::none : destination);
            };
        }
        grid.channelSettings = [this] (int channel)
        {
            selectedChannel = channel;
            showPanel (Panel::channel);
        };
        for (auto* value : { &loopStart, &loopLength })
        {
            addChildComponent (*value);
            value->valueFontSize = 18;
            value->onBegin = [this] { controller.begin ("Channel settings"); };
            value->onEnd = [this] { controller.commit(); };
            value->onCancel = [this] { controller.cancel(); };
        }
        addChildComponent (channelNote);
        channelNote.valueFontSize = 18;
        channelNote.caption = "MIDI note";
        channelNote.setTitle ("Channel MIDI note");
        channelNote.minimum = 0;
        channelNote.maximum = 127;
        channelNote.format = [this] (int note)
        {
            return controller.isControllerRow (selectedChannel) ? "CC " + juce::String (note)
                : juce::MidiMessage::getMidiNoteName (note, true, true, 4) + " / " + juce::String (note);
        };
        channelNote.onChoose = [this] { showNoteChooser (selectedChannel, channelNote); };
        loopStart.caption = "Loop start";
        loopStart.setTitle ("Channel loop start");
        loopStart.minimum = 1;
        loopStart.onChange = [this] (int v)
        {
            const auto loop = controller.loop (selectedChannel);
            controller.command ("channel_loop_length", { selectedChannel + 1, juce::jmin (loop.loopLength, layout.steps - v + 1), v });
        };
        loopLength.caption = "Loop length";
        loopLength.setTitle ("Channel loop length");
        loopLength.minimum = 1;
        loopLength.onChange = [this] (int v) { controller.command ("channel_loop_length", { selectedChannel + 1, v, controller.loop (selectedChannel).loopStart + 1 }); };
        channelReverse.onClick = [this] { grid.toggleDirection (selectedChannel, true); };
        channelPing.onClick = [this] { grid.toggleDirection (selectedChannel, false); };
        channelSolo.onClick = [this] { controller.toggleSolo (selectedChannel); };
    }

    void NativeEditor::setCompactViewport (bool compact, int width, int height, bool phone)
    {
        const bool onPhone = compact && phone;
        const bool auv3 = controller.processor.wrapperType == juce::AudioProcessor::wrapperType_AudioUnitv3;
        const bool tabletPlugin = !phone && layout.touchRows && auv3;
        const bool usePhoneLayout = onPhone || (compact && tabletPlugin);
        const bool settingsInHeader = !compact && tabletPlugin;
        if (layout.compact == compact && layout.phoneRowControls == usePhoneLayout && layout.compactOnPhone == onPhone
            && layout.settingsInHeader == settingsInHeader)
        {
            if (compact && (juce::roundToInt (layout.compactWidth) != width || juce::roundToInt (layout.compactHeight) != height))
            {
                cancelCompactGestures();
                if (patternPicker.isVisible())
                    patternPicker.dismiss();
                trackColourPopup.reset();
                trackColourContent.reset();
                layout.compactWidth = static_cast<float> (width);
                layout.compactHeight = static_cast<float> (height);
                sync();
            }
            return;
        }
        cancelCompactGestures();
        if (patternPicker.isVisible())
            patternPicker.dismiss();
        trackColourPopup.reset();
        trackColourContent.reset();
        layout.compact = compact;
        layout.phoneRowControls = usePhoneLayout;
        noteChooser.setMenuPage (usePhoneLayout);
        layout.compactOnPhone = onPhone;
        layout.compactHeaderTopInset = usePhoneLayout && auv3 ? 4 : 0;
        padVelocityMode.phonePadStyle = onPhone;
        layout.settingsInHeader = settingsInHeader;
        if (usePhoneLayout)
            grid.copySelection = false;
        compactPattern.getProperties().set ("patternBankNumber", usePhoneLayout);
        controls.setButtonText (usePhoneLayout ? "Menu" : "Settings");
        controls.setTitle (usePhoneLayout ? "Open menu" : "Open controls");
        panelTabs[3].setButtonText (usePhoneLayout ? "Settings" : "View");
        panelTabs[4].setButtonText (usePhoneLayout ? "About" : "Help");
        auto styleMenuButton = [usePhoneLayout] (juce::TextButton& button, phoneMenu::Icon icon, const char* label = "")
        {
            auto& properties = button.getProperties();
            if (usePhoneLayout)
            {
                properties.set ("phoneMenuIcon", static_cast<int> (icon));
                properties.set ("phoneMenuLabel", label);
            }
            else
            {
                properties.remove ("phoneMenuIcon");
                properties.remove ("phoneMenuLabel");
            }
        };
        for (size_t i = 0; i < panelTabs.size(); ++i)
        {
            if (i != 1)
                styleMenuButton (panelTabs[i], onPhone && i == 2 ? phoneMenu::twoNotes : menuTabIcons[i]);
            panelTabs[i].getProperties().set ("phoneMenuTab", usePhoneLayout && i != 1);
        }
        const std::array<phoneMenu::Icon, 6> projectIcons {
            phoneMenu::add, phoneMenu::folder, phoneMenu::save, phoneMenu::previous, phoneMenu::next, phoneMenu::saveAs
        };
        for (size_t i = 0; i < projectButtons.size(); ++i)
            styleMenuButton (projectButtons[i], projectIcons[i]);
        styleMenuButton (projectHeader, phoneMenu::info);
        styleMenuButton (compactClear, phoneMenu::trash);
        styleMenuButton (recordOptions, phoneMenu::clock, "Timing options");
        styleMenuButton (showSwing, phoneMenu::eye, "Show swing on grid");
        styleMenuButton (dc, phoneMenu::palette);
        styleMenuButton (cellDisplay, phoneMenu::values, "Show values");
        styleMenuButton (rowOrder, phoneMenu::rows);
        styleMenuButton (compactAccent, phoneMenu::palette);
        styleMenuButton (modifierToggle, phoneMenu::keyboard);
        styleMenuButton (visibleRows, phoneMenu::grid);
        styleMenuButton (metronomeOptions, phoneMenu::metronome, "Metronome sound");
        styleMenuButton (midiOutput, phoneMenu::midi, "MIDI");
        midiOutput.getProperties().set ("phoneMenuTab", usePhoneLayout);
        styleMenuButton (linkSettings, phoneMenu::link);
        for (auto* button : { &compactPattern, &controls, &panelTabs[1] })
            if (usePhoneLayout)
                button->getProperties().set ("dropdown", true);
            else
                button->getProperties().remove ("dropdown");
        for (auto* button : { &controls, &panelTabs[1] })
            button->getProperties().set ("dropdownInsetReduction", usePhoneLayout ? phoneDropdownInsetReduction : 0);
        jamButton.getProperties().set ("hideJamLabel", usePhoneLayout);
        jamButton.setButtonText (usePhoneLayout ? juce::String() : juce::String ("Jam"));
        metronome.openSettingsOnHold = !usePhoneLayout;
        metronome.showSettings = usePhoneLayout ? std::function<void()>() : [this] { metronomeMenu (metronome); };
        metronome.setTooltip (usePhoneLayout ? "Toggle metronome" : "Right-click or hold for metronome settings");
        recordQuantize.showSettings = usePhoneLayout ? std::function<void()>() : [this] { recordingMenu(); };
        chanceLayer.setButtonText (usePhoneLayout ? "Prob" : "Chance");
        chanceLayer.setTooltip (usePhoneLayout ? "Hold for temporary Prob" : "Hold here or hold Option for temporary Chance");
        configureSelectButton();
        layout.compactVisibleRows = onPhone ? preferredPhoneRows : 8;
        layout.firstRow = 0;
        layout.compactWidth = static_cast<float> (width);
        layout.compactHeight = static_cast<float> (height);
        layout.firstStep = 0;
        panel = Panel::none;
        generate.setButtonText (usePhoneLayout ? juce::String() : juce::String ("Generate"));
        generate.setToggleState (false, juce::dontSendNotification);
        setTempoOpen (false);
        if (compact)
            aboutViewport.setScrollBarThickness (0);
        preview.setCompact (compact, usePhoneLayout, onPhone);
        randomPanel.setCompact (compact, usePhoneLayout, onPhone);
        velocityLayer.setButtonText (compact ? "Vel" : "Velocity");
        const std::array<juce::String, 6> compactLabels { "New", "Open", "Save", "Previous project", "Next project", "Save as" };
        const std::array<juce::String, 6> desktopLabels { "NEW", "LOAD", "SAVE", juce::String::fromUTF8 ("◀"), juce::String::fromUTF8 ("▶"), "SAVE AS" };
        for (size_t i = 0; i < compactLabels.size(); ++i)
            projectButtons[i].setButtonText (compact ? compactLabels[i] : desktopLabels[i]);
        for (const auto index : { 0u, 1u, 2u, 5u })
            projectButtons[index].getProperties().set ("insetProjectArtwork", !compact);
        projectButtons[3].getProperties().set ("icon", compact ? "" : "projectPrevious");
        projectButtons[4].getProperties().set ("icon", compact ? "" : "projectNext");
        if (!compact)
        {
            for (auto* control : std::initializer_list<juce::Component*> {
                     &projectHeader, &swing, &subdivision, &showSwing, &velocityHumanize, &timingHumanize, &mutePattern,
                     &velocityLayer, &chanceLayer, &selectMode, &undoButton, &redoButton, &rate, &steps, &clear,
                     &left, &right, &recordQuantize, &record, &showPads, &generate, &grid })
                control->setVisible (true);
            for (auto& button : projectButtons)
                button.setVisible (true);
            for (auto& button : patterns)
                button.setVisible (true);

            const auto compactOnly = std::initializer_list<juce::Component*> {
                &visibleRows, &rowScroll, &compactAccent, &compactClear, &recordOptions, &compactPattern, &controls,
                &closePanel, &noteChooserBack, &deselect, &page, &duplicate, &modifierToggle, &channelNote, &loopStart, &loopLength,
                &aboutViewport, &phoneAboutWordmark, &metronomeOptions, &channelReverse, &channelPing, &channelSolo
            };
            for (auto* control : compactOnly)
                control->setVisible (false);
            for (auto& tab : panelTabs)
                tab.setVisible (false);
            layout.randomVisible = false;
        }
        sync();
    }

    void NativeEditor::setCompactVisibleRows (int count)
    {
        if (!layout.compactOnPhone)
            return;
        cancelCompactGestures();
        noteChooser.dismiss();
        preferredPhoneRows = juce::jlimit (4, 8, count);
        layout.compactVisibleRows = preferredPhoneRows;
       #if JUCE_IOS
        uiPreferences->setValue ("iphoneVisibleRows", preferredPhoneRows);
        uiPreferences->saveIfNeeded();
       #endif
        sync();
    }

    void NativeEditor::visibleRowsMenu()
    {
        juce::PopupMenu menu;
        for (int count = 4; count <= 8; ++count)
            menu.addItem (count, juce::String (count) + " rows", true, layout.compactVisibleRows == count);
        juce::Component::SafePointer<NativeEditor> safe (this);
        auto options = juce::PopupMenu::Options().withTargetComponent (&visibleRows)
            .withStandardItemHeight (44).withMinimumWidth (visibleRows.getWidth()).withMaximumNumColumns (1);
       #if JUCE_IOS
        options = options.withParentComponent (getTopLevelComponent());
       #endif
        menu.showMenuAsync (options, [safe] (int count)
        {
            if (safe != nullptr && count > 0)
                safe->setCompactVisibleRows (count);
        });
    }

    void NativeEditor::scrollCompactRows (int delta)
    {
        if (!layout.compact || !layout.phoneRowControls)
            return;
        const int next = juce::jlimit (0, juce::jmax (0, layout.channels - layout.compactVisibleRows), layout.firstRow + delta);
        if (next == layout.firstRow)
            return;
        cancelCompactGestures();
        noteChooser.dismiss();
        layout.firstRow = next;
        sync();
    }

    void NativeEditor::scrollBarMoved (juce::ScrollBar* bar, double start)
    {
        if (bar == &rowScroll)
            scrollCompactRows (juce::roundToInt (start) - layout.firstRow);
    }

    void NativeEditor::cancelCompactGestures()
    {
        finishTempoScrub();
        preview.cancelGesture();
        randomPanel.cancelGesture();
        for (auto* value : { &swing, &subdivision, &velocityHumanize, &timingHumanize, &steps, &loopStart, &loopLength })
            value->cancelGesture();
        grid.cancelGesture();
    }

    void NativeEditor::showPanel (Panel next)
    {
        cancelCompactGestures();
        controller.commit();
        if (patternPicker.isVisible())
            patternPicker.dismiss();
        confirmPatternClear = false;
        compactClearFlash.cancel();
        setTempoOpen (false);
        panel = next;
        if (next == Panel::generate)
            aboutViewport.setViewPosition (0, 0);
        if (next >= Panel::project && next <= Panel::help && !phonePatternPanel())
            lastSettingsPanel = next;
        if (isShowing())
            grabKeyboardFocus();
        sync();
        repaint();
    }

    bool NativeEditor::phonePatternPanel() const
    {
        return layout.phoneRowControls && panel == Panel::pattern;
    }

    void NativeEditor::returnToPhoneGrid()
    {
        if (layout.phoneRowControls)
        {
            showPads.setToggleState (false, juce::dontSendNotification);
            showPanel (Panel::none);
        }
    }

    void NativeEditor::TempoOverlay::paint (juce::Graphics& g)
    {
        {
            const juce::Graphics::ScopedSaveState clip (g);
            g.excludeClipRegion (triggerBounds);
            g.fillAll (juce::Colours::black.withAlpha (0.28f));
        }
        paintPopupPanel (g, outline, Palette::accent, panelGlow);
    }

    void NativeEditor::TempoOverlay::mouseDown (const juce::MouseEvent& event)
    {
        if (!card.contains (event.getPosition()) && !outline.contains (event.position) && dismiss)
            dismiss();
    }

    void NativeEditor::setTempoOpen (bool open)
    {
        if (tempoOpen == open || (open && (!layout.standalone || !tempoButton.isVisible() || patternPicker.isVisible())))
            return;
        if (open)
        {
            cancelCompactGestures();
            syncTempo();
        }
        tempoTaps = {};
        tempoOpen = open;
        tempoButton.getProperties().set ("tempoPopupOpen", open);
        tempoButton.repaint();
        tempoOverlay.setVisible (open);
        if (open)
        {
            layoutTempoPopup();
            tempoOverlay.toFront (false);
        }
        if (isShowing())
            (open ? static_cast<juce::Component&> (tempoOverlay) : tempoButton).grabKeyboardFocus();
    }

    void NativeEditor::layoutTempoPopup()
    {
        const bool wide = !layout.compactOnPhone;
        const int target = layout.compact ? 52 : Layout::touchTarget;
        const int inset = wide ? 20 : 12;
        const int readoutHeight = wide ? 46 : 28;
        const int width = wide ? 460 : 340;
        const int gap = 8;
        const int popupGap = 16;
        const int height = 2 * target + readoutHeight + gap + 2 * inset;
        tempoOverlay.setBounds (getLocalBounds());
        tempoOverlay.triggerBounds = tempoOverlay.getLocalArea (&tempoButton, tempoButton.getLocalBounds());
        tempoOverlay.card = juce::Rectangle<int> (tempoOverlay.triggerBounds.getCentreX() - width / 2,
                                                  tempoOverlay.triggerBounds.getBottom() + popupGap, width, height)
                                .constrainedWithin (getLocalBounds().reduced (8));
        tempoOverlay.outline = popupPanelOutline (tempoOverlay.card.toFloat().reduced (0.5f), wide ? 10.0f : 8.0f,
                                                  tempoOverlay.triggerBounds.toFloat());
        auto controlsRow = tempoOverlay.card.reduced (inset);
        const auto tapRow = controlsRow.removeFromBottom (target);
        tapTempo.setBounds (tapRow.withSizeKeepingCentre (juce::jmin (tapRow.getWidth(), 148), target));
        controlsRow.removeFromBottom (gap);
        for (auto* stepper : { &tempoDown, &tempoUp })
            stepper->getProperties().set ("tempoStepper", wide);
        tempoDown.setBounds (controlsRow.removeFromLeft (target).withTrimmedTop (readoutHeight));
        tempoUp.setBounds (controlsRow.removeFromRight (target).withTrimmedTop (readoutHeight));
        tempoFader.getProperties().set ("wideTempoFader", wide);
        tempoFader.setTextBoxStyle (juce::Slider::TextBoxAbove, false, wide ? 144 : 104, readoutHeight);
        tempoFader.setBounds (controlsRow.reduced (10, 0));
        tempoOverlay.repaint();
    }

    void NativeEditor::syncTempo()
    {
        const int bpm = tempoScrub && tempoScrub->dragging ? tempoScrub->requested
            : juce::roundToInt (controller.processor.getStandaloneTempoBpm());
        tempoButton.setButtonText (juce::String (bpm) + " BPM");
        if (!tempoFader.isMouseButtonDown())
            tempoFader.setValue (bpm, juce::dontSendNotification);
        tempoDown.setEnabled (bpm > minimumTempoBpm);
        tempoUp.setEnabled (bpm < maximumTempoBpm);
    }

    void NativeEditor::finishTempoScrub()
    {
        if (const auto scrub = std::exchange (tempoScrub, std::nullopt))
        {
            tempoButton.setState (juce::Button::buttonNormal);
            if (scrub->dragging)
                controller.commit();
        }
    }

    void NativeEditor::registerTempoTap()
    {
        constexpr double msPerMinute = 60000.0;
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double interval = now - tempoTaps.lastMs.value_or (now);
        if (!tempoTaps.lastMs || interval > msPerMinute / minimumTempoBpm)
        {
            tempoTaps = {};
            tempoTaps.lastMs = now;
            return;
        }
        if (interval < msPerMinute / maximumTempoBpm)
            return;

        tempoTaps.lastMs = now;
        tempoTaps.intervals[tempoTaps.next] = interval;
        tempoTaps.next = (tempoTaps.next + 1) % tempoTaps.intervals.size();
        tempoTaps.count = juce::jmin (tempoTaps.count + 1, tempoTaps.intervals.size());
        if (tempoTaps.count >= 2)
        {
            const double total = std::accumulate (tempoTaps.intervals.begin(), tempoTaps.intervals.end(), 0.0);
            const int bpm = juce::roundToInt (msPerMinute * static_cast<double> (tempoTaps.count) / total);
            controller.command ("standalone_tempo", { bpm });
        }
    }

    void NativeEditor::syncChannel()
    {
        selectedChannel = juce::jlimit (0, layout.channels - 1, selectedChannel);
        const auto& channel = controller.state.playback.channels[static_cast<size_t> (selectedChannel)];
        const juce::String caption = channel.controller ? "MIDI CC" : "MIDI note";
        if (channelNote.caption != caption)
        {
            channelNote.caption = caption;
            channelNote.setTitle (caption);
            channelNote.repaint();
        }
        channelNote.setValue (channel.controller ? channel.ccNumber : channel.note);
        const auto loop = controller.loop (selectedChannel);
        loopStart.maximum = layout.steps;
        loopStart.setValue (loop.loopStart + 1);
        loopLength.maximum = layout.steps - loop.loopStart;
        loopLength.setValue (loop.loopLength);
        loopStart.setEnabled (controller.source() >= 0);
        loopLength.setEnabled (controller.source() >= 0);
        channelReverse.setToggleState (channel.playbackMode == PlaybackMode::reverse || channel.playbackMode == PlaybackMode::reverse_ping_pong, juce::dontSendNotification);
        channelPing.setToggleState (channel.playbackMode == PlaybackMode::ping_pong || channel.playbackMode == PlaybackMode::reverse_ping_pong, juce::dontSendNotification);
        const bool solo = controller.soloedChannels().test (static_cast<size_t> (selectedChannel));
        channelSolo.setButtonText (solo ? "Exit solo" : "Solo");
        channelSolo.setToggleState (solo, juce::dontSendNotification);
        for (auto* button : { &channelReverse, &channelPing, &channelSolo })
            button->setEnabled (controller.source() >= 0);
    }

    bool NativeEditor::showingCompactPadPreview() const
    {
        return layout.phoneRowControls && panel == Panel::none && showingRecordingPads() && !patternPicker.isVisible();
    }

    bool NativeEditor::showingRecordingPads() const
    {
        return showPads.getToggleState() && (controller.selected.none() || controller.selection (true).empty());
    }

    juce::Rectangle<int> NativeEditor::compactPadPreviewBounds() const
    {
        return { 0, layout.compactHeaderBottom(), getWidth(), 76 };
    }

    void NativeEditor::paintCompactPadPreview (juce::Graphics& g) const
    {
        const auto bounds = compactPadPreviewBounds().toFloat();
        g.setColour (Palette::shell);
        g.fillRect (bounds);
        if (layout.channels <= 0 || layout.steps <= 0)
            return;

        const auto tracks = bounds.reduced (12.0f, 8.0f).withTrimmedLeft (22.0f);
        const float stepWidth = tracks.getWidth() / static_cast<float> (layout.steps);
        const float trackHeight = tracks.getHeight() / static_cast<float> (layout.channels);
        const int playhead = controller.playingStep - 1;
        if (juce::isPositiveAndBelow (playhead, layout.steps))
        {
            g.setColour (Palette::accent.withAlpha (0.28f));
            g.fillRect (tracks.withX (tracks.getX() + static_cast<float> (playhead) * stepWidth).withWidth (stepWidth));
        }

        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto& playback = controller.state.playback;
        const int beatStride = Layout::beatAccentStride (playback.beatsPerStep);
        const int source = controller.source();
        for (int row = 0; row < layout.channels; ++row)
        {
            const int channel = layout.channelForRow (row);
            const auto index = static_cast<size_t> (channel);
            const auto loop = controller.loop (channel);
            const bool muted = source >= 0 && playback.sourceChannelMutes[static_cast<size_t> (source)][index];
            const auto colour = Palette::channel (channel, controller.drumColors);
            const float y = tracks.getY() + static_cast<float> (row) * trackHeight;
            g.setColour (colour.withAlpha (muted ? 0.28f : 0.75f));
            g.fillRoundedRectangle (14.0f, y + 1.0f, 12.0f, trackHeight - 2.0f, 1.0f);
            for (int step = 0; step < layout.steps; ++step)
            {
                const bool inLoop = step >= loop.loopStart && step < loop.loopStart + loop.loopLength;
                const bool active = inLoop && controller.cell ({ channel, step }).enabled;
                const bool hit = active && !muted && controller.sourceFlashes[index][static_cast<size_t> (step)] > now;
                const float opacity = muted ? 0.13f : 0.8f;
                g.setColour (hit ? colour.brighter (0.6f) : active ? colour.withAlpha (opacity)
                                                                 : Palette::border.withAlpha (inLoop && step % beatStride == 0 ? 0.75f : 0.32f));
                const float x = tracks.getX() + static_cast<float> (step) * stepWidth;
                g.fillRoundedRectangle (x + 1.0f, y + 1.0f, stepWidth - 2.0f, trackHeight - 2.0f, 1.0f);
            }
        }
    }

    void NativeEditor::resizedCompact()
    {
        // Grid tools belong to editing; settings and channel pages have their own navigation.
        std::vector<juce::Component*> visible;
        auto finish = [this, &visible]
        {
            if (auto* presets = grid.openPresetMenu(); presets != nullptr && presets->getParentComponent() == this)
                visible.push_back (presets);
            if (tempoOpen)
            {
                layoutTempoPopup();
                visible.push_back (&tempoOverlay);
            }
            if (projectBrowser.isVisible())
                visible.push_back (&projectBrowser);
            if (noteChooser.isVisible())
                visible.push_back (&noteChooser);
            if (discardChanges.isVisible())
                visible.push_back (&discardChanges);
            if (patternPicker.isVisible())
                visible.push_back (&patternPicker);
            if (trackColourPopup != nullptr && trackColourPopup->isVisible())
                visible.push_back (trackColourPopup.get());
            for (auto* child : getChildren())
                child->setVisible (std::find (visible.begin(), visible.end(), child) != visible.end());
            syncNoteChooserGrid();
            if (tempoOpen)
                tempoOverlay.toFront (false);
            if (patternPicker.isVisible())
                patternPicker.toFront (false);
            if (projectBrowser.isVisible())
                projectBrowser.toFront (false);
        };
        auto place = [&visible] (juce::Component& component, int x, int y, int w, int h)
        {
            component.setBounds (x, y, w, h);
            visible.push_back (&component);
        };
        const int w = getWidth();
        const bool phone = layout.phoneRowControls;
        const bool gridHeader = panel == Panel::none || panel == Panel::generate;
        const bool patternOpen = phonePatternPanel();
        const bool menuOpen = phone && !patternOpen && panel >= Panel::project && panel <= Panel::help;
        auto& patternButton = panelTabs[1];
        const int toolbarInset = phone ? 0 : 2;
        const int toolbarY = toolbarInset + layout.compactHeaderTopInset;
        const int toolbarHeight = Layout::compactHeaderHeight - 2 * toolbarInset;
        const int headerBottom = layout.compactHeaderBottom();
        if (showingNoteChooserPage())
        {
            const PhoneMenuLayout menu (getLocalBounds(), true);
            place (noteChooserBack, menu.body.getX(), toolbarY, 88, toolbarHeight);
            noteChooser.setBounds (menu.body);
            finish();
            return;
        }
        controls.setToggleState (menuOpen, juce::dontSendNotification);
        generate.setToggleState (panel == Panel::generate, juce::dontSendNotification);
        if (phone)
        {
            patternButton.setToggleState (patternOpen, juce::dontSendNotification);
            patternButton.setTitle (patternOpen ? "Close pattern settings" : "Open pattern settings");
        }
        else
            patternButton.setTitle ({});
        if (phone)
            controls.setTitle (menuOpen ? "Close menu" : "Open menu");
        patternPicker.setBounds (0, headerBottom, w, getHeight() - headerBottom);
        auto placePlay = [&] (int x, int width)
        {
            play.setBounds (x, toolbarY, width, toolbarHeight);
            stop.setBounds (play.getBounds());
            visible.push_back (controller.processor.isStandaloneTransportPlaying() ? &stop : &play);
        };
        const bool patternBankOpen = phone && patternPicker.isVisible();
        const bool multiplePages = layout.steps > Layout::compactStepsPerPage;
        const bool showSelectionActions = !layout.compactOnPhone && (controller.selected.any() || controller.shiftDown ({}));
        if (phone && (gridHeader || menuOpen || patternOpen))
        {
            const bool denseHeader = multiplePages || showSelectionActions;
            const int historyWidth = denseHeader ? 34 : 38;
            const bool standalone = layout.standalone;
            const bool groupedHeader = controller.processor.wrapperType == juce::AudioProcessor::wrapperType_AudioUnitv3;
            constexpr int buttonWidth = 42;
            constexpr int groupGap = 10;
            constexpr int sectionGap = 2 * groupGap;
            const int patternWidth = dropdownWidth (patternButton.getButtonText()) - 2 * phoneDropdownInsetReduction;
            const int menuWidth = dropdownWidth (controls.getButtonText()) - 2 * phoneDropdownInsetReduction;
            auto& transport = controller.processor.isStandaloneTransportPlaying() ? stop : play;
            struct HeaderControl
            {
                juce::Component& component;
                int width;
                bool startsGroup = true;
            };
            const HeaderControl header[] {
                { compactPattern, denseHeader ? 58 : 66 },
                { velocityLayer, denseHeader ? 33 : 46 },
                { chanceLayer, denseHeader ? 46 : 54, false },
                { selectMode, denseHeader ? 38 : 54, false },
                { deselect, showSelectionActions ? 28 : 0 },
                { page, multiplePages ? 34 : 0 },
                { undoButton, historyWidth },
                { redoButton, historyWidth, false },
                { generate, buttonWidth },
                { jamButton, standalone ? buttonWidth : 0 },
                { showPads, buttonWidth },
                { record, buttonWidth },
                { tempoButton, standalone ? (denseHeader ? 56 : 64) : 0 },
                { transport, standalone ? buttonWidth : 0 },
                { patternButton, patternWidth },
                { controls, menuWidth },
            };
            juce::Component* headerOnly = patternOpen ? &patternButton : menuOpen ? &controls : patternBankOpen ? &compactPattern : nullptr;
            int freeWidth = w, gaps = -1;
            for (const auto& item : header)
            {
                freeWidth -= item.width;
                if (item.width > 0 && item.startsGroup)
                    ++gaps;
            }

            const int performanceWidth = 3 * buttonWidth + 2 * groupGap;
            const int navigationX = w - patternWidth - groupGap - menuWidth;
            int x = 0, gapIndex = 0, distributed = 0;
            for (const auto& item : header)
            {
                if (item.width == 0)
                    continue;
                if (x != 0 && item.startsGroup)
                {
                    if (groupedHeader)
                    {
                        if (&item.component == &generate)
                            x = navigationX - sectionGap - performanceWidth;
                        else if (&item.component == &patternButton)
                            x = navigationX;
                        else
                            x += groupGap;
                    }
                    else
                    {
                        // Spread rounding across the gaps so neither edge gets leftover padding.
                        const int next = ++gapIndex * freeWidth / gaps;
                        x += next - distributed;
                        distributed = next;
                    }
                }
                if (headerOnly == nullptr || headerOnly == &item.component)
                    place (item.component, x, toolbarY, item.width, toolbarHeight);
                x += item.width;
            }
            if (standalone && headerOnly == nullptr)
                (&transport == &play ? stop : play).setBounds (transport.getBounds());
        }
        if (gridHeader)
        {
            if (patternBankOpen)
            {
                finish();
                return;
            }
            if (!phone)
            {
                place (compactPattern, 6, toolbarY, 108, toolbarHeight);
                place (velocityLayer, 120, toolbarY, 46, toolbarHeight);
                place (chanceLayer, 166, toolbarY, 54, toolbarHeight);
                place (selectMode, 222, toolbarY, 48, toolbarHeight);
                if (showSelectionActions)
                {
                    place (deselect, 275, toolbarY, 40, toolbarHeight);
                    if (controller.selected.any())
                        place (duplicate, 319, toolbarY, 42, toolbarHeight);
                }
                if (multiplePages)
                    place (page, 366, toolbarY, 36, toolbarHeight);
                place (undoButton, 404, toolbarY, 28, toolbarHeight);
                place (redoButton, 432, toolbarY, 28, toolbarHeight);
                if (layout.standalone)
                {
                    int edge = w - 6;
                    const auto fromRight = [&] (juce::Component& button, int width)
                    {
                        place (button, edge - width, toolbarY, width, toolbarHeight);
                        edge -= width + 2;
                    };
                    fromRight (controls, 61);
                    fromRight (record, 32);
                    fromRight (showPads, 32);
                    fromRight (jamButton, 32);
                    fromRight (generate, 68);
                    fromRight (metronome, 32);
                    fromRight (tempoButton, 58);
                    placePlay (edge - 32, 32);
                }
                else
                {
                    place (controls, w - 74, toolbarY, 68, toolbarHeight);
                    place (record, w - 110, toolbarY, 32, toolbarHeight);
                    place (showPads, w - 146, toolbarY, 32, toolbarHeight);
                    place (generate, showPads.getX() - 78, toolbarY, 74, toolbarHeight);
                }
            }
            if (panel == Panel::generate)
            {
                place (aboutViewport, 12, headerBottom + 8, w - 24, getHeight() - headerBottom - 16);
                randomPanel.setSize (aboutViewport.getWidth(), aboutViewport.getHeight());
                finish();
                return;
            }
            if (showingCompactPadPreview())
            {
                const auto bounds = compactPadPreviewBounds();
                place (recordingStrip, 0, bounds.getBottom(), w, getHeight() - bounds.getBottom());
                layoutRecordingPads();
                repaint (bounds);
                finish();
                return;
            }
            if (showingRecordingPads())
            {
                place (recordingStrip, 0, juce::roundToInt (layout.previewY()), w, Layout::compactInspectorHeight);
                layoutRecordingPads();
            }
            else
            {
                place (preview, 0, juce::roundToInt (layout.previewY()), w, Layout::compactInspectorHeight);
                preview.setInspectorBounds (preview.getLocalBounds());
            }
            const bool scrolling = layout.phoneRowControls && layout.compactVisibleRows < layout.channels;
            place (grid, 0, juce::roundToInt (layout.gridY()), w - (scrolling ? 24 : 0), juce::roundToInt (layout.gridHeight()));
            if (scrolling)
                place (rowScroll, w - 24, juce::roundToInt (layout.gridY() + layout.cellsY()), 24, juce::roundToInt (layout.gridHeight() - layout.cellsY()));
            finish();
            return;
        }
        if (menuOpen || patternOpen)
        {
            layoutPhoneMenu (visible);
            finish();
            return;
        }
        else
        {
            place (undoButton, w - 300, toolbarY, 34, toolbarHeight);
            place (redoButton, w - 264, toolbarY, 34, toolbarHeight);
            if (layout.standalone)
            {
                placePlay (w - 220, 36);
                place (tempoButton, w - 180, toolbarY, 86, toolbarHeight);
            }
            place (closePanel, w - 84, toolbarY, 78, toolbarHeight);
        }
        if (panel != Panel::channel)
        {
            const int tabCount = static_cast<int> (panelTabs.size());
            const int tabWidth = (w - 24) / tabCount;
            for (int i = 0; i < tabCount; ++i)
            {
                auto& tab = panelTabs[static_cast<size_t> (i)];
                place (tab, 12 + i * tabWidth, 46, tabWidth - 6, 40);
                tab.setToggleState (panel == static_cast<Panel> (i + static_cast<int> (Panel::project)), juce::dontSendNotification);
            }
        }
        switch (panel)
        {
            case Panel::project:
                place (projectHeader, 24, 108, w - 48, 44);
                for (int i = 0; i < 3; ++i)
                    place (projectButtons[static_cast<size_t> (i)], 24 + i * 134, 180, 124, 48);
                place (projectButtons[5], 426, 180, 124, 48);
                place (projectButtons[3], 24, 256, 172, 44);
                place (projectButtons[4], 206, 256, 172, 44);
                break;
            case Panel::pattern:
                place (steps, 28, 126, 120, 66);
                place (rate, 164, 146, 160, 46);
                place (record, 372, 146, 64, 46);
                place (recordQuantize, 448, 146, 48, 46);
                place (recordOptions, 512, 146, w - 540, 46);
                place (left, 28, 248, 60, 44);
                place (right, 94, 248, 60, 44);
                place (compactClear, w - 224, 248, 196, 44);
                break;
            case Panel::feel:
                place (showSwing, 300, 104, 44, 44);
                place (swing, 22, 128, 152, 76);
                place (subdivision, 190, 128, 100, 76);
                place (velocityHumanize, 366, 128, 176, 76);
                place (timingHumanize, 562, 128, 176, 76);
                break;
            case Panel::view:
                place (dc, 28, 130, 168, 48);
                place (cellDisplay, 212, 130, 168, 48);
                place (rowOrder, 396, 130, 168, 48);
                place (compactAccent, 580, 130, 206, 48);
                place (modifierToggle, 28, 224, 208, 44);
                if (layout.phoneRowControls)
                    place (visibleRows, 260, 224, 180, 44);
                if (phone && layout.standalone)
                    place (metronomeOptions, 464, 224, 220, 44);
                if (layout.standalone && controller.processor.showStandaloneLinkSettings)
                    place (linkSettings, w / 2 + 8, 292, w / 2 - 36, 44);
                if (layout.standalone && controller.processor.showStandaloneMidiOutputMenu)
                    place (midiOutput, 28, 292, controller.processor.showStandaloneLinkSettings ? w / 2 - 36 : w - 56, 44);
                break;
            case Panel::channel:
                place (channelNote, 28, 106, 152, 68);
                place (loopStart, 28, 206, 156, 68);
                place (loopLength, 200, 206, 156, 68);
                if (phone)
                {
                    place (channelReverse, 372, 226, 118, 48);
                    place (channelPing, 502, 226, 118, 48);
                    place (channelSolo, 632, 226, 118, 48);
                }
                break;
            case Panel::help:
                place (phoneAboutWordmark, w - 264, 120, 244, 128);
                phoneAboutWordmark.setArtworkBounds ({});
                break;
            case Panel::generate:
            case Panel::none: break;
        }
        finish();
    }

    void NativeEditor::layoutPhoneMenu (std::vector<juce::Component*>& visible)
    {
        const PhoneMenuLayout menu (getLocalBounds(), phonePatternPanel());
        auto place = [&visible] (juce::Component& component, juce::Rectangle<int> bounds)
        {
            component.setBounds (bounds);
            visible.push_back (&component);
        };
        auto row = [&place] (juce::Rectangle<int> bounds, std::initializer_list<juce::Component*> components)
        {
            const int count = static_cast<int> (components.size());
            const int available = bounds.getWidth() - 8 * (count - 1);
            int index = 0;
            for (auto* component : components)
            {
                const int start = index * available / count;
                const int end = (index + 1) * available / count;
                place (*component, bounds.withX (bounds.getX() + start + index * 8).withWidth (end - start));
                ++index;
            }
        };
        if (!phonePatternPanel())
        {
            if (layout.standalone && controller.processor.showStandaloneMidiOutputMenu)
                row (menu.tabs, { &panelTabs[0], &panelTabs[2], &midiOutput, &panelTabs[3], &panelTabs[4] });
            else
                row (menu.tabs, { &panelTabs[0], &panelTabs[2], &panelTabs[3], &panelTabs[4] });
        }
        for (size_t i = 0; i < panelTabs.size(); ++i)
            panelTabs[i].setToggleState (panel == static_cast<Panel> (static_cast<int> (Panel::project) + static_cast<int> (i)), juce::dontSendNotification);

        auto leftControls = PhoneMenuLayout::controls (menu.left);
        auto rightControls = PhoneMenuLayout::controls (menu.right);
        constexpr int buttonHeight = 48;
        constexpr int rowStride = 58;
        switch (panel)
        {
            case Panel::project:
                place (projectHeader, leftControls.withHeight (68));
                row (leftControls.withTrimmedTop (82).withHeight (buttonHeight),
                     { &projectButtons[3], &projectButtons[4] });
                row (rightControls.withHeight (buttonHeight), { &projectButtons[0], &projectButtons[1] });
                row (rightControls.withTrimmedTop (rowStride).withHeight (buttonHeight), { &projectButtons[2], &projectButtons[5] });
                break;
            case Panel::pattern:
                place (steps, leftControls.withWidth (114).withHeight (68));
                place (rate, leftControls.withTrimmedLeft (122).withTrimmedTop (20).withHeight (48));
                place (recordQuantize, rightControls.withTrimmedTop (20).withWidth (64).withHeight (48));
                place (recordOptions, rightControls.withTrimmedLeft (80).withTrimmedTop (20).withHeight (48));
                // The edit row lives in a separate card below the two playback groups.
                place (left, menu.body.withPosition (menu.body.getX() + 14, menu.body.getY() + 194).withSize (52, 48));
                place (right, left.getBounds().translated (60, 0));
                place (compactClear, { menu.body.getRight() - 204, left.getY(), 190, 48 });
                break;
            case Panel::feel:
                row (leftControls.withHeight (80), { &swing, &subdivision });
                row (rightControls.withHeight (80), { &velocityHumanize, &timingHumanize });
                place (showSwing, leftControls.withTrimmedTop (100).withHeight (buttonHeight));
                break;
            case Panel::view:
            {
                row (leftControls.withHeight (buttonHeight), { &dc, &cellDisplay });
                const auto rowSettings = leftControls.withTrimmedTop (rowStride).withHeight (buttonHeight);
                if (layout.compactOnPhone)
                    row (rowSettings, { &rowOrder, &visibleRows });
                else
                    place (rowOrder, rowSettings);
                place (modifierToggle, leftControls.withTrimmedTop (2 * rowStride).withHeight (buttonHeight));
                const auto settingsRow = [&] (juce::Component& component)
                {
                    place (component, rightControls.removeFromTop (rowStride).withHeight (buttonHeight));
                };
                if (layout.standalone)
                {
                    auto metronomeRow = rightControls.removeFromTop (rowStride).withHeight (buttonHeight);
                    place (metronome, metronomeRow.removeFromLeft (buttonHeight));
                    place (metronomeOptions, metronomeRow.withTrimmedLeft (8));
                    if (controller.processor.showStandaloneLinkSettings)
                        settingsRow (linkSettings);
                }
                break;
            }
            case Panel::help:
            {
                const auto brand = menu.aboutBrandColumn();
                place (phoneAboutWordmark, menu.body);
                phoneAboutWordmark.setArtworkBounds (brand.withSizeKeepingCentre (juce::jmin (314.0f, brand.getWidth() - 24), 134.0f)
                                                          .translated (static_cast<float> (-menu.body.getX()), static_cast<float> (-menu.body.getY() - 20)));
                break;
            }
            case Panel::generate:
            case Panel::none:
            case Panel::channel: break;
        }
    }

    void NativeEditor::paintPhoneMenu (juce::Graphics& g) const
    {
        const bool notePage = showingNoteChooserPage();
        const bool singleHeader = phonePatternPanel() || notePage;
        const PhoneMenuLayout menu (getLocalBounds(), singleHeader);
        const auto body = menu.body.toFloat();
        const auto leftCard = menu.left.toFloat();
        const auto rightCard = menu.right.toFloat();
        const auto labelAbove = [&g] (const juce::Component& component, const juce::String& label, juce::Justification align)
        {
            const auto bounds = component.getBounds().toFloat();
            text (g, label, bounds.withY (bounds.getY() - 20).withHeight (18), 12, Palette::muted, true, align);
        };

        const int navHeight = singleHeader ? 42 : 94;
        g.setColour (Palette::border.withAlpha (0.48f));
        g.drawHorizontalLine (navHeight, body.getX(), body.getRight());
        if (notePage)
        {
            const auto title = noteChooserBack.getBounds().withX (noteChooserBack.getRight() + 12).toFloat().withRight (body.getRight());
            text (g, "Track " + juce::String (noteChooserChannel + 1), title, 15,
                  Palette::channel (noteChooserChannel, controller.drumColors), true, juce::Justification::centredLeft);
            phoneMenu::card (g, body);
            return;
        }

        switch (panel)
        {
            case Panel::project:
                phoneMenu::card (g, leftCard.withHeight (194), "Current project");
                phoneMenu::card (g, rightCard.withHeight (194), "Project files");
                break;
            case Panel::pattern:
            {
                const auto patternName = controller.source() < 0 ? juce::String ("Muted pattern") : "Pattern " + juce::String (controller.source() + 1);
                phoneMenu::card (g, leftCard.withHeight (140), patternName + " grid");
                phoneMenu::card (g, rightCard.withHeight (140), "Recording");
                phoneMenu::card (g, body.withTrimmedTop (152).withHeight (106), "Nudge pattern");
                labelAbove (rate, "Grid division", juce::Justification::centredLeft);
                labelAbove (recordQuantize, "Quantize", juce::Justification::centred);
                break;
            }
            case Panel::feel:
                phoneMenu::card (g, leftCard.withHeight (206), "Swing");
                phoneMenu::card (g, rightCard.withHeight (206), "Humanize");
                text (g, "Drag to adjust. Double-tap to enter a value.", body.withTrimmedLeft (2).withTrimmedTop (216).withHeight (30),
                      12, Palette::muted, false, juce::Justification::centredLeft);
                break;
            case Panel::view:
                phoneMenu::card (g, leftCard, "Interface");
                if (layout.standalone)
                    phoneMenu::card (g, rightCard, "Performance");
                break;
            case Panel::help:
            {
                const auto brand = menu.aboutBrandColumn();
                surface (g, body);
                text (g, "MIDI step sequencer  /  v" + juce::String (VERSION),
                      brand.withY (brand.getCentreY() + 52).withHeight (26), 14, Palette::muted);

                const auto motif = body.withTrimmedLeft (brand.getWidth()).reduced (28, 42);
                const float width = motif.getWidth() / 16.0f;
                const float height = motif.getHeight() / 8.0f;
                for (int row = 0; row < 8; ++row)
                    for (int step = 0; step < 16; ++step)
                    {
                        const bool hit = (step + row * 3) % (row % 3 + 4) == 0;
                        g.setColour (hit ? Palette::channel (row).withAlpha (0.72f) : Palette::app.withAlpha (0.58f));
                        g.fillRoundedRectangle (motif.getX() + step * width + 2, motif.getY() + row * height + 2, width - 4, height - 4, 2);
                    }
                break;
            }
            case Panel::generate:
            case Panel::none:
            case Panel::channel: break;
        }
    }
}
