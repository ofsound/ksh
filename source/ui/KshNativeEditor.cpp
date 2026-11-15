#include "KshNativeEditor.h"
#include "KshUiScale.h"
#include "../engine/KshMidiExport.h"
namespace ksh::ui
{
    namespace
    {
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
                widest = juce::jmax (widest, dropdownWidth (name));
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

    NativeEditor::NativeEditor (PluginProcessor& processor) : controller (processor), grid (controller), preview (controller), randomPanel (controller, true), projectDetails (processor)
    {
       #if JUCE_IOS
        layout.touchRows = true;
       #endif
        juce::PropertiesFile::Options options;
        options.applicationName = "KSH";
        options.filenameSuffix = ".ui.settings";
        options.folderName = "ofsound";
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = -1;
        uiPreferences = std::make_unique<juce::PropertiesFile> (options);
        Palette::setBlueAccent (uiPreferences->getBoolValue ("blueAccent", Palette::blueAccentSelected()));
        footerHelp.setHelpEnabled (uiPreferences->getBoolValue ("footerTooltips", true));
        layout.reverseRows = uiPreferences->getBoolValue ("reverseRows", false);
        layout.showSwing = uiPreferences->getBoolValue ("showSwing", true);
        controller.keyboardModifiersEnabled = uiPreferences->getBoolValue ("keyboardModifiersEnabled", true);
        addAndMakeVisible (modifierToggle);
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
        modifierToggle.onClick = [this]
        {
            cancelCompactGestures();
            controller.keyboardModifiersEnabled = !controller.keyboardModifiersEnabled;
            uiPreferences->setValue ("keyboardModifiersEnabled", controller.keyboardModifiersEnabled);
            uiPreferences->saveIfNeeded();
            modifierKeysChanged (juce::ModifierKeys::getCurrentModifiersRealtime());
            sync();
            grid.repaint();
        };
        controller.graphicalCellValues = uiPreferences->getBoolValue ("graphicalCellValues", true);
        addAndMakeVisible (cellDisplay);
        cellDisplay.getProperties().set ("icon", "cellDisplay");
        cellDisplay.setTitle ("Show cell numbers");
        cellDisplay.setTooltip ("Switch between numbers and graphs");
        cellDisplay.onClick = [this]
        {
            controller.graphicalCellValues = !controller.graphicalCellValues;
            uiPreferences->setValue ("graphicalCellValues", controller.graphicalCellValues);
            uiPreferences->saveIfNeeded();
            sync();
        };
        addAndMakeVisible (rowOrder);
        rowOrder.setTitle ("Reverse row order");
        rowOrder.getProperties().set ("icon", "rowOrder");
        rowOrder.onClick = [this]
        {
            cancelCompactGestures();
            layout.reverseRows = !layout.reverseRows;
            uiPreferences->setValue ("reverseRows", layout.reverseRows);
            uiPreferences->saveIfNeeded();
            sync();
            repaint();
        };
        addAndMakeVisible (footerHelp);
        addAndMakeVisible (tooltipToggle);
        tooltipToggle.setTitle ("Footer tooltips");
        tooltipToggle.onClick = [this]
        {
            footerHelp.setHelpEnabled (!footerHelp.isHelpEnabled());
            footerHelp.refresh();
            uiPreferences->setValue ("footerTooltips", footerHelp.isHelpEnabled());
            uiPreferences->saveIfNeeded();
            sync();
        };
        setLookAndFeel (&look);
        setOpaque (true);
        setWantsKeyboardFocus (true);
        addMouseListener (this, true);
        addAndMakeVisible (grid);
        grid.rowTriggered = [this] (int channel) { triggerPad (channel); };
        addChildComponent (noteChooser);
        for (int row = 0; row < Constants::maxChannels; ++row)
            noteChooser.setRowNoteInput (row, grid.rowNoteInput (row));
        grid.noteChooserRequested = [this] (int ch, juce::Component& input) { showNoteChooser (ch, input); };
        noteChooser.onChoose = [this] (int note)
        {
            controller.refresh();
            if (noteChooserEpoch == controller.processor.getUiBridge().getDocumentEpoch()
                && juce::isPositiveAndBelow (noteChooserChannel, controller.state.playback.channelCount)
                && controller.state.playback.channels[static_cast<size_t> (noteChooserChannel)].note == noteChooserOriginal)
            {
                // Accept subsequent scrub notes, while still rejecting external edits.
                const juce::ScopedValueSetter<bool> editing (noteChooserEditing, true);
                controller.command ("channel_note", { noteChooserChannel + 1, note }, "Change channel note");
                noteChooserOriginal = note;
                (void) controller.processor.auditionRowFromUi (noteChooserChannel);
            }
        };
        noteChooser.onNavigate = [this] (int delta)
        {
            const int row = layout.displayRow (noteChooserChannel) + delta;
            if (!juce::isPositiveAndBelow (row, layout.channels))
                return;
            const int channel = layout.channelForRow (row);
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
        noteChooser.onPresetAll = [this] (const NoteChooser::RowNotes& notes)
        {
            controller.refresh();
            applyNoteChooserPreset (notes, "Apply scale preset");
        };
        addAndMakeVisible (preview);
        addChildComponent (aboutViewport);
        aboutViewport.setViewedComponent (&randomPanel, false);
        aboutViewport.setScrollBarsShown (true, false);
        preview.onLogoDoubleClick = [this] { toggleAboutPanel(); };
        preview.onAccentSelected = [this] (bool blue)
        {
            uiPreferences->setValue ("blueAccent", blue);
            uiPreferences->saveIfNeeded();
            Palette::setBlueAccent (blue);
        };
        const std::array<juce::String, 6> projectLabels { "NEW", "LOAD", "SAVE", juce::String::fromUTF8 ("◀"), juce::String::fromUTF8 ("▶"), "SAVE AS" };
        const std::array<juce::String, 6> projectTips { "", "", "", "Previous project in this folder", "Next project in this folder", "" };
        for (int i = 0; i < static_cast<int> (projectButtons.size()); ++i)
        {
            auto& button = projectButtons[static_cast<size_t> (i)];
            addAndMakeVisible (button);
            button.setButtonText (projectLabels[static_cast<size_t> (i)]);
            button.setTooltip (projectTips[static_cast<size_t> (i)]);
            button.onClick = [this, i] { controller.commit(); if (projectAction) projectAction (i); };
        }
        addAndMakeVisible (projectHeader);
        addChildComponent (projectDetails);
        addChildComponent (discardChanges);
        projectHeader.onDetails = [this]
        {
            cancelCompactGestures();
            projectDetails.open();
        };
       #if JUCE_IOS
        projectDetails.onShare = [this] { if (projectAction) projectAction (6); };
       #endif
        addChildComponent (recordingStrip);
        recordingStrip.setTitle ("Performance pads");
        for (int channel = 0; channel < static_cast<int> (recordingPads.size()); ++channel)
        {
            auto& pad = recordingPads[static_cast<size_t> (channel)];
            recordingStrip.addAndMakeVisible (pad);
            pad.onClick = [this, channel] { triggerPad (channel, recordingPads[static_cast<size_t> (channel)].pressedVelocity()); };
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
        projectDetails.onClose = [this]
        {
            sync();
            if (isShowing())
                grabKeyboardFocus();
        };
        for (auto* b : { &mutePattern, &velocityLayer, &clear, &record, &left, &right, &undoButton, &redoButton, &rate, &play, &dc })
            addAndMakeVisible (*b);
        addAndMakeVisible (chanceLayer);
        for (juce::TextButton* button : std::array<juce::TextButton*, 2> { &showPads, &metronome })
        {
            addAndMakeVisible (*button);
            button->setClickingTogglesState (true);
            button->getProperties().set ("outlinedIcon", true);
        }
        showPads.getProperties().set ("icon", "pads");
        showPads.setTitle ("Show pads");
        showPads.onClick = [this]
        {
            preview.cancelGesture();
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
        selectMode.onClick = [this] { controller.selectLatched = !controller.selectLatched; controller.notify(); };
        for (int i = 0; i < 16; ++i)
        {
            auto& b = patterns[static_cast<size_t> (i)];
            addAndMakeVisible (b);
            b.setButtonText (juce::String (i + 1));
            b.getProperties().set ("patternNumber", true);
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
        record.setTooltip ("Record MIDI or ASDFGHJK");
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
        rate.setTitle ("Pattern rate");
        rate.onClick = [this] { rateMenu(); };
        play.onClick = [this] { controller.command ("standalone_transport_playing", { true }); };
        stop.onClick = [this] { controller.command ("standalone_transport_playing", { false }); };
        dc.onClick = [this] { controller.drumColors = ! controller.drumColors; sync(); };
        configureValue (swing, "Amount", 0, 100, "swing");
        configureValue (subdivision, "Target", 0, 2, "swing_subdivision");
        swing.setTitle ("Swing amount");
        swing.suffix = "%";
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
        subdivision.setTooltip ("Swing groups of 1, 2, or 4 steps");
        subdivision.format = [] (int v) { return juce::String (1 << v) + "x"; };
        subdivision.resetValue = Constants::defaultSwingSubdivisionIndex;
        configureValue (velocityHumanize, "Velocity", 0, 100, "velocity_humanize");
        configureValue (timingHumanize, "Timing", 0, 100, "timing_humanize");
        velocityHumanize.setTitle ("Velocity humanization");
        timingHumanize.setTitle ("Timing humanization");
        velocityHumanize.setTooltip ("Random velocity variation per hit");
        timingHumanize.setTooltip ("At 100%, hits vary by up to 1/5 step");
        velocityHumanize.suffix = timingHumanize.suffix = "%";
        configureValue (steps, "Grid Length", 1, 32, "source_steps");
        steps.captionFontSize = 12;
        steps.captionColour = Palette::muted;
        steps.captionGap = 5;
        steps.resetValue = 16;
        configureValue (tempo, "", 20, 300, "standalone_tempo");
        tempo.suffix = " BPM";
        tempo.suffixFontScale = 0.75f;
        tempo.setTitle ("Tempo BPM");
        tempo.resetValue = 120;
        addAndMakeVisible (zen);
        zen.setTitle ("Zen mode");
        zen.setTooltip ("Esc to exit Zen");
        zen.onClick = [this] { if (zenModeChanged) zenModeChanged (!zen.getToggleState()); };
        addChildComponent (scale);
        scale.setTitle ("UI scale percent");
        scale.setTooltip ("Double-click to enter a scale");
        scale.minimum = UiScale::minimumPercent;
        scale.maximum = UiScale::maximumPercent;
        scale.resetValue = 100;
        scale.suffix = "%";
        scale.valueFontSize = 18;
        zen.getProperties().set ("icon", "zen");
        modifierToggle.getProperties().set ("icon", "modifiers");
        tooltipToggle.getProperties().set ("icon", "tips");
        addChildComponent (audioSettings);
        audioSettings.setTitle ("Audio/MIDI Settings");
        audioSettings.getProperties().set ("icon", "gear");
        audioSettings.onClick = [this]
        {
            if (controller.processor.showStandaloneAudioSettings)
                controller.processor.showStandaloneAudioSettings();
        };
        dc.getProperties().set ("icon", "palette");
        dc.setTitle ("Channel colors");
        for (auto* button : { &cellDisplay, &zen, &tooltipToggle, &rowOrder, &modifierToggle, &dc, &audioSettings })
            button->getProperties().set ("footerControl", true);
        scale.getProperties().set ("footerControl", true);
        scale.onChange = [this] (int v) { controller.processor.setProjectUiScalePercent (v); if (preferredSizeChanged) preferredSizeChanged(); };
        controller.changed = [this] { sync(); };
        setupCompactControls();
        Palette::accentChanges().addChangeListener (this);
        changeListenerCallback (&Palette::accentChanges());
        sync();
        startTimerHz (60);
    }
    NativeEditor::~NativeEditor()
    {
        stopTimer();
        Palette::accentChanges().removeChangeListener (this);
        removeMouseListener (this);
        juce::PopupMenu::dismissAllActiveMenus();
        controller.changed = {};
        setLookAndFeel (nullptr);
    }
    void NativeEditor::setZenMode (bool enabled)
    {
        zen.setToggleState (enabled, juce::dontSendNotification);
        if (!enabled)
            scale.cancelGesture();
        scale.minimum = UiScale::minimumPercent;
        scale.maximum = UiScale::maximumPercent;
        scale.setValue (controller.processor.getProjectUiScalePercent());
        resized();
        repaint (0, getHeight() - Layout::footerHeight, getWidth(), Layout::footerHeight);
    }
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
        noteChooserChannel = ch;
        noteChooserOriginal = channel.note;
        noteChooserEpoch = controller.processor.getUiBridge().getDocumentEpoch();
        noteChooser.open (getLocalArea (&input, input.getLocalBounds()), juce::String (ch + 1), channel.note, &input, grid.rowTitle (ch));
        updateNoteChooserTranspose();
    }
    void NativeEditor::updateNoteChooserTranspose()
    {
        const auto& channels = controller.state.playback.channels;
        const auto [lowest, highest] = std::minmax_element (channels.begin(), channels.end(),
            [] (const auto& a, const auto& b) { return a.note < b.note; });
        noteChooser.setTransposeRange (lowest->note, highest->note);
    }
    void NativeEditor::applyNoteChooserPreset (const NoteChooser::RowNotes& notes, const juce::String& undoLabel)
    {
        if (!noteChooser.isVisible() || noteChooserEpoch != controller.processor.getUiBridge().getDocumentEpoch()
            || !juce::isPositiveAndBelow (noteChooserChannel, controller.state.playback.channelCount)
            || controller.state.playback.channels[static_cast<size_t> (noteChooserChannel)].note != noteChooserOriginal
            || std::any_of (notes.begin(), notes.end(), [] (int note) { return !juce::isPositiveAndBelow (note, 128); }))
            return;

        const juce::ScopedValueSetter<bool> editing (noteChooserEditing, true);
        controller.command ("channel_notes", notes, undoLabel);
        noteChooserOriginal = controller.state.playback.channels[static_cast<size_t> (noteChooserChannel)].note;
        noteChooser.setAssignedNote (noteChooserOriginal);
        updateNoteChooserTranspose();
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
        const bool scaleChanged = previousScale != controller.processor.getProjectUiScalePercent();
        previousScale = controller.processor.getProjectUiScalePercent();
        layout.channels = p.channelCount;
        layout.steps = p.stepCount;
        if (layout.firstStep >= layout.steps)
            layout.firstStep = 0;
        layout.standalone = controller.processor.hasStandaloneTransport();
        syncMidiOutput();
        midiOutput.setVisible (layout.standalone && controller.processor.showStandaloneMidiOutputMenu && !layout.compact);
        audioSettings.setVisible (layout.standalone && controller.processor.showStandaloneAudioSettings && !layout.compact);
        layout.firstRow = layout.compact ? juce::jlimit (0, juce::jmax (0, layout.channels - layout.compactVisibleRows), layout.firstRow) : 0;
        grid.layout = layout;
        if (layout.compact && layout.compactVisibleRows < layout.channels)
            grid.layout.compactWidth -= 24;
        visibleRows.setButtonText ("Rows: " + juce::String (layout.compactVisibleRows));
        rowScroll.setRangeLimits (0, layout.channels, juce::dontSendNotification);
        rowScroll.setCurrentRange (layout.firstRow, juce::jmin (layout.channels, layout.compactVisibleRows), juce::dontSendNotification);
        preview.reverseRows = randomPanel.reverseRows = layout.reverseRows;
        auto patternLabel = controller.source() < 0 ? juce::String ("Muted") : "Pattern " + juce::String (controller.source() + 1);
        if (!layout.phoneRowControls)
            patternLabel += "  v";
        compactPattern.setButtonText (patternLabel);
        compactPattern.setToggleState (patternPicker.isVisible(), juce::dontSendNotification);
        compactAccent.setButtonText (Palette::blueAccentSelected() ? "Accent: Blue" : "Accent: Orange");
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
        page.setButtonText (layout.firstStep == 0 ? "1-16  >" : "<  17-" + juce::String (layout.steps));
        duplicate.setToggleState (grid.isCopyMode(), juce::dontSendNotification);
        updatePatternButtons();
        updateLayerHeading();
        for (auto* b : { &clear, &record, &left, &right, &rate, static_cast<juce::TextButton*> (&recordQuantize) })
            b->setEnabled (controller.source() >= 0);
        steps.setEnabled (controller.source() >= 0);
        undoButton.setEnabled (controller.canUndo());
        redoButton.setEnabled (controller.canRedo());
        swing.setValue (p.swing);
        showSwing.setToggleState (layout.showSwing, juce::dontSendNotification);
        subdivision.setValue (p.swingSubdivisionIndex);
        velocityHumanize.setValue (p.velocityHumanize);
        timingHumanize.setValue (p.timingHumanize);
        steps.setValue (p.stepCount);
        const int bpm = juce::roundToInt (controller.processor.getStandaloneTempoBpm());
        tempo.setValue (bpm);
        if (layout.phoneRowControls)
        {
            phoneTempo.setButtonText (juce::String (bpm) + " BPM");
            tempoFader.setValue (bpm, juce::dontSendNotification);
        }
        scale.setValue (zen.getToggleState() ? juce::jmin (controller.processor.getProjectUiScalePercent(), scale.maximum) : controller.processor.getProjectUiScalePercent());
        static const std::array<std::string, 8> rates { "4n", "4nt", "8n", "8nt", "16n", "16nt", "32n", "32nt" };
        auto value = p.sourceSettings[static_cast<size_t> (juce::jmax (0, controller.source()))].rate;
        auto found = std::find (rates.begin(), rates.end(), value);
        rate.setButtonText (found == rates.end() ? "16th" : rateNames[static_cast<size_t> (found - rates.begin())]);
        const bool playing = controller.processor.isStandaloneTransportPlaying();
        play.setVisible (layout.standalone && !playing);
        stop.setVisible (layout.standalone && playing);
        tempo.setVisible (layout.standalone && !layout.phoneRowControls);
        metronome.setVisible (layout.standalone);
        metronome.setToggleState (controller.processor.isStandaloneMetronomeEnabled(), juce::dontSendNotification);
       #if JUCE_IOS
        zen.setVisible (false);
       #else
        zen.setVisible (layout.standalone && !layout.compact);
       #endif
        play.setToggleState (playing, juce::dontSendNotification);
        stop.setToggleState (!playing, juce::dontSendNotification);
        record.setToggleState (controller.processor.isPatternRecordingEnabled(), juce::dontSendNotification);
        if (!layout.phoneRowControls)
        {
            recordingStrip.setVisible (!layout.compact && showPads.getToggleState());
            preview.setVisible (layout.compact || !showPads.getToggleState());
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
        tooltipToggle.setToggleState (footerHelp.isHelpEnabled(), juce::dontSendNotification);
        projectHeader.setProjectName (controller.processor.getProjectName());
        if (projectDetails.isVisible())
            projectDetails.refresh();
        if (noteChooser.isVisible() && !noteChooserEditing)
        {
            if (noteChooserEpoch != controller.processor.getUiBridge().getDocumentEpoch()
                || !juce::isPositiveAndBelow (noteChooserChannel, p.channelCount)
                || p.channels[static_cast<size_t> (noteChooserChannel)].note != noteChooserOriginal)
                noteChooser.dismiss();
            else
            {
                updateNoteChooserTranspose();
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
        if (projectDetails.isVisible())
            projectDetails.toFront (false);
        if (discardChanges.isVisible())
        {
            if (discardEpoch != controller.processor.getUiBridge().getDocumentEpoch())
                discardChanges.dismiss();
            else
                discardChanges.toFront (false);
        }
        if (!layout.compact)
            repaint (getWidth() / 2, juce::roundToInt (layout.toolbarY()), getWidth() - getWidth() / 2, 68);
        repaint (0, getHeight() - Layout::footerHeight, getWidth(), Layout::footerHeight);
        syncing = false;
        if ((scaleChanged || std::abs (oldHeight - layout.height()) > 0.01f || std::abs (oldWidth - layout.width()) > 0.01f) && preferredSizeChanged)
            preferredSizeChanged();
    }
    void NativeEditor::resized()
    {
        laidOut = { layout, getLocalBounds() };
        noteChooser.setBounds (getLocalBounds());
        projectDetails.setBounds (getLocalBounds());
        discardChanges.setBounds (getLocalBounds());
        constexpr int target = Layout::touchTarget;
        steps.valueWidth = target;
        for (auto* value : { &swing, &subdivision, &velocityHumanize, &timingHumanize })
        {
            value->valueWidth = layout.compact ? 42 : target;
            value->valueFontSize = layout.compact ? 12.0f : 18.0f;
            value->captionInset = 3;
        }
        for (auto* button : { &cellDisplay, &zen, &tooltipToggle, &rowOrder, &modifierToggle, &dc, &midiOutput, &audioSettings })
            button->getProperties().set ("insetFooterArtwork", !layout.compact);
        scale.getProperties().set ("insetFooterArtwork", !layout.compact);
        scale.setVisible (layout.standalone && !layout.compact && zen.getToggleState());
        if (layout.compact)
        {
            resizedCompact();
            return;
        }
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
        constexpr int macroHeight = target + 20;
        constexpr int macroY = secondRowY + target - macroBottomPadding - macroHeight;
        swing.setBounds (366, macroY, 72, macroHeight);
        subdivision.setBounds (444, macroY, 72, macroHeight);
        showSwing.setBounds (subdivision.getRight() + 6 - target, 0, target, target);
        velocityHumanize.setBounds (538, macroY, 72, macroHeight);
        timingHumanize.setBounds (616, macroY, 72, macroHeight);
        for (auto* value : { &swing, &subdivision, &velocityHumanize, &timingHumanize })
        {
            value->captionFontSize = labelFontSize;
            value->captionGap = 2;
        }
        constexpr int patternGap = 4;
        constexpr int headerControlGap = 8;
        const int muteX = rightEdge - target - (layout.standalone ? target + headerControlGap : 0);
        const int patternRight = muteX - headerControlGap;
        const int patternLeft = patternRight - (8 * target + 7 * patternGap);
        for (int i = 0; i < 16; ++i)
            patterns[static_cast<size_t> (i)].setBounds (patternLeft + (i % 8) * (target + patternGap), headerTop + (i / 8) * (target + Layout::headerRowGap), target, target);
        mutePattern.setBounds (muteX, layout.standalone ? secondRowY : (headerTop + secondRowY) / 2, target, target);
        if (layout.standalone)
        {
            play.setBounds (rightEdge - target, headerTop, target, target);
            stop.setBounds (play.getBounds());
            tempo.setBounds (muteX, headerTop, target, target);
            tempo.valueFontSize = 13;
            metronome.setBounds (rightEdge - target, secondRowY, target, target);
        }
        const int inputY = juce::roundToInt (layout.toolbarY()) + Layout::toolbarPadding + Layout::toolbarCaptionSpace;
        velocityLayer.setBounds (12, inputY, 104, target);
        chanceLayer.setBounds (115, inputY, 104, target);
        selectMode.setBounds (218, inputY, 104, target);
        layerHint.setBounds (12, inputY - 22, 310, 20);
        undoButton.setBounds (336, inputY, target, target);
        redoButton.setBounds (399, inputY, target, target);
        const int divisionWidth = juce::jmax (144, rateControlWidth());
        rate.setBounds (rightEdge - divisionWidth, inputY, divisionWidth, target);
        steps.valueFontSize = 22;
        steps.captionFontSize = labelFontSize;
        steps.setBounds (rate.getX() - 100, inputY - 23, 90, target + 23);
        clear.setBounds (steps.getX() - 78, inputY, target, target);
        right.setBounds (clear.getX() - 74, inputY, target, target);
        left.setBounds (right.getX() - 66, inputY, target, target);
        recordQuantize.setBounds (left.getX() - 80, inputY, target, target);
        record.setBounds (recordQuantize.getX() - 66, inputY, target, target);
        showPads.setBounds (record.getX() - target - 20, inputY, target, target);
        const int randomY = juce::roundToInt (layout.gridY());
        const int aboutHeight = juce::roundToInt (layout.previewY()) - randomY;
        aboutViewport.setBounds (0, randomY, getWidth(), aboutHeight);
        const int scrollWidth = aboutHeight < 540 ? aboutViewport.getScrollBarThickness() : 0;
        randomPanel.setSize (getWidth() - scrollWidth, juce::jmax (540, aboutHeight));
        grid.setBounds (0, juce::roundToInt (layout.gridY()), getWidth(), juce::roundToInt (layout.gridHeight()));
        preview.setBounds (0, juce::roundToInt (layout.previewY()), getWidth(), Layout::previewHeight);
        const int inspectorLeft = juce::roundToInt (layout.gridLeft()) - Layout::rowEndHandleSpace;
        const int inspectorRight = juce::roundToInt (layout.gridLeft() + layout.gridWidth()) + Layout::rowEndHandleSpace;
        preview.setInspectorBounds ({ inspectorLeft, 8, inspectorRight - inspectorLeft, Layout::previewHeight - 16 });
        recordingStrip.setBounds (preview.getBounds());
        layoutRecordingPads();
        const int footerControlY = getHeight() - Layout::footerHeight + Layout::footerPadding;
        int footerRight = rightEdge;
        const auto placeFooter = [&] (juce::Component& control, int width = Layout::touchTarget)
        {
            control.setBounds (footerRight - width, footerControlY, width, target);
            footerRight -= width + Layout::footerGap;
        };
        if (scale.isVisible())
            placeFooter (scale, 88);
        if (layout.standalone && controller.processor.showStandaloneMidiOutputMenu)
            placeFooter (midiOutput, 104);
        if (audioSettings.isVisible())
            placeFooter (audioSettings);
        if (zen.isVisible())
            placeFooter (zen);
        placeFooter (dc);
        placeFooter (cellDisplay);
        placeFooter (rowOrder);
        placeFooter (modifierToggle);
        placeFooter (tooltipToggle);
        footerHelp.setBounds (Layout::outerGutter, footerControlY, tooltipToggle.getX() - 2 * Layout::outerGutter, target);
    }
    void NativeEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::app);
        if (layout.compact)
        {
            if (showingCompactPadPreview())
                paintCompactPadPreview (g);
            if (panel != Panel::none)
            {
                const juce::String menuTitle = layout.phoneRowControls ? "Menu" : "Settings";
                const auto heading = panel == Panel::channel ? "Track " + juce::String (selectedChannel + 1)
                                     : panel == Panel::tempo ? juce::String ("Tempo")
                                     : panel == Panel::pattern ? menuTitle + (controller.source() < 0 ? " / Muted Pattern" : " / Pattern " + juce::String (controller.source() + 1))
                                                               : menuTitle;
                text (g, heading, { 16, 2, static_cast<float> (getWidth() - 328), 30 }, 15, Palette::text, true, juce::Justification::centredLeft);
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
                    text (g, "MIDI note and loop", { 28, 64, 160, 24 }, 16, Palette::channel (selectedChannel, controller.drumColors), true, juce::Justification::centredLeft);
                }
                else if (panel == Panel::help && layout.phoneRowControls)
                {
                    PreviewPanel::paintBranding (g, getLocalBounds().toFloat().withTrimmedTop (96).withSizeKeepingCentre (270, 160));
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
                        text (g, lines[static_cast<size_t> (i)], { 20, 100.0f + static_cast<float> (i) * 38, static_cast<float> (getWidth() - 40), 34 }, labelFontSize, Palette::text, false, juce::Justification::centredLeft);
                }
            }
            return;
        }
        const auto headerBounds = getLocalBounds().withHeight (Layout::headerHeight).toFloat();
        const auto headerTop = Palette::raised.interpolatedWith (Palette::info, 0.14f).interpolatedWith (Palette::app, 0.66f);
        const auto headerBottom = Palette::shell.interpolatedWith (Palette::app, 0.66f);
        g.setGradientFill (juce::ColourGradient (headerTop, headerBounds.getTopLeft(), headerBottom, headerBounds.getBottomRight(), false));
        g.fillRect (headerBounds);
        text (g, "Grid Division", { static_cast<float> (rate.getX()), static_cast<float> (steps.getY()), static_cast<float> (rate.getWidth()), 18 }, steps.captionFontSize, steps.captionColour, true, juce::Justification::centredLeft);
        text (g, "Pads", showPads.getBounds().withRight (record.getX() - 4).withY (steps.getY()).withHeight (18).toFloat(), steps.captionFontSize, steps.captionColour, true, juce::Justification::centredLeft);
        text (g, "Arm Record", record.getBounds().getUnion (recordQuantize.getBounds()).withY (steps.getY()).withHeight (18).toFloat(), steps.captionFontSize, steps.captionColour, true, juce::Justification::centredLeft);
        text (g, "Nudge", left.getBounds().getUnion (right.getBounds()).withY (steps.getY()).withHeight (18).toFloat(), steps.captionFontSize, steps.captionColour, true, juce::Justification::centredLeft);
        text (g, "Clear", clear.getBounds().withY (steps.getY()).withHeight (18).toFloat(), steps.captionFontSize, steps.captionColour, true, juce::Justification::centredLeft);
        g.setColour (Palette::border.withAlpha (0.5f));
        g.drawHorizontalLine (juce::roundToInt (layout.toolbarY()), 0, static_cast<float> (getWidth()));
        for (auto y : { layout.gridY(), layout.height() - Layout::footerHeight })
            g.drawHorizontalLine (static_cast<int> (y), 0, static_cast<float> (getWidth()));
        const auto patternRows = patterns.front().getBounds().getUnion (patterns.back().getBounds());
        const auto groupY = static_cast<float> (patternRows.getY()) + 0.5f;
        const auto groupHeight = static_cast<float> (patternRows.getHeight()) - 1.0f;
        paintControlGroup (g, { static_cast<float> (swing.getX() - 8), groupY, static_cast<float> (subdivision.getRight() - swing.getX() + 16), groupHeight }, "Swing");
        paintControlGroup (g, { static_cast<float> (velocityHumanize.getX() - 8), groupY, static_cast<float> (timingHumanize.getRight() - velocityHumanize.getX() + 16), groupHeight }, "Humanize");
        // Paint in the parent so the halo can extend beyond the button's clip bounds.
        const auto paintPatternGlow = [this, &g] (const juce::TextButton& button)
        {
            if (!button.isVisible() || !button.getToggleState())
                return;
            const auto bounds = button.getBounds().toFloat().reduced (0.5f);
            if (!g.clipRegionIntersects (bounds.expanded (static_cast<float> (patternGlowExtent)).getSmallestIntegerContainer()))
                return;
            patternGlow.setColor (Palette::accent.withAlpha (0.8f * button.getAlpha()), 0);
            patternGlow.setColor (Palette::accent.withAlpha (0.55f * button.getAlpha()), 1);
            juce::Path silhouette;
            silhouette.addRoundedRectangle (bounds, 4);
            patternGlow.render (g, silhouette);
        };
        for (const auto& button : patterns)
            paintPatternGlow (button);
        paintPatternGlow (mutePattern);
    }
    void NativeEditor::mouseDown (const juce::MouseEvent& event)
    {
        if (showingCompactPadPreview() || (layout.phoneRowControls && patternPicker.isVisible())
            || grid.handleCellPopupMouseDown (event.originalComponent)
            || !event.mods.isLeftButtonDown() || controller.selected.none()
            || projectDetails.isVisible() || discardChanges.isVisible())
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
        auto options = juce::PopupMenu::Options().withTargetComponent (&rate);
       #if JUCE_IOS || JUCE_ANDROID
        options = options.withParentComponent (getTopLevelComponent());
       #endif
        menu.showMenuAsync (options
                                .withTargetScreenArea (target)
                                .withMinimumWidth (rate.getWidth())
                                .withStandardItemHeight (44)
                                .withMaximumNumColumns (1)
                                .withInitiallySelectedItem (selected),
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
    void NativeEditor::toggleAboutPanel()
    {
        controller.commit();
        grid.cancelGesture();
        layout.randomVisible = !layout.randomVisible;
        aboutViewport.setVisible (layout.randomVisible);
        if (layout.randomVisible)
            aboutViewport.toFront (false);
        if (isShowing())
            grabKeyboardFocus();
        repaint();
    }
    bool NativeEditor::textFocused() const { return dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr; }
    void NativeEditor::confirmDiscardingChanges (std::function<void()> proceed)
    {
        cancelCompactGestures();
        discardEpoch = controller.processor.getUiBridge().getDocumentEpoch();
        discardChanges.open (controller.processor.getProjectName(), std::move (proceed));
    }
    bool NativeEditor::keyPressed (const juce::KeyPress& key)
    {
        if (patternPicker.isVisible())
            return patternPicker.keyPressed (key);
        if (noteChooser.isVisible())
            return noteChooser.keyPressed (key);
        if (discardChanges.isVisible())
            return discardChanges.keyPressed (key);
        if (projectDetails.isVisible())
            return projectDetails.keyPressed (key);
        if (textFocused())
            return false;
        if (key.getModifiers().isCommandDown() && key.isKeyCode ('Z'))
        {
            if (key.getModifiers().isShiftDown())
                controller.redo();
            else
                controller.undo();
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
        {
            if (grid.dismissCyclePopup())
                return true;
            if (controller.exitSolo())
                return true;
            const bool exitZen = zen.getToggleState() && zenModeChanged;
            const bool closeCompactPanel = layout.compact && panel != Panel::none;
            const bool handled = exitZen || closeCompactPanel || layout.randomVisible
                || controller.selectHeld || controller.selectLatched || controller.copySource >= 0
                || grid.hasActiveGesture() || controller.editing();
            if (!handled)
                return false;
            if (zen.getToggleState() && zenModeChanged)
                zenModeChanged (false);
            if (layout.compact)
                closePanel.onClick();
            controller.cancelSelect();
            controller.copySource = -1;
            if (layout.randomVisible)
                toggleAboutPanel();
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
        const auto character = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        const int index = juce::String ("asdfghjk").indexOfChar (character);
        if (index >= 0 && index < layout.channels && !key.getModifiers().isCommandDown())
        {
            if (!heldPads.test (static_cast<size_t> (index)))
            {
                heldPads.set (static_cast<size_t> (index));
                triggerPad (index);
            }
            return true;
        }
        return false;
    }
    bool NativeEditor::keyStateChanged (bool down)
    {
        if (!down)
        {
            const juce::String keys ("ASDFGHJK");
            for (int i = 0; i < 8; ++i)
                if (!juce::KeyPress::isKeyCurrentlyDown (keys[i]))
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
            const float alpha = controller.copySource == i ? copyAlpha : 1.0f;
            if (button.getToggleState() != active || !juce::approximatelyEqual (button.getAlpha(), alpha))
                repaint (button.getBounds().expanded (patternGlowExtent));
            button.setToggleState (active, juce::dontSendNotification);
            button.setAlpha (alpha);
        }
    }
    void NativeEditor::updateLayerHeading()
    {
        const auto layer = grid.displayLayer();
        preview.setDisplayedLayer (layer);
        layerHint.setVisible (!layout.compact && overviewLayer (layer));
        layerHint.setText (overviewLayer (layer) ? juce::String (layer == Layer::nudge ? "Nudge" : layer == Layer::roll ? "Roll" : "Cycle") + " / overview" : juce::String(), juce::dontSendNotification);
        const bool selecting = grid.isSelectMode() || controller.selected.any();
        const bool chance = grid.displayLayer() == Layer::chance;
        velocityLayer.setToggleState (!selecting && grid.displayLayer() == Layer::velocity, juce::dontSendNotification);
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
        chanceLayer.setTitle ("Chance editing layer");
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
        grid.modifierKeysChanged (mods);
        duplicate.setToggleState (grid.isCopyMode(), juce::dontSendNotification);
        updateLayerHeading();
    }
    void NativeEditor::metronomeMenu (juce::Component& anchor)
    {
        constexpr int soundId = 1, pitchId = 10, volumeId = 100;
        const auto& current = controller.processor;
        juce::PopupMenu menu, pitch, volume;
        menu.setLookAndFeel (&look);
        menu.addSectionHeader ("Click sound");
        const std::array<const char*, 3> sounds { "Classic", "Woodblock", "Claves" };
        for (int index = 0; index < static_cast<int> (sounds.size()); ++index)
            menu.addItem (soundId + index, sounds[static_cast<size_t> (index)], true, static_cast<int> (current.getMetronomeSound()) == index);
        for (int offset = -1; offset <= 1; ++offset)
            pitch.addItem (pitchId + offset, offset < 0 ? "Low" : offset > 0 ? "High" : "Normal", true, offset == current.getMetronomePitch());
        for (const int percent : { 0, 10, 25, 50, 75, 100 })
            volume.addItem (volumeId + percent, juce::String (percent) + "%", true, percent == current.getMetronomeVolume());
        menu.addSubMenu ("Pitch", pitch);
        menu.addSubMenu ("Volume: " + juce::String (current.getMetronomeVolume()) + "%", volume);
        auto options = juce::PopupMenu::Options().withTargetComponent (&anchor).withStandardItemHeight (44);
       #if JUCE_IOS || JUCE_ANDROID
        options = options.withParentComponent (getTopLevelComponent());
       #endif
        const juce::Component::SafePointer<NativeEditor> safe (this);
        menu.showMenuAsync (options, [safe] (int result)
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
        auto options = juce::PopupMenu::Options().withTargetComponent (layout.phoneRowControls ? &recordOptions : static_cast<juce::TextButton*> (&recordQuantize));
       #if JUCE_IOS
        if (layout.phoneRowControls)
            options = options.withParentComponent (getTopLevelComponent()).withStandardItemHeight (44);
       #endif
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

    void NativeEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        look.refreshAccent();
        layerHint.setColour (juce::Label::textColourId, Palette::accent);
        grid.refreshAccent();
        syncRecordingPads();
        sendLookAndFeelChange();
        repaint();
    }
    void NativeEditor::triggerPad (int channel, int velocity)
    {
        if (controller.processor.triggerRowFromUi (channel, velocity) && recordingStrip.isVisible())
            recordingPads[static_cast<size_t> (channel)].flash (velocity);
    }

    void NativeEditor::syncRecordingPads()
    {
        const auto& playback = controller.state.playback;
        const juce::String keys ("ASDFGHJK");
        padVelocityMode.setAppearance (Palette::accent, {}, {});
        for (int channel = 0; channel < static_cast<int> (recordingPads.size()); ++channel)
        {
            auto& pad = recordingPads[static_cast<size_t> (channel)];
            const auto key = juce::String::charToString (keys[channel]);
            const auto note = juce::MidiMessage::getMidiNoteName (playback.channels[static_cast<size_t> (channel)].note, true, true, 4);
            pad.setTitle ("Trigger channel " + juce::String (channel + 1));
            pad.setAppearance (Palette::channel (channel, controller.drumColors), key, note);
            pad.setEnabled (channel < playback.channelCount && playback.deviceActive);
            pad.positionSensitive = padVelocityMode.getToggleState();
        }
    }

    void NativeEditor::layoutRecordingPads()
    {
        if (layout.phoneRowControls)
        {
            constexpr int columns = 4, gap = 8, switchWidth = 56;
            const auto bounds = recordingStrip.getLocalBounds().reduced (12, 6);
            const int side = juce::jmax (0, juce::jmin ((bounds.getWidth() - switchWidth - columns * gap) / columns,
                                                       (bounds.getHeight() - gap) / 2));
            const int totalWidth = columns * (side + gap) + switchWidth;
            const int x = bounds.getCentreX() - totalWidth / 2;
            const int y = bounds.getCentreY() - side - gap / 2;
            for (int index = 0; index < static_cast<int> (recordingPads.size()); ++index)
                recordingPads[static_cast<size_t> (index)].setBounds (x + (index % columns) * (side + gap), y + (index / columns) * (side + gap), side, side);
            padVelocityMode.setBounds (x + columns * (side + gap), bounds.getCentreY() - switchWidth / 2, switchWidth, switchWidth);
            return;
        }
        constexpr int gap = 6;
        constexpr int verticalPadding = 2;
        const int count = static_cast<int> (recordingPads.size());
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
        footerHelp.refresh();
        syncMidiOutput();
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
