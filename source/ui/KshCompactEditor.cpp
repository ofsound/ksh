#include "KshNativeEditor.h"

namespace ksh::ui
{
    void NativeEditor::setupCompactControls()
    {
        for (auto* button : { &compactAccent, &compactClear, &recordOptions, &compactPattern, &controls, &closePanel, &deselect, &page, &duplicate,
                              &phoneTempo, &metronomeOptions, &channelReverse, &channelPing, &channelSolo })
            addChildComponent (*button);
        phoneTempo.setTitle ("Set tempo");
        phoneTempo.onClick = [this]
        {
            tempoReturnPanel = panel;
            showPanel (Panel::tempo);
        };
        addChildComponent (tempoFader);
        tempoFader.setTitle ("Tempo BPM fader");
        tempoFader.setRange (tempo.minimum, tempo.maximum, 1);
        tempoFader.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 112, 48);
        tempoFader.setTextValueSuffix (" BPM");
        tempoFader.setColour (juce::Slider::trackColourId, Palette::accent);
        tempoFader.setColour (juce::Slider::thumbColourId, Palette::text);
        tempoFader.onValueChange = [this] { tempo.onChange (juce::roundToInt (tempoFader.getValue())); };
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
       #if JUCE_IOS
        preferredPhoneRows = juce::jlimit (1, 8, uiPreferences->getIntValue ("iphoneVisibleRows", 8));
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
        controls.onClick = [this] { showPanel (lastSettingsPanel); };
        closePanel.onClick = [this] { showPanel (panel == Panel::tempo ? tempoReturnPanel : Panel::none); };
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
            layout.firstStep = layout.firstStep == 0 ? 16 : 0;
            sync();
        };
        const std::array<juce::String, 6> names { "Project", "Pattern", "Feel", "Generate", "View", "Help" };
        for (int i = 0; i < 6; ++i)
        {
            auto& tab = panelTabs[static_cast<size_t> (i)];
            addChildComponent (tab);
            tab.setButtonText (names[static_cast<size_t> (i)]);
            tab.onClick = [this, i] { showPanel (static_cast<Panel> (i + static_cast<int> (Panel::project))); };
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
        channelNote.format = [] (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, 4) + " / " + juce::String (note); };
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
        if (layout.compact == compact && layout.phoneRowControls == (compact && phone) && (!compact || (juce::roundToInt (layout.compactWidth) == width && juce::roundToInt (layout.compactHeight) == height)))
            return;
        cancelCompactGestures();
        if (patternPicker.isVisible())
            patternPicker.dismiss();
        layout.compact = compact;
        layout.phoneRowControls = compact && phone;
        const bool isPhone = layout.phoneRowControls;
        controls.setButtonText (isPhone ? "Menu" : "Settings");
        controls.setTitle (isPhone ? "Open menu" : "Open controls");
        panelTabs[4].setButtonText (isPhone ? "Settings" : "View");
        panelTabs[5].setButtonText (isPhone ? "About" : "Help");
        if (isPhone)
            compactPattern.getProperties().set ("dropdown", true);
        else
            compactPattern.getProperties().remove ("dropdown");
        metronome.openSettingsOnHold = !isPhone;
        metronome.showSettings = isPhone ? std::function<void()>() : [this] { metronomeMenu (metronome); };
        metronome.setTooltip (isPhone ? "Toggle metronome" : "Right-click or hold for metronome settings");
        recordQuantize.showSettings = isPhone ? std::function<void()>() : [this] { recordingMenu(); };
        chanceLayer.setTooltip (isPhone ? "Hold for temporary Chance" : "Hold here or hold Option for temporary Chance");
        for (auto* button : { &undoButton, &redoButton })
            if (isPhone)
                button->getProperties().remove ("historySegment");
            else
                button->getProperties().set ("historySegment", button == &undoButton ? 0 : 1);
        layout.compactVisibleRows = layout.phoneRowControls ? preferredPhoneRows : 8;
        layout.firstRow = 0;
        layout.compactWidth = static_cast<float> (width);
        layout.compactHeight = static_cast<float> (height);
        layout.firstStep = 0;
        panel = Panel::none;
        preview.setCompact (compact);
        randomPanel.setCompact (compact);
        tempo.suffix = compact ? "" : " BPM";
        velocityLayer.setButtonText (compact ? "Vel" : "Velocity");
        dc.setButtonText (compact ? "Channel colors" : "DC");
        dc.getProperties().set ("icon", compact ? "" : "palette");
        cellDisplay.setButtonText (compact ? "Graphical values" : "");
        rowOrder.setButtonText (compact ? "Reverse rows" : "");
        cellDisplay.getProperties().set ("icon", compact ? "" : "cellDisplay");
        rowOrder.getProperties().set ("icon", compact ? "" : "rowOrder");
        modifierToggle.setButtonText (compact ? "Keyboard shortcuts" : "Keys");
        modifierToggle.getProperties().set ("icon", compact ? "" : "modifiers");
        const std::array<juce::String, 6> compactLabels { "New", "Open", "Save", "Previous project", "Next project", "Save as" };
        const std::array<juce::String, 6> desktopLabels { "NEW", "LOAD", "SAVE", juce::String::fromUTF8 ("◀"), juce::String::fromUTF8 ("▶"), "SAVE AS" };
        for (size_t i = 0; i < compactLabels.size(); ++i)
            projectButtons[i].setButtonText (compact ? compactLabels[i] : desktopLabels[i]);
        projectButtons[3].getProperties().set ("icon", compact ? "" : "projectPrevious");
        projectButtons[4].getProperties().set ("icon", compact ? "" : "projectNext");
        if (!compact)
        {
            const bool detailsVisible = projectDetails.isVisible();
            for (auto* child : getChildren())
                child->setVisible (true);
            projectDetails.setVisible (detailsVisible);
            patternPicker.setVisible (false);
            visibleRows.setVisible (false);
            rowScroll.setVisible (false);
            for (auto* button : { &compactAccent, &compactClear, &recordOptions, &compactPattern, &controls, &closePanel, &deselect, &page, &duplicate })
                button->setVisible (false);
            for (auto& tab : panelTabs)
                tab.setVisible (false);
            for (auto* component : std::array<juce::Component*, 10> { &channelNote, &loopStart, &loopLength, &aboutViewport,
                                                                    &phoneTempo, &tempoFader, &metronomeOptions, &channelReverse, &channelPing, &channelSolo })
                component->setVisible (false);
            layout.randomVisible = false;
        }
        sync();
    }

    void NativeEditor::setCompactVisibleRows (int count)
    {
        if (!layout.compact || !layout.phoneRowControls)
            return;
        cancelCompactGestures();
        noteChooser.dismiss();
        preferredPhoneRows = juce::jlimit (1, 8, count);
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
        for (int count = 1; count <= 8; ++count)
            menu.addItem (count, juce::String (count) + (count == 1 ? " row" : " rows"), true, layout.compactVisibleRows == count);
        juce::Component::SafePointer<NativeEditor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&visibleRows), [safe] (int count)
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
        preview.cancelGesture();
        randomPanel.cancelGesture();
        for (auto* value : { &swing, &subdivision, &velocityHumanize, &timingHumanize, &steps, &tempo, &loopStart, &loopLength })
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
        panel = next;
        if (next >= Panel::project && next <= Panel::help)
            lastSettingsPanel = next;
        if (isShowing())
            grabKeyboardFocus();
        sync();
        repaint();
    }

    void NativeEditor::syncChannel()
    {
        selectedChannel = juce::jlimit (0, layout.channels - 1, selectedChannel);
        const auto& channel = controller.state.playback.channels[static_cast<size_t> (selectedChannel)];
        channelNote.setValue (channel.note);
        const auto loop = controller.loop (selectedChannel);
        loopStart.maximum = layout.steps;
        loopStart.setValue (loop.loopStart + 1);
        loopLength.maximum = layout.steps - loop.loopStart;
        loopLength.setValue (loop.loopLength);
        loopStart.setEnabled (controller.source() >= 0);
        loopLength.setEnabled (controller.source() >= 0);
        channelReverse.setToggleState (channel.playbackMode == PlaybackMode::reverse || channel.playbackMode == PlaybackMode::reverse_ping_pong, juce::dontSendNotification);
        channelPing.setToggleState (channel.playbackMode == PlaybackMode::ping_pong || channel.playbackMode == PlaybackMode::reverse_ping_pong, juce::dontSendNotification);
        const bool solo = controller.soloedChannel() == selectedChannel;
        channelSolo.setButtonText (solo ? "Exit solo" : "Solo");
        channelSolo.setToggleState (solo, juce::dontSendNotification);
        for (auto* button : { &channelReverse, &channelPing, &channelSolo })
            button->setEnabled (controller.source() >= 0);
    }

    bool NativeEditor::showingCompactPadPreview() const
    {
        return layout.phoneRowControls && panel == Panel::none && showPads.getToggleState() && !patternPicker.isVisible();
    }

    juce::Rectangle<int> NativeEditor::compactPadPreviewBounds() const
    {
        return { 0, Layout::compactHeaderHeight, getWidth(), 76 };
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
                const bool hit = active && controller.sourceFlashes[index][static_cast<size_t> (step)] > now;
                const float opacity = muted ? 0.28f : 0.8f;
                g.setColour (hit ? colour.brighter (0.6f) : active ? colour.withAlpha (opacity)
                                                                 : Palette::border.withAlpha (inLoop && step % 4 == 0 ? 0.75f : 0.32f));
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
            if (projectDetails.isVisible())
                visible.push_back (&projectDetails);
            if (noteChooser.isVisible())
                visible.push_back (&noteChooser);
            if (discardChanges.isVisible())
                visible.push_back (&discardChanges);
            if (patternPicker.isVisible())
                visible.push_back (&patternPicker);
            for (auto* child : getChildren())
                child->setVisible (std::find (visible.begin(), visible.end(), child) != visible.end());
            if (patternPicker.isVisible())
                patternPicker.toFront (false);
        };
        auto place = [&visible] (juce::Component& component, int x, int y, int w, int h)
        {
            component.setBounds (x, y, w, h);
            visible.push_back (&component);
        };
        const int w = getWidth();
        const bool phone = layout.phoneRowControls;
        constexpr int toolbarY = 2;
        constexpr int toolbarHeight = Layout::compactHeaderHeight - 4;
        patternPicker.setBounds (0, Layout::compactHeaderHeight, w, getHeight() - Layout::compactHeaderHeight);
        auto placePlay = [&] (int x, int width)
        {
            play.setBounds (x, toolbarY, width, toolbarHeight);
            stop.setBounds (play.getBounds());
            visible.push_back (controller.processor.isStandaloneTransportPlaying() ? &stop : &play);
        };
        if (panel == Panel::none)
        {
            const bool patternBankOpen = layout.phoneRowControls && patternPicker.isVisible();
            const bool padPreview = showingCompactPadPreview();
            place (compactPattern, 6, toolbarY, 108, toolbarHeight);
            if (!patternBankOpen && !padPreview)
            {
                place (velocityLayer, phone ? 118 : 130, toolbarY, phone ? 46 : 54, toolbarHeight);
                place (chanceLayer, phone ? 164 : 184, toolbarY, 54, toolbarHeight);
                place (selectMode, phone ? 218 : 244, toolbarY, 54, toolbarHeight);
                if (controller.selected.any() || controller.shiftDown ({}))
                {
                    place (deselect, phone ? 276 : 304, toolbarY, phone ? 40 : 50, toolbarHeight);
                    if (controller.selected.any())
                        place (duplicate, phone ? 320 : 358, toolbarY, phone ? 40 : 50, toolbarHeight);
                }
                if (layout.steps > 16)
                    place (page, phone ? 364 : 480, toolbarY, phone ? 54 : 60, toolbarHeight);
            }
            if (phone)
            {
                juce::Rectangle<int> header (0, toolbarY, w - 6, toolbarHeight);
                auto buttonFromRight = [&] (juce::Component& button, int width)
                {
                    const auto bounds = header.removeFromRight (width);
                    place (button, bounds.getX(), toolbarY, width, toolbarHeight);
                };
                buttonFromRight (controls, 60);
                header.removeFromRight (8);
                if (layout.standalone)
                {
                    placePlay (header.removeFromRight (42).getX(), 42);
                    buttonFromRight (metronome, 42);
                    buttonFromRight (phoneTempo, 64);
                    header.removeFromRight (8);
                }
                buttonFromRight (redoButton, 38);
                buttonFromRight (record, 42);
                buttonFromRight (showPads, 42);
                buttonFromRight (undoButton, 38);
            }
            else
            {
                place (undoButton, 412, toolbarY, 32, toolbarHeight);
                place (redoButton, 444, toolbarY, 32, toolbarHeight);
                place (controls, w - 74, toolbarY, 68, toolbarHeight);
                place (record, w - 110, toolbarY, 32, toolbarHeight);
                place (showPads, w - 146, toolbarY, 32, toolbarHeight);
                if (layout.standalone)
                {
                    placePlay (w - 270, 32);
                    place (tempo, w - 234, toolbarY, 48, toolbarHeight);
                    place (metronome, w - 182, toolbarY, 32, toolbarHeight);
                }
            }
            if (patternBankOpen)
            {
                finish();
                return;
            }
            if (padPreview)
            {
                const auto bounds = compactPadPreviewBounds();
                place (recordingStrip, 0, bounds.getBottom(), w, getHeight() - bounds.getBottom());
                layoutRecordingPads();
                repaint (bounds);
                finish();
                return;
            }
            if (showPads.getToggleState())
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
        place (undoButton, w - 300, toolbarY, 34, toolbarHeight);
        place (redoButton, w - 264, toolbarY, 34, toolbarHeight);
        if (layout.standalone && panel != Panel::tempo)
        {
            placePlay (w - 220, 36);
            place (phone ? static_cast<juce::Component&> (phoneTempo) : static_cast<juce::Component&> (tempo), w - 180, toolbarY, 86, toolbarHeight);
        }
        place (closePanel, w - 84, toolbarY, 78, toolbarHeight);
        if (panel != Panel::channel && panel != Panel::tempo)
        {
            const int tabWidth = (w - 24) / 6;
            for (int i = 0; i < 6; ++i)
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
            case Panel::generate:
                place (aboutViewport, 12, 100, w - 24, getHeight() - 112);
                randomPanel.setSize (w - 36, 440);
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
                if (layout.standalone && controller.processor.showStandaloneMidiOutputMenu)
                    place (midiOutput, 28, 292, w - 56, 44);
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
            case Panel::tempo:
                place (tempoFader, 48, 118, w - 96, 124);
                break;
            case Panel::none:
            case Panel::help: break;
        }
        finish();
    }
}
