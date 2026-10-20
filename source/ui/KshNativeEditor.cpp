#include "KshNativeEditor.h"
namespace ksh::ui
{
    namespace
    {
        void paintControlGroup (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title)
        {
            g.setColour (Palette::border.withAlpha (0.65f));
            g.drawRoundedRectangle (bounds, 5, 1);
            text (g, title, bounds.reduced (8, 3).withHeight (15), 10, Palette::muted, true, juce::Justification::centredLeft);
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

    NativeEditor::NativeEditor (PluginProcessor& processor) : controller (processor), grid (controller), preview (controller), randomPanel (controller, true), projectDetails (processor)
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "KSH";
        options.filenameSuffix = ".ui.settings";
        options.folderName = "ofsound";
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = -1;
        uiPreferences = std::make_unique<juce::PropertiesFile> (options);
        footerHelp.setHelpEnabled (uiPreferences->getBoolValue ("footerTooltips", true));
        layout.reverseRows = uiPreferences->getBoolValue ("reverseRows", false);
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
        addAndMakeVisible (grid);
        addAndMakeVisible (preview);
        addChildComponent (aboutViewport);
        aboutViewport.setViewedComponent (&randomPanel, false);
        aboutViewport.setScrollBarsShown (true, false);
        preview.onLogoDoubleClick = [this] { toggleAboutPanel(); };
        const std::array<juce::String, 5> projectLabels { "NEW", "LOAD", "SAVE", juce::String::fromUTF8 ("◀"), juce::String::fromUTF8 ("▶") };
        const std::array<juce::String, 5> projectTips { "Create a new project", "Load a project from disk", "Save the current project", "Load the previous project in this folder", "Load the next project in this folder" };
        for (int i = 0; i < 5; ++i)
        {
            auto& button = projectButtons[static_cast<size_t> (i)];
            addAndMakeVisible (button);
            button.setButtonText (projectLabels[static_cast<size_t> (i)]);
            button.setTooltip (projectTips[static_cast<size_t> (i)]);
            button.onClick = [this, i] { controller.commit(); if (projectAction) projectAction (i); };
        }
        addAndMakeVisible (projectHeader);
        addChildComponent (projectDetails);
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
        projectDetails.onSaveAs = [this] { if (projectAction) projectAction (5); };
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
        }
        velocityLayer.getProperties().set ("layerSegment", 0);
        chanceLayer.getProperties().set ("layerSegment", 1);
        const std::array<std::pair<juce::TextButton*, const char*>, 8> icons { { { &clear, "clear" }, { &record, "record" }, { &left, "previous" }, { &right, "next" }, { &undoButton, "undo" }, { &redoButton, "redo" }, { &projectButtons[3], "previous" }, { &projectButtons[4], "next" } } };
        for (auto [button, icon] : icons)
        {
            button->getProperties().set ("icon", icon);
            button->setTitle (icon);
        }
        mutePattern.onClick = [this] { controller.selectSource (-1); };
        mutePattern.setTooltip ("Mute pattern playback");
        for (auto* button : { &velocityLayer, &chanceLayer })
            button->onClick = [this, button]
            {
                controller.layer = button == &velocityLayer ? Layer::velocity : Layer::chance;
                controller.selectLatched = false;
                sync();
            };
        grid.layerChanged = [this] { updateLayerHeading(); };
        velocityLayer.setTooltip ("Show and edit cell velocity");
        chanceLayer.setTooltip ("Show and edit cell probability and cycle");
        clear.setTooltip ("Double-click to clear pattern");
        clear.onClick = [this] { const auto now = juce::Time::getMillisecondCounter(); if (now - static_cast<juce::uint32> (static_cast<int> (clear.getProperties()["lastClick"])) < 300) controller.clearPattern(); clear.getProperties().set ("lastClick", static_cast<int> (now)); };
        record.setTooltip ("Record MIDI or ASDFGHJK into selected pattern");
        record.onClick = [this] { controller.command ("pattern_record_enabled", { !controller.processor.isPatternRecordingEnabled(), controller.source() + 1 }); };
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
        configureValue (subdivision, "Subdivision", 0, 2, "swing_subdivision");
        swing.setTitle ("Swing amount");
        swing.suffix = "%";
        swing.setTooltip ("Delay the swung subdivisions. 0% is straight timing; the grid shows the resulting positions.");
        subdivision.setTitle ("Swing subdivision");
        subdivision.setTooltip ("Choose the subdivision used to group the swing timing");
        subdivision.format = [] (int v) { return juce::String (1 << v) + "x"; };
        subdivision.resetValue = 1;
        configureValue (velocityHumanize, "Velocity", 0, 100, "velocity_humanize");
        configureValue (timingHumanize, "Timing", 0, 100, "timing_humanize");
        velocityHumanize.setTitle ("Velocity humanization");
        timingHumanize.setTitle ("Timing humanization");
        velocityHumanize.setTooltip ("Randomly vary hit velocity by up to this percentage of the cell's velocity. 0% keeps it steady.");
        timingHumanize.setTooltip ("Add subtle random timing variation. 0% is steady; 100% uses a range of one fifth of a step.");
        velocityHumanize.suffix = timingHumanize.suffix = "%";
        configureValue (steps, "", 1, 32, "source_steps");
        steps.suffix = " steps";
        steps.resetValue = 16;
        configureValue (tempo, "", 20, 300, "standalone_tempo");
        tempo.suffix = " BPM";
        tempo.setTitle ("Tempo BPM");
        tempo.resetValue = 120;
        addAndMakeVisible (scale);
        scale.setTitle ("UI scale percent");
        scale.minimum = 50;
        scale.maximum = 120;
        scale.resetValue = 100;
        scale.suffix = "%";
        scale.onChange = [this] (int v) { controller.processor.setProjectUiScalePercent (v); if (preferredSizeChanged) preferredSizeChanged(); };
        controller.changed = [this] { sync(); };
        setupCompactControls();
        sync();
        startTimerHz (60);
    }
    NativeEditor::~NativeEditor()
    {
        stopTimer();
        juce::PopupMenu::dismissAllActiveMenus();
        controller.changed = {};
        setLookAndFeel (nullptr);
    }
    void NativeEditor::useViewportFit()
    {
        scale.setEnabled (false);
        scale.format = [] (int) { return juce::String ("Fit"); };
        scale.setTitle ("UI scale: fit to screen");
        scale.setTooltip ("Automatically fits the full interface to the available screen space");
        scale.onChange = {};
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
        for (int i = 0; i < 16; ++i)
            patterns[static_cast<size_t> (i)].setToggleState (controller.source() == i || controller.copySource == i, juce::dontSendNotification);
        mutePattern.setToggleState (controller.source() < 0, juce::dontSendNotification);
        updateLayerHeading();
        for (auto* b : { &clear, &record, &left, &right, &rate })
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
        scale.setValue (controller.processor.getProjectUiScalePercent());
        static const std::array<std::string, 8> rates { "4n", "4nt", "8n", "8nt", "16n", "16nt", "32n", "32nt" };
        auto value = p.sourceSettings[static_cast<size_t> (juce::jmax (0, controller.source()))].rate;
        auto found = std::find (rates.begin(), rates.end(), value);
        rate.setButtonText (found == rates.end() ? "16th" : rateNames[static_cast<size_t> (found - rates.begin())]);
        const bool playing = controller.processor.isStandaloneTransportPlaying();
        play.setVisible (layout.standalone && !playing);
        stop.setVisible (layout.standalone && playing);
        tempo.setVisible (layout.standalone);
        play.setToggleState (playing, juce::dontSendNotification);
        stop.setToggleState (!playing, juce::dontSendNotification);
        record.setToggleState (controller.processor.isPatternRecordingEnabled(), juce::dontSendNotification);
        dc.setToggleState (controller.drumColors, juce::dontSendNotification);
        rowOrder.setToggleState (layout.reverseRows, juce::dontSendNotification);
        tooltipToggle.setToggleState (footerHelp.isHelpEnabled(), juce::dontSendNotification);
        projectHeader.setProjectName (controller.processor.getProjectName());
        if (projectDetails.isVisible())
            projectDetails.refresh();
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
        repaint (404, juce::roundToInt (layout.transportHeight()) + 63, 186, 16);
        repaint (0, getHeight() - Layout::footerHeight, getWidth(), Layout::footerHeight);
        syncing = false;
        if ((scaleChanged || std::abs (oldHeight - layout.height()) > 0.01f || std::abs (oldWidth - layout.width()) > 0.01f) && preferredSizeChanged)
            preferredSizeChanged();
    }
    void NativeEditor::resized()
    {
        laidOut = { layout, getLocalBounds() };
        projectDetails.setBounds (getLocalBounds());
        if (layout.compact)
        {
            resizedCompact();
            return;
        }
        const int top = static_cast<int> (layout.transportHeight());
        const int rightEdge = getWidth() - Layout::outerGutter;
        projectButtons[0].setBounds (Layout::outerGutter, top + 27, 54, 32);
        projectButtons[1].setBounds (74, top + 27, 54, 32);
        projectButtons[2].setBounds (136, top + 27, 54, 32);
        projectButtons[3].setBounds (198, top + 27, 30, 32);
        projectHeader.setBounds (236, top + 27, 278, 32);
        projectButtons[4].setBounds (522, top + 27, 30, 32);
        swing.setBounds (582, top + 25, 72, 43);
        subdivision.setBounds (662, top + 25, 72, 43);
        velocityHumanize.setBounds (758, top + 25, 72, 43);
        timingHumanize.setBounds (838, top + 25, 72, 43);
        for (int i = 0; i < 16; ++i)
            patterns[static_cast<size_t> (i)].setBounds (rightEdge - 400 + (i % 8) * 42, top + 10 + (i / 8) * 36, 36, 30);
        mutePattern.setBounds (rightEdge - 58, top + 10, 58, 66);
        const int y = static_cast<int> (layout.toolbarY());
        velocityLayer.setBounds (Layout::outerGutter, y + 23, 65, 29);
        chanceLayer.setBounds (velocityLayer.getRight() - 1, velocityLayer.getY(), 65, velocityLayer.getHeight());
        selectMode.setBounds (chanceLayer.getRight() - 1, velocityLayer.getY(), 64, velocityLayer.getHeight());
        record.setBounds (375, y + 13, 40, 41);
        left.setBounds (425, y + 13, 40, 41);
        right.setBounds (475, y + 13, 40, 41);
        clear.setBounds (525, y + 13, 40, 41);
        undoButton.setBounds (686, y + 13, 40, 41);
        redoButton.setBounds (736, y + 13, 40, 41);
        rate.setBounds (rightEdge - rateControlWidth(), y + 13, rateControlWidth(), 41);
        steps.setBounds (rate.getX() - 128, y + 13, 120, 41);
        play.setBounds (rightEdge - 138, 6, 32, 32);
        stop.setBounds (play.getBounds());
        tempo.setBounds (rightEdge - 98, play.getY(), 98, play.getHeight());
        const int randomY = juce::roundToInt (layout.toolbarY() + 68);
        const int aboutHeight = juce::roundToInt (layout.previewY()) - randomY;
        aboutViewport.setBounds (0, randomY, getWidth(), aboutHeight);
        const int scrollWidth = aboutHeight < 540 ? aboutViewport.getScrollBarThickness() : 0;
        randomPanel.setSize (getWidth() - scrollWidth, juce::jmax (540, aboutHeight));
        grid.setBounds (0, static_cast<int> (layout.gridY()), getWidth(), static_cast<int> (layout.gridHeight()));
        preview.setBounds (0, static_cast<int> (layout.previewY()), getWidth(), 164);
        const int inspectorLeft = juce::roundToInt (layout.gridLeft() - 16.0f);
        preview.setInspectorBounds ({ inspectorLeft, 12, rightEdge - inspectorLeft, 140 });
        const int footerControlY = getHeight() - Layout::footerHeight + Layout::outerGutter;
        dc.setBounds (rightEdge - 116, footerControlY, 36, 32);
        rowOrder.setBounds (rightEdge - 164, footerControlY, 36, 32);
        tooltipToggle.setBounds (rightEdge - 226, footerControlY, 50, 32);
        scale.setBounds (rightEdge - 66, footerControlY, 66, 32);
        footerHelp.setBounds (Layout::outerGutter, footerControlY, tooltipToggle.getX() - 2 * Layout::outerGutter, 32);
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
        g.setColour (Palette::border.withAlpha (0.5f));
        for (auto y : { layout.transportHeight(), layout.toolbarY(), layout.gridY(), layout.height() - Layout::footerHeight })
            g.drawHorizontalLine (static_cast<int> (y), 0, static_cast<float> (getWidth()));
        paintControlGroup (g, { 574, layout.transportHeight() + 6, 168, 70 }, "Swing");
        paintControlGroup (g, { 750, layout.transportHeight() + 6, 168, 70 }, "Humanize");
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
    bool NativeEditor::keyPressed (const juce::KeyPress& key)
    {
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
    void NativeEditor::updateLayerHeading()
    {
        const bool selecting = grid.isSelectMode();
        const bool chance = grid.displayLayer() == Layer::chance;
        velocityLayer.setToggleState (!selecting && !chance, juce::dontSendNotification);
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
    void NativeEditor::timerCallback()
    {
        if (!isShowing())
            return;
        footerHelp.refresh();
        // Drain hits immediately before repainting, without an audio-thread wakeup
        // or an additional frame of delay between the processor and editor timers.
        controller.processor.emitPendingNoteHitsForUi();
        controller.poll();
        modifierKeysChanged (juce::ModifierKeys::getCurrentModifiersRealtime());
        grid.updatePlayback();
        preview.updatePlayback();
        if (layout.randomVisible || (layout.compact && panel == Panel::generate))
            randomPanel.updatePlayback();
        keyStateChanged (false);
    }
}
