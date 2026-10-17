#include "KshNativeEditor.h"
namespace ksh::ui
{
    NativeEditor::NativeEditor (PluginProcessor& processor) : controller (processor), grid (controller), preview (controller), randomPanel (controller, true)
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "KSH";
        options.filenameSuffix = ".ui.settings";
        options.folderName = "ofsound";
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = -1;
        uiPreferences = std::make_unique<juce::PropertiesFile> (options);
        if (uiPreferences->getBoolValue ("hoverTooltips", true))
            tooltips = std::make_unique<HoverTooltipWindow> (*this);
        addAndMakeVisible (tooltipToggle);
        tooltipToggle.setTitle ("Hover tooltips");
        tooltipToggle.setTooltip ("Turn hover tooltips on or off");
        tooltipToggle.onClick = [this]
        {
            if (tooltips)
                tooltips.reset();
            else
                tooltips = std::make_unique<HoverTooltipWindow> (*this);
            uiPreferences->setValue ("hoverTooltips", tooltips != nullptr);
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
        for (int i = 0; i < 5; ++i)
        {
            auto& button = projectButtons[static_cast<size_t> (i)];
            addAndMakeVisible (button);
            button.setButtonText (projectLabels[static_cast<size_t> (i)]);
            button.onClick = [this, i] { controller.commit(); if (projectAction) projectAction (i); };
        }
        for (auto* edit : { &title, &description })
        {
            addAndMakeVisible (*edit);
            edit->setFont (font (12, edit == &title));
            edit->setIndents (10, 0);
            edit->setJustification (juce::Justification::centredLeft);
            edit->getProperties().set ("projectMetadata", true);
            edit->onFocusLost = [this] { if (! syncing) controller.processor.setProjectMetadata (title.getText(), description.getText(), controller.processor.getProjectCreatedAt(), controller.processor.getProjectModifiedAt()); };
        }
        title.setTextToShowWhenEmpty ("Untitled Project", Palette::faint);
        description.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Add Description…"), Palette::faint);
        title.setTitle ("Project title");
        description.setTitle ("Project description");
        for (auto* b : { &mutePattern, &heading, &clear, &record, &left, &right, &undoButton, &redoButton, &rate, &play, &dc })
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
        heading.getProperties().set ("heading", true);
        addAndMakeVisible (patternHeading);
        static auto headingFace = juce::Typeface::createSystemTypefaceFor (BinaryData::GantariExtrabold_ttf, BinaryData::GantariExtrabold_ttfSize);
        patternHeading.setFont (juce::Font (juce::FontOptions (headingFace).withHeight (56.6f)));
        patternHeading.setColour (juce::Label::textColourId, Palette::accent);
        patternHeading.setBorderSize (juce::BorderSize<int> (0));
        patternHeading.setInterceptsMouseClicks (false, false);
        patternHeading.setTitle ("Current pattern");
        const std::array<std::pair<juce::TextButton*, const char*>, 8> icons { { { &clear, "clear" }, { &record, "record" }, { &left, "previous" }, { &right, "next" }, { &undoButton, "undo" }, { &redoButton, "redo" }, { &projectButtons[3], "previous" }, { &projectButtons[4], "next" } } };
        for (auto [button, icon] : icons)
        {
            button->getProperties().set ("icon", icon);
            button->setTitle (icon);
        }
        mutePattern.onClick = [this] { controller.selectSource (-1); };
        heading.onClick = [this] { controller.layer = static_cast<Layer> ((static_cast<int> (controller.layer) + 1) % 2); sync(); };
        grid.layerChanged = [this] { updateLayerHeading(); };
        heading.setTooltip (juce::String::fromUTF8 ("Switch Velocity / Chance editing layer • Hold Option for momentary Chance • Probability and cycle edits temporarily show Chance in the inspector"));
        clear.setTooltip ("Double-click to clear pattern");
        clear.onClick = [this] { const auto now = juce::Time::getMillisecondCounter(); if (now - static_cast<juce::uint32> (static_cast<int> (clear.getProperties()["lastClick"])) < 300) controller.clearPattern(); clear.getProperties().set ("lastClick", static_cast<int> (now)); };
        record.setTooltip ("Record MIDI or ASDFGHJK into selected pattern");
        record.onClick = [this] { controller.command ("pattern_record_enabled", { !controller.processor.isPatternRecordingEnabled(), controller.source() + 1 }); };
        left.onClick = [this] { controller.shift (-1, -1); };
        right.onClick = [this] { controller.shift (-1, 1); };
        undoButton.onClick = [this] { controller.undo(); };
        redoButton.onClick = [this] { controller.redo(); };
        undoButton.setTooltip (juce::String::fromUTF8 ("Undo • Cmd-Z"));
        redoButton.setTooltip (juce::String::fromUTF8 ("Redo • Cmd-Shift-Z"));
        rate.onClick = [this] { rateMenu(); };
        play.onClick = [this] { controller.command ("standalone_transport_playing", { true }); };
        stop.onClick = [this] { controller.command ("standalone_transport_playing", { false }); };
        dc.onClick = [this] { controller.drumColors = ! controller.drumColors; sync(); };
        configureValue (swing, "Swing", 0, 100, "swing");
        configureValue (subdivision, "Sub", 0, 2, "swing_subdivision");
        subdivision.format = [] (int v) { return juce::String (1 << v) + "x"; };
        subdivision.resetValue = 1;
        configureValue (velocityHumanize, "Vel %", 0, 100, "velocity_humanize");
        configureValue (timingHumanize, "Time %", 0, 100, "timing_humanize");
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
        layout.patternScale = static_cast<float> (controller.processor.getPatternViewScale());
        grid.layout = layout;
        compactPattern.setButtonText (controller.copySource >= 0 ? "Copy P" + juce::String (controller.copySource + 1) : "P" + juce::String (controller.source() + 1) + "  v");
        page.setButtonText (layout.firstStep == 0 ? "1-16  >" : "<  17-" + juce::String (layout.steps));
        duplicate.setToggleState (grid.copySelection, juce::dontSendNotification);
        for (int i = 0; i < 16; ++i)
            patterns[static_cast<size_t> (i)].setToggleState (controller.source() == i || controller.copySource == i, juce::dontSendNotification);
        mutePattern.setToggleState (controller.source() < 0, juce::dontSendNotification);
        const auto patternLabel = controller.source() < 0 ? juce::String ("M") : "P" + juce::String (controller.source() + 1);
        patternHeading.setText (patternLabel, juce::dontSendNotification);
        heading.setButtonText ("Velocity / Chance");
        const bool selecting = controller.shiftDown (juce::ModifierKeys::getCurrentModifiersRealtime());
        selectMode.setToggleState (selecting, juce::dontSendNotification);
        heading.getProperties().set ("selectActive", selecting);
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
        static const std::array<juce::String, 8> names { "Quarter", "Quarter Triplet", "Eighth", "Eighth Triplet", "16th", "16th Triplet", "32nd", "32nd Triplet" };
        auto value = p.sourceSettings[static_cast<size_t> (juce::jmax (0, controller.source()))].rate;
        auto found = std::find (rates.begin(), rates.end(), value);
        rate.setButtonText (found == rates.end() ? "16th" : names[static_cast<size_t> (found - rates.begin())] + juce::String::fromUTF8 ("   ▾"));
        play.setVisible (layout.standalone);
        stop.setVisible (layout.standalone);
        tempo.setVisible (layout.standalone);
        play.setToggleState (controller.processor.isStandaloneTransportPlaying(), juce::dontSendNotification);
        stop.setToggleState (!controller.processor.isStandaloneTransportPlaying(), juce::dontSendNotification);
        record.setToggleState (controller.processor.isPatternRecordingEnabled(), juce::dontSendNotification);
        dc.setToggleState (controller.drumColors, juce::dontSendNotification);
        tooltipToggle.setToggleState (tooltips != nullptr, juce::dontSendNotification);
        if (!title.hasKeyboardFocus (true))
            title.setText (controller.processor.getProjectName(), false);
        if (!description.hasKeyboardFocus (true))
            description.setText (controller.processor.getProjectDescription(), false);
        grid.sync();
        preview.sync();
        randomPanel.sync();
        if (layout.compact)
            syncChannel();
        resized();
        repaint (404, juce::roundToInt (layout.transportHeight()) + 63, 186, 16);
        repaint (0, getHeight() - 40, getWidth(), 40);
        syncing = false;
        if ((scaleChanged || std::abs (oldHeight - layout.height()) > 0.01f || std::abs (oldWidth - layout.width()) > 0.01f) && preferredSizeChanged)
            preferredSizeChanged();
    }
    void NativeEditor::resized()
    {
        if (layout.compact)
        {
            resizedCompact();
            return;
        }
        const int top = static_cast<int> (layout.transportHeight());
        projectButtons[0].setBounds (12, top + 27, 54, 32);
        projectButtons[1].setBounds (74, top + 27, 54, 32);
        projectButtons[2].setBounds (136, top + 27, 54, 32);
        projectButtons[3].setBounds (198, top + 27, 30, 32);
        title.setBounds (236, top + 27, 160, 32);
        description.setBounds (404, top + 27, 186, 32);
        projectButtons[4].setBounds (598, top + 27, 30, 32);
        swing.setBounds (650, top + 19, 42, 43);
        subdivision.setBounds (700, top + 19, 42, 43);
        velocityHumanize.setBounds (750, top + 19, 42, 43);
        timingHumanize.setBounds (800, top + 19, 44, 43);
        for (int i = 0; i < 16; ++i)
            patterns[static_cast<size_t> (i)].setBounds (926 + (i % 8) * 42, top + 10 + (i / 8) * 36, 36, 30);
        mutePattern.setBounds (1268, top + 10, 58, 66);
        const int y = static_cast<int> (layout.toolbarY());
        constexpr int patternLabelWidth = 112;
        patternHeading.setBounds (12, y + 8, patternLabelWidth, 52);
        heading.setBounds (patternHeading.getRight() + 8, y + 23, 129, 29);
        selectMode.setBounds (heading.getRight() - 1, heading.getY(), 64, heading.getHeight());
        record.setBounds (375, y + 13, 40, 41);
        left.setBounds (425, y + 13, 40, 41);
        right.setBounds (475, y + 13, 40, 41);
        clear.setBounds (525, y + 13, 40, 41);
        undoButton.setBounds (686, y + 13, 40, 41);
        redoButton.setBounds (736, y + 13, 40, 41);
        steps.setBounds (getWidth() - 374, y + 13, 120, 41);
        rate.setBounds (getWidth() - 246, y + 13, 220, 41);
        play.setBounds (getWidth() - 190, 6, 32, 32);
        stop.setBounds (getWidth() - 150, play.getY(), play.getWidth(), play.getHeight());
        tempo.setBounds (getWidth() - 110, play.getY(), 98, play.getHeight());
        const int randomY = juce::roundToInt (layout.toolbarY() + 68);
        const int aboutHeight = juce::roundToInt (layout.previewY()) - randomY;
        aboutViewport.setBounds (0, randomY, getWidth(), aboutHeight);
        const int scrollWidth = aboutHeight < 540 ? aboutViewport.getScrollBarThickness() : 0;
        randomPanel.setSize (getWidth() - scrollWidth, juce::jmax (540, aboutHeight));
        grid.setBounds (0, static_cast<int> (layout.gridY()), getWidth(), static_cast<int> (layout.gridHeight()));
        preview.setBounds (0, static_cast<int> (layout.previewY()), getWidth(), 164);
        preview.setInspectorBounds (juce::Rectangle<float> (layout.gridLeft() - 16.0f, 12, layout.gridWidth() + 32.0f, 140).toNearestInt());
        dc.setBounds (getWidth() - 128, getHeight() - 36, 36, 32);
        tooltipToggle.setBounds (getWidth() - 190, getHeight() - 36, 50, 32);
        scale.setBounds (getWidth() - 78, getHeight() - 36, 66, 32);
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
                    text (g, "Drag a value vertically, or double-tap to enter it. Changes are shared with the host.", { 22, 218, static_cast<float> (getWidth() - 44), 36 }, 13, Palette::muted, false, juce::Justification::centredLeft);
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
        for (auto y : { layout.transportHeight(), layout.toolbarY(), layout.gridY(), layout.height() - 40 })
            g.drawHorizontalLine (static_cast<int> (y), 0, static_cast<float> (getWidth()));
        text (g, controller.copySource >= 0 ? "Copy P" + juce::String (controller.copySource + 1) + juce::String::fromUTF8 (" → choose destination • Escape to cancel") : "Source " + juce::String (controller.source() + 1) + juce::String::fromUTF8 (" · ") + juce::String (layout.channels) + juce::String::fromUTF8 (" channels · Shift select · Option copy"), { 12, static_cast<float> (getHeight() - 40), static_cast<float> (getWidth() - 218), 40 }, 11, Palette::muted, false, juce::Justification::centredLeft);
        const auto date = controller.processor.getProjectModifiedAt().substring (0, 10);
        text (g, date, { 404, layout.transportHeight() + 63, 186, 16 }, 9, Palette::muted, false, juce::Justification::centredRight);
    }
    void NativeEditor::rateMenu()
    {
        juce::PopupMenu menu;
        const std::array<juce::String, 8> names { "Quarter", "Quarter triplet", "Eighth", "Eighth triplet", "16th", "16th triplet", "32nd", "32nd triplet" };
        for (int i = 0; i < 8; ++i)
            menu.addItem (i + 1, names[static_cast<size_t> (i)]);
        const juce::Component::SafePointer<NativeEditor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&rate), [safe] (int result) {
            if (safe == nullptr || result < 1)
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
        if (textFocused())
            return false;
        if (key.getModifiers().isCommandDown() && juce::CharacterFunctions::toLowerCase (key.getTextCharacter()) == 'z')
        {
            if (controller.useSelectionModifier (key.getModifiers()))
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
        const auto patternLabel = controller.source() < 0 ? juce::String ("M") : "P" + juce::String (controller.source() + 1);
        const bool chance = grid.displayLayer() == Layer::chance;
        heading.setToggleState (chance, juce::dontSendNotification);
        heading.setTitle (patternLabel + " editing layer: " + (chance ? "chance" : "velocity") + " active");
    }
    void NativeEditor::modifierKeysChanged (const juce::ModifierKeys& mods)
    {
        const bool selecting = controller.shiftDown (mods);
        selectMode.setToggleState (selecting, juce::dontSendNotification);
        if (static_cast<bool> (heading.getProperties()["selectActive"]) != selecting)
        {
            heading.getProperties().set ("selectActive", selecting);
            heading.repaint();
        }
        grid.modifierKeysChanged (mods);
        updateLayerHeading();
    }
    void NativeEditor::timerCallback()
    {
        if (!isShowing())
            return;
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
