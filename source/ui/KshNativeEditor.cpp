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
            text (g, title, bounds.reduced (horizontalPadding, 3).withHeight (15), 10, Palette::muted, true, juce::Justification::centredLeft);
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
        controller.keyboardModifiersEnabled = uiPreferences->getBoolValue ("keyboardModifiersEnabled", true);
        addAndMakeVisible (modifierToggle);
        modifierToggle.setTitle ("Shift/Option shortcuts");
        modifierToggle.setTooltip ("Enable Shift for selection and solo, and Option for Chance preview and copying. The Select and Copy buttons remain available.");
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
        controller.graphicalCellValues = uiPreferences->getBoolValue ("graphicalCellValues", false);
        addAndMakeVisible (cellDisplay);
        cellDisplay.getProperties().set ("icon", "cellDisplay");
        cellDisplay.setTitle ("Graphical cell values");
        cellDisplay.setTooltip ("Hide cell numbers: velocity uses fill height and probability uses a pie. Toggle again for numbers.");
        cellDisplay.onClick = [this]
        {
            controller.graphicalCellValues = !controller.graphicalCellValues;
            uiPreferences->setValue ("graphicalCellValues", controller.graphicalCellValues);
            uiPreferences->saveIfNeeded();
            sync();
        };
        addAndMakeVisible (rowOrder);
        rowOrder.setTitle ("Reverse row order");
        rowOrder.setTooltip ("Flip the displayed rows: channel 1 at the bottom or top. MIDI and pattern data stay the same.");
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
        tooltipToggle.setTooltip ("Turn control help in the footer on or off");
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
        addChildComponent (noteChooser);
        grid.noteChooserRequested = [this] (int ch, juce::Component& input) { showNoteChooser (ch, input); };
        noteChooser.onChoose = [this] (int note)
        {
            controller.refresh();
            if (noteChooserEpoch == controller.processor.getUiBridge().getDocumentEpoch()
                && juce::isPositiveAndBelow (noteChooserChannel, controller.state.playback.channelCount)
                && controller.state.playback.channels[static_cast<size_t> (noteChooserChannel)].note == noteChooserOriginal)
                controller.command ("channel_note", { noteChooserChannel + 1, note }, "Change channel note");
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
        const std::vector<juce::String> projectLabels { "NEW", "LOAD", "SAVE", juce::String::fromUTF8 ("◀"), juce::String::fromUTF8 ("▶"), "SAVE AS"
           #if JUCE_IOS
            , "SHARE"
           #endif
        };
        const std::vector<juce::String> projectTips { "Create a new project", "Load a project from disk", "Save the current project", "Load the previous project in this folder", "Load the next project in this folder", "Save a copy to a new project file"
           #if JUCE_IOS
            , "Share a project copy with AirDrop, Messages or another app"
           #endif
        };
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
        projectDetails.onClose = [this]
        {
            sync();
            if (isShowing())
                grabKeyboardFocus();
        };
        for (auto* b : { &mutePattern, &velocityLayer, &chanceLayer, &clear, &record, &left, &right, &undoButton, &redoButton, &rate, &play, &dc })
            addAndMakeVisible (*b);
        addAndMakeVisible (selectMode);
        addAndMakeVisible (stop);
        for (auto* button : { &play, &stop })
        {
            button->getProperties().set ("transport", true);
            button->getProperties().set ("icon", button == &play ? "play" : "stop");
            button->setTitle (button == &play ? "Play" : "Stop");
            button->setTooltip (button == &play ? "Play transport (Space toggles play/stop)" : "Stop transport (Space toggles play/stop)");
        }
        selectMode.getProperties().set ("selectMode", true);
        selectMode.setTitle ("Select modifier");
        selectMode.setTooltip ("Tap to latch Select; hold for momentary Shift. Select cells, adjust selected values, solo channels, or choose a pattern copy source.");
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
            b.setTooltip (juce::String::fromUTF8 ("Select pattern • Shift-click chooses copy source, then click destination"));
            b.onClick = [this, i] { controller.selectSource (i, controller.useSelectionModifier (juce::ModifierKeys::getCurrentModifiersRealtime())); };
           #if !JUCE_IOS && !JUCE_ANDROID
            b.setTooltip (b.getTooltip() + ". Drag to a DAW MIDI track to export one pattern pass.");
            b.onDrag = [this, i] { dragPattern (i); };
           #endif
        }
        velocityLayer.getProperties().set ("layerSegment", 0);
        chanceLayer.getProperties().set ("layerSegment", 1);
        undoButton.getProperties().set ("historySegment", 0);
        redoButton.getProperties().set ("historySegment", 1);
        const std::array<std::pair<juce::TextButton*, const char*>, 8> icons { { { &clear, "clear" }, { &record, "record" }, { &left, "previous" }, { &right, "next" }, { &undoButton, "undo" }, { &redoButton, "redo" }, { &projectButtons[3], "previous" }, { &projectButtons[4], "next" } } };
        for (auto [button, icon] : icons)
        {
            button->getProperties().set ("icon", icon);
            button->setTitle (icon);
        }
        for (auto* button : { &record, &left, &right, &clear })
            button->getProperties().set ("outlinedIcon", true);
        mutePattern.onClick = [this] { controller.selectSource (-1); };
        mutePattern.setTooltip ("Mute pattern playback");
        for (auto* button : { &velocityLayer, &chanceLayer })
            button->onClick = [this, button]
            {
                controller.commit();
                controller.selected.reset();
                controller.layer = button == &velocityLayer ? Layer::velocity : Layer::chance;
                controller.selectLatched = false;
                sync();
            };
        grid.layerChanged = [this] { updateLayerHeading(); };
        addAndMakeVisible (layerHint);
        layerHint.setFont (font (12, true));
        layerHint.setColour (juce::Label::textColourId, Palette::accent);
        layerHint.setBorderSize ({});
        layerHint.setJustificationType (juce::Justification::centredLeft);
        layerHint.setInterceptsMouseClicks (false, false);
        velocityLayer.setTooltip ("Show and edit cell velocity");
        chanceLayer.setTooltip ("Show and edit cell probability and cycle");
        clear.setTooltip ("Double-click to clear pattern");
        clear.onClick = [this] { const auto now = juce::Time::getMillisecondCounter(); if (now - static_cast<juce::uint32> (static_cast<int> (clear.getProperties()["lastClick"])) < 300) controller.clearPattern(); clear.getProperties().set ("lastClick", static_cast<int> (now)); };
        record.setTitle ("Arm Record");
        record.setTooltip ("Arm recording of MIDI or ASDFGHJK into selected pattern");
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
        left.setTooltip ("Shift all rows one step left within their loops");
        right.setTooltip ("Shift all rows one step right within their loops");
        undoButton.onClick = [this] { controller.undo(); };
        redoButton.onClick = [this] { controller.redo(); };
        undoButton.setTooltip (juce::String::fromUTF8 ("Undo • Cmd-Z"));
        redoButton.setTooltip (juce::String::fromUTF8 ("Redo • Cmd-Shift-Z"));
        rate.getProperties().set ("dropdown", true);
        rate.setTitle ("Pattern rate");
        rate.setTooltip ("Choose the step duration for this pattern");
        rate.setTriggeredOnMouseDown (true);
        rate.onClick = [this] { rateMenu(); };
        play.onClick = [this] { controller.command ("standalone_transport_playing", { true }); };
        stop.onClick = [this] { controller.command ("standalone_transport_playing", { false }); };
        dc.onClick = [this] { controller.drumColors = ! controller.drumColors; sync(); };
        dc.setTooltip ("Toggle channel colors");
        configureValue (swing, "Amount", 0, 100, "swing");
        configureValue (subdivision, "Target", 0, 2, "swing_subdivision");
        swing.setTitle ("Swing amount");
        swing.suffix = "%";
        swing.setTooltip ("Delay the swung subdivisions. 0% is straight timing; the grid shows the resulting positions.");
        subdivision.setTitle ("Swing target");
        subdivision.setTooltip ("1x swings every other step at the current grid division; 2x alternates two-step groups; 4x alternates four-step groups");
        subdivision.format = [] (int v) { return juce::String (1 << v) + "x"; };
        subdivision.resetValue = 1;
        configureValue (velocityHumanize, "Velocity", 0, 100, "velocity_humanize");
        configureValue (timingHumanize, "Timing", 0, 100, "timing_humanize");
        velocityHumanize.setTitle ("Velocity humanization");
        timingHumanize.setTitle ("Timing humanization");
        velocityHumanize.setTooltip ("Randomly vary hit velocity by up to this percentage of the cell's velocity. 0% keeps it steady.");
        timingHumanize.setTooltip ("Add subtle random timing variation. 0% is steady; 100% uses a range of one fifth of a step.");
        velocityHumanize.suffix = timingHumanize.suffix = "%";
        configureValue (steps, "Grid Length", 1, 32, "source_steps");
        steps.captionFontSize = 12;
        steps.captionColour = Palette::muted;
        steps.captionGap = 5;
        steps.resetValue = 16;
        configureValue (tempo, "", 20, 300, "standalone_tempo");
        tempo.suffix = " BPM";
        tempo.setTitle ("Tempo BPM");
        tempo.resetValue = 120;
        addAndMakeVisible (zen);
        zen.setTitle ("Zen mode");
        zen.setTooltip ("Fullscreen with a centered interface and adjustable scale. Escape exits Zen.");
        zen.onClick = [this] { if (zenModeChanged) zenModeChanged (!zen.getToggleState()); };
        addAndMakeVisible (scale);
        scale.setTitle ("UI scale percent");
        scale.minimum = UiScale::minimumPercent;
        scale.maximum = UiScale::maximumPercent;
        scale.resetValue = 100;
        scale.suffix = "%";
        scale.valueFontSize = 12;
        zen.getProperties().set ("icon", "zen");
        modifierToggle.getProperties().set ("icon", "modifiers");
        tooltipToggle.getProperties().set ("icon", "tips");
        dc.getProperties().set ("icon", "palette");
        dc.setTitle ("Channel colors");
        for (auto* button : { &cellDisplay, &zen, &tooltipToggle, &rowOrder, &modifierToggle, &dc })
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
    void NativeEditor::useViewportFit()
    {
        viewportFit = true;
        setZenMode (false);
    }
    void NativeEditor::setZenMode (bool enabled)
    {
        zen.setToggleState (enabled, juce::dontSendNotification);
        scale.minimum = UiScale::minimumPercent;
        scale.maximum = UiScale::maximumPercent;
        scale.setEnabled (!viewportFit || enabled);
        scale.format = viewportFit && !enabled ? std::function<juce::String (int)> ([] (int) { return juce::String ("Fit"); }) : nullptr;
        scale.setTitle (viewportFit && !enabled ? "UI scale: fit to screen" : "UI scale percent");
        scale.setTooltip (viewportFit && !enabled ? "Automatically fits the full interface to the available screen space" : "Adjust interface size; Zen keeps it centered within the screen");
        scale.setValue (controller.processor.getProjectUiScalePercent());
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
        noteChooser.open (getLocalArea (&input, input.getLocalBounds()), channel.label, channel.note, &input);
    }
    void NativeEditor::configureValue (ValueControl& control, const juce::String& caption, int minimum, int maximum, const std::string& command)
    {
        addAndMakeVisible (control);
        control.caption = caption;
        control.minimum = minimum;
        control.maximum = maximum;
        control.setTitle (caption.isEmpty() ? juce::String (command) : caption);
        control.setTooltip (juce::String::fromUTF8 ("Drag or scroll to change • double-click to type • right-click to reset"));
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
        grid.layout = layout;
        preview.reverseRows = randomPanel.reverseRows = layout.reverseRows;
        compactPattern.setButtonText (controller.copySource >= 0 ? "Copy P" + juce::String (controller.copySource + 1) : "P" + juce::String (controller.source() + 1) + "  v");
        page.setButtonText (layout.firstStep == 0 ? "1-16  >" : "<  17-" + juce::String (layout.steps));
        duplicate.setToggleState (grid.isCopyMode(), juce::dontSendNotification);
        updatePatternButtons();
        mutePattern.setToggleState (controller.source() < 0, juce::dontSendNotification);
        updateLayerHeading();
        for (auto* b : { &clear, &record, &left, &right, &rate, static_cast<juce::TextButton*> (&recordQuantize) })
            b->setEnabled (controller.source() >= 0);
        steps.setEnabled (controller.source() >= 0);
        undoButton.setEnabled (controller.canUndo());
        redoButton.setEnabled (controller.canRedo());
        swing.setValue (p.swing);
        subdivision.setValue (p.swingSubdivisionIndex);
        velocityHumanize.setValue (p.velocityHumanize);
        timingHumanize.setValue (p.timingHumanize);
        steps.setValue (p.stepCount);
        tempo.setValue (juce::roundToInt (controller.processor.getStandaloneTempoBpm()));
        scale.setValue (zen.getToggleState() ? juce::jmin (controller.processor.getProjectUiScalePercent(), scale.maximum) : controller.processor.getProjectUiScalePercent());
        static const std::array<std::string, 8> rates { "4n", "4nt", "8n", "8nt", "16n", "16nt", "32n", "32nt" };
        auto value = p.sourceSettings[static_cast<size_t> (juce::jmax (0, controller.source()))].rate;
        auto found = std::find (rates.begin(), rates.end(), value);
        rate.setButtonText (found == rates.end() ? "16th" : rateNames[static_cast<size_t> (found - rates.begin())]);
        const bool playing = controller.processor.isStandaloneTransportPlaying();
        play.setVisible (layout.standalone && !playing);
        stop.setVisible (layout.standalone && playing);
        tempo.setVisible (layout.standalone);
        zen.setVisible (layout.standalone && !layout.compact);
        play.setToggleState (playing, juce::dontSendNotification);
        stop.setToggleState (!playing, juce::dontSendNotification);
        record.setToggleState (controller.processor.isPatternRecordingEnabled(), juce::dontSendNotification);
        const int recordStrength = controller.processor.getPatternRecordingQuantizeStrength();
        recordQuantize.setToggleState (recordStrength == 100, juce::dontSendNotification);
        recordQuantize.setTitle (recordStrength == 100 ? "Record quantization on" : recordStrength == 0 ? "Record quantization off" : "Record quantization partial");
        recordQuantize.setTooltip ((recordStrength == 100 ? juce::String ("Grid locked") : recordStrength == 0 ? juce::String ("Preserve played timing") : "Record quantization " + juce::String (recordStrength) + "%")
                                  + ". Click to toggle; right-click or Shift-F10 for strength and recording offset ("
                                  + juce::String (controller.processor.getPatternRecordingOffsetMs()) + " ms).");
        dc.setToggleState (controller.drumColors, juce::dontSendNotification);
        cellDisplay.setToggleState (controller.graphicalCellValues, juce::dontSendNotification);
        modifierToggle.setToggleState (controller.keyboardModifiersEnabled, juce::dontSendNotification);
        rowOrder.setToggleState (layout.reverseRows, juce::dontSendNotification);
        tooltipToggle.setToggleState (footerHelp.isHelpEnabled(), juce::dontSendNotification);
        projectHeader.setProjectName (controller.processor.getProjectName());
        if (projectDetails.isVisible())
            projectDetails.refresh();
        if (noteChooser.isVisible())
        {
            if (noteChooserEpoch != controller.processor.getUiBridge().getDocumentEpoch()
                || !juce::isPositiveAndBelow (noteChooserChannel, p.channelCount)
                || p.channels[static_cast<size_t> (noteChooserChannel)].note != noteChooserOriginal)
                noteChooser.dismiss();
            else
                noteChooser.toFront (false);
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
        repaint (404, juce::roundToInt (layout.transportHeight()) + 63, 186, 16);
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
        steps.valueWidth = layout.compact ? 0 : 44;
        if (layout.compact)
        {
            resizedCompact();
            return;
        }
        const int top = static_cast<int> (layout.transportHeight()) + 4;
        const int rightEdge = getWidth() - Layout::outerGutter;
        projectButtons[0].setBounds (Layout::outerGutter, top + 27, 54, 32);
        projectButtons[1].setBounds (74, top + 27, 54, 32);
        projectButtons[2].setBounds (136, top + 27, 54, 32);
        projectButtons[5].setBounds (198, top + 27, 76, 32);
        projectButtons[3].setBounds (282, top + 27, 30, 32);
        projectHeader.setBounds (320, top + 27, 194, 32);
        projectButtons[4].setBounds (522, top + 27, 30, 32);
       #if JUCE_IOS
        projectButtons[6].setBounds (556, top + 27, 44, 32);
       #endif
        swing.setBounds (602, top + 25, 48, 43);
        subdivision.setBounds (666, top + 25, 40, 43);
        velocityHumanize.setBounds (774, top + 25, 48, 43);
        timingHumanize.setBounds (838, top + 25, 48, 43);
        const int patternLeft = rightEdge - 400;
        const int patternTop = static_cast<int> (layout.transportHeight()) + 10;
        constexpr int patternHeight = 70;
        for (int i = 0; i < 16; ++i)
            patterns[static_cast<size_t> (i)].setBounds (patternLeft + 12 + (i % 8) * 38, patternTop + (i / 8) * 37, 34, 33);
        mutePattern.setBounds (rightEdge - patternHeight, patternTop, patternHeight, patternHeight);
        const int y = static_cast<int> (layout.toolbarY());
        constexpr int layerHeight = 41;
        const int layerY = juce::roundToInt ((layout.toolbarY() + layout.gridY() - layerHeight) * 0.5f);
        velocityLayer.setBounds (Layout::outerGutter, layerY, 90, layerHeight);
        chanceLayer.setBounds (velocityLayer.getRight() - 1, velocityLayer.getY(), 90, velocityLayer.getHeight());
        selectMode.setBounds (chanceLayer.getRight() - 1, velocityLayer.getY(), 90, velocityLayer.getHeight());
        layerHint.setBounds (velocityLayer.getX(), velocityLayer.getBottom() + 4, selectMode.getRight() - velocityLayer.getX(), 18);
        undoButton.setBounds (selectMode.getRight() + 40, layerY, 48, layerHeight);
        redoButton.setBounds (undoButton.getRight() - 1, layerY, 48, layerHeight);
        constexpr int gridInputHeight = 28;
        const int gridInputY = y + 34;
        const int gridLabelHeight = 18 + steps.captionGap;
        rate.setBounds (rightEdge - rateControlWidth(), gridInputY, rateControlWidth(), gridInputHeight);
        steps.setBounds (rate.getX() - 92, gridInputY - gridLabelHeight, 84, gridLabelHeight + gridInputHeight);
        clear.setBounds (steps.getX() - 112, gridInputY, 48, gridInputHeight);
        right.setBounds (clear.getX() - 40, gridInputY, 32, gridInputHeight);
        left.setBounds (right.getX() - 34, gridInputY, 32, gridInputHeight);
        record.setBounds (left.getX() - 96, gridInputY, 48, gridInputHeight);
        recordQuantize.setBounds (record.getRight() + 4, record.getY(), 20, juce::roundToInt (record.getHeight() * 0.75f));
        play.setBounds (rightEdge - 138, 6, 32, 32);
        stop.setBounds (play.getBounds());
        tempo.setBounds (rightEdge - 98, play.getY(), 98, play.getHeight());
        const int randomY = juce::roundToInt (layout.gridY());
        const int aboutHeight = juce::roundToInt (layout.previewY()) - randomY;
        aboutViewport.setBounds (0, randomY, getWidth(), aboutHeight);
        const int scrollWidth = aboutHeight < 540 ? aboutViewport.getScrollBarThickness() : 0;
        randomPanel.setSize (getWidth() - scrollWidth, juce::jmax (540, aboutHeight));
        grid.setBounds (0, static_cast<int> (layout.gridY()), getWidth(), static_cast<int> (layout.gridHeight()));
        preview.setBounds (0, static_cast<int> (layout.previewY()), getWidth(), Layout::previewHeight);
        const int inspectorLeft = juce::roundToInt (layout.gridLeft() - 16.0f);
        const int inspectorRight = juce::roundToInt (layout.gridLeft() + layout.gridWidth() + 16.0f);
        preview.setInspectorBounds ({ inspectorLeft, 12, inspectorRight - inspectorLeft, Layout::previewHeight - 24 });
        const int footerControlY = getHeight() - Layout::footerHeight + Layout::outerGutter;
        int footerRight = rightEdge;
        const auto placeFooter = [&] (juce::Component& control, int width)
        {
            control.setBounds (footerRight - width, footerControlY, width, 32);
            footerRight -= width + 8;
        };
        placeFooter (scale, 66);
        if (layout.standalone)
            placeFooter (zen, 40);
        placeFooter (dc, 40);
        placeFooter (cellDisplay, 40);
        placeFooter (rowOrder, 40);
        placeFooter (modifierToggle, 40);
        placeFooter (tooltipToggle, 40);
        footerHelp.setBounds (Layout::outerGutter, footerControlY - 2, tooltipToggle.getX() - 2 * Layout::outerGutter, 36);
    }
    void NativeEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::app);
        if (layout.compact)
        {
            if (layout.steps <= 16)
                text (g, "KSH", { 450, 3, 108, 40 }, 21, Palette::accent, true);
            if (panel != Panel::none)
            {
                surface (g, { 4, 48, static_cast<float> (getWidth() - 8), layout.gridHeight() - 4 });
                if (panel == Panel::patterns)
                    text (g, controller.copySource >= 0 ? "Choose a destination to copy into. Tap P again to cancel." : "Choose a pattern. Select + pattern copies it to another slot.", { 18, 96, static_cast<float> (getWidth() - 36), 26 }, 13, Palette::muted, false, juce::Justification::centredLeft);
                else if (panel == Panel::project)
                    text (g, "PROJECT", { 18, 100, 110, 28 }, 12, Palette::accent, true, juce::Justification::centredLeft);
                else if (panel == Panel::feel)
                {
                    paintControlGroup (g, { 12, 104, 336, 110 }, "Swing");
                    paintControlGroup (g, { 356, 104, 392, 110 }, "Humanize");
                    text (g, "Drag a value vertically, or double-tap to enter it. Changes are shared with the host.", { 22, 218, static_cast<float> (getWidth() - 44), 36 }, 13, Palette::muted, false, juce::Justification::centredLeft);
                }
                else if (panel == Panel::channel)
                    text (g, "CHANNEL " + juce::String (selectedChannel + 1) + "  /  tap its name in the grid to audition", { 20, 96, static_cast<float> (getWidth() - 40), 24 }, 12, Palette::accent, true, juce::Justification::centredLeft);
                else if (panel == Panel::help)
                {
                    const std::array<juce::String, 5> lines {
                        "Tap cells to toggle. Drag across a row to paint; drag up/down to change its value.",
                        "Hold a cell, or tap Select, to select cells. Drag a rectangle to select several.",
                        "Turn Select off to move selected cells. Enable Copy to duplicate while dragging.",
                        "Deselect returns to drawing. Inspector values adjust all selected enabled cells.",
                        "Tap a channel name to audition. The ... button opens note, loop, direction and solo." };
                    for (int i = 0; i < static_cast<int> (lines.size()); ++i)
                        text (g, lines[static_cast<size_t> (i)], { 20, 100.0f + static_cast<float> (i) * 38, static_cast<float> (getWidth() - 40), 34 }, 13, Palette::text, false, juce::Justification::centredLeft);
                }
            }
            return;
        }
        text (g, "Grid Division", { static_cast<float> (rate.getX()), static_cast<float> (steps.getY()), static_cast<float> (rate.getWidth()), 18 }, steps.captionFontSize, steps.captionColour, true, juce::Justification::centredLeft);
        text (g, "Arm Record", record.getBounds().getUnion (recordQuantize.getBounds()).withY (steps.getY()).withHeight (18).toFloat(), steps.captionFontSize, steps.captionColour, true, juce::Justification::centredLeft);
        text (g, "Nudge", left.getBounds().getUnion (right.getBounds()).withY (steps.getY()).withHeight (18).toFloat(), steps.captionFontSize, steps.captionColour, true, juce::Justification::centredLeft);
        text (g, "Clear", clear.getBounds().withY (steps.getY()).withHeight (18).toFloat(), steps.captionFontSize, steps.captionColour, true, juce::Justification::centredLeft);
        g.setColour (Palette::border.withAlpha (0.5f));
        g.drawHorizontalLine (juce::roundToInt (layout.toolbarY()), 0, static_cast<float> (getWidth()));
        for (auto y : { layout.transportHeight(), layout.gridY(), layout.height() - Layout::footerHeight })
            g.drawHorizontalLine (static_cast<int> (y), 0, static_cast<float> (getWidth()));
        paintControlGroup (g, { 582, layout.transportHeight() + 10, 144, 70 }, "Swing", 20);
        paintControlGroup (g, { 754, layout.transportHeight() + 10, 152, 70 }, "Humanize", 20);
    }
    void NativeEditor::mouseDown (const juce::MouseEvent& event)
    {
        if (!event.mods.isLeftButtonDown() || controller.selected.none()
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
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&rate)
                                .withTargetScreenArea (target)
                                .withMinimumWidth (rate.getWidth())
                                .withStandardItemHeight (30)
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
            if (zen.getToggleState() && zenModeChanged)
                zenModeChanged (false);
            if (layout.compact)
                showPanel (Panel::none);
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
        const juce::String pads ("asdfghjk");
        const int index = pads.indexOfChar (character);
        if (index >= 0 && index < layout.channels && !key.getModifiers().isCommandDown())
        {
            if (!heldPads.test (static_cast<size_t> (index)))
            {
                heldPads.set (static_cast<size_t> (index));
                (void) controller.processor.triggerRowFromUi (index);
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
        // Keep the armed source highlighted; pulse smoothly between 70% and full opacity.
        const auto phase = static_cast<double> (juce::Time::getMillisecondCounter() % 200u) / 200.0;
        const float copyAlpha = 0.85f + 0.15f * static_cast<float> (std::cos (juce::MathConstants<double>::twoPi * phase));
        for (int i = 0; i < 16; ++i)
        {
            auto& button = patterns[static_cast<size_t> (i)];
            button.setToggleState (controller.copySource == i || controller.source() == i, juce::dontSendNotification);
            button.setAlpha (controller.copySource == i ? copyAlpha : 1.0f);
        }
    }
    void NativeEditor::updateLayerHeading()
    {
        const auto layer = grid.displayLayer();
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
    }
    void NativeEditor::modifierKeysChanged (const juce::ModifierKeys& mods)
    {
        grid.modifierKeysChanged (mods);
        duplicate.setToggleState (grid.isCopyMode(), juce::dontSendNotification);
        updateLayerHeading();
    }
    void NativeEditor::recordingMenu()
    {
        juce::PopupMenu menu, offsets;
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
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recordQuantize), [safe] (int result)
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
        sendLookAndFeelChange();
        repaint();
    }
    void NativeEditor::timerCallback()
    {
        if (!isShowing())
            return;
        footerHelp.refresh();
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
        grid.updatePlayback();
        preview.updatePlayback();
        if (layout.randomVisible || (layout.compact && panel == Panel::generate))
            randomPanel.updatePlayback();
        keyStateChanged (false);
    }
}
