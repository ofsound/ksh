#include "KshNativeEditor.h"
namespace ksh::ui
{
    NativeEditor::NativeEditor (PluginProcessor& processor) : controller (processor), grid (controller), preview (controller), randomPanel (controller, true)
    {
        setLookAndFeel (&look);
        setOpaque (true);
        setWantsKeyboardFocus (true);
        addAndMakeVisible (grid);
        addAndMakeVisible (preview);
        addChildComponent (randomPanel);
        addAndMakeVisible (randomButton);
        randomButton.setTooltip ("Show random mode, refresh length, and channel locks");
        randomButton.onClick = [this]
        {
            controller.commit();
            grid.cancelGesture();
            layout.randomVisible = !layout.randomVisible;
            randomButton.setToggleState (layout.randomVisible, juce::dontSendNotification);
            randomPanel.setVisible (layout.randomVisible);
            if (layout.randomVisible)
                randomPanel.toFront (false);
            resized();
            repaint();
        };
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
        for (int i = 0; i < 16; ++i)
        {
            auto& b = patterns[static_cast<size_t> (i)];
            addAndMakeVisible (b);
            b.setButtonText (juce::String (i + 1));
            b.setTooltip (juce::String::fromUTF8 ("Select pattern • Shift-click chooses copy source, then click destination"));
            b.onClick = [this, i] { controller.selectSource (i, juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown()); };
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
        heading.setTooltip (juce::String::fromUTF8 ("Switch editing layer • 1 velocity, 2 chance • Hold Option / Alt for chance • Roll in inspector"));
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
        play.onClick = [this] { controller.command ("standalone_transport_playing", { !controller.processor.isStandaloneTransportPlaying() }); };
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
        grid.displayLayerChanged = [this] { sync(); };
        controller.changed = [this] { sync(); };
        sync();
        startTimerHz (60);
    }
    NativeEditor::~NativeEditor()
    {
        stopTimer();
        controller.changed = {};
        setLookAndFeel (nullptr);
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
        layout.standalone = controller.processor.hasStandaloneTransport();
        layout.patternScale = static_cast<float> (controller.processor.getPatternViewScale());
        grid.layout = layout;
        for (int i = 0; i < 16; ++i)
            patterns[static_cast<size_t> (i)].setToggleState (controller.source() == i || controller.copySource == i, juce::dontSendNotification);
        mutePattern.setToggleState (controller.source() < 0, juce::dontSendNotification);
        static const std::array<juce::String, 2> layers { "velocity", "chance" };
        const auto patternLabel = controller.source() < 0 ? juce::String ("M") : "P" + juce::String (controller.source() + 1);
        patternHeading.setText (patternLabel, juce::dontSendNotification);
        heading.setButtonText ("Velocity / Chance");
        heading.setToggleState (grid.displayLayer() == Layer::chance, juce::dontSendNotification);
        heading.setTitle (patternLabel + " editing layer: " + layers[static_cast<size_t> (grid.displayLayer())] + " active");
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
        tempo.setVisible (layout.standalone);
        play.setButtonText (controller.processor.isStandaloneTransportPlaying() ? "Stop" : "Play");
        record.setToggleState (controller.processor.isPatternRecordingEnabled(), juce::dontSendNotification);
        dc.setToggleState (controller.drumColors, juce::dontSendNotification);
        if (!title.hasKeyboardFocus (true))
            title.setText (controller.processor.getProjectName(), false);
        if (!description.hasKeyboardFocus (true))
            description.setText (controller.processor.getProjectDescription(), false);
        grid.sync();
        preview.sync();
        randomPanel.sync();
        resized();
        repaint (404, juce::roundToInt (layout.transportHeight()) + 63, 186, 16);
        repaint (0, getHeight() - 40, getWidth(), 40);
        syncing = false;
        if ((scaleChanged || std::abs (oldHeight - layout.height()) > 0.01f || std::abs (oldWidth - layout.width()) > 0.01f) && preferredSizeChanged)
            preferredSizeChanged();
    }
    void NativeEditor::resized()
    {
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
        const int patternLabelWidth = juce::GlyphArrangement::getStringWidthInt (patternHeading.getFont(), patternHeading.getText()) + 4;
        patternHeading.setBounds (12, y + 8, patternLabelWidth, 52);
        heading.setBounds (patternHeading.getRight() + 8, y + 23, 129, 29);
        record.setBounds (305, y + 13, 40, 41);
        left.setBounds (355, y + 13, 40, 41);
        right.setBounds (405, y + 13, 40, 41);
        clear.setBounds (455, y + 13, 40, 41);
        randomButton.setBounds (852, top + 27, 66, 32);
        undoButton.setBounds (686, y + 13, 40, 41);
        redoButton.setBounds (736, y + 13, 40, 41);
        steps.setBounds (getWidth() - 374, y + 13, 120, 41);
        rate.setBounds (getWidth() - 246, y + 13, 220, 41);
        play.setBounds (getWidth() - 184, 8, 64, 28);
        tempo.setBounds (getWidth() - 110, play.getY(), 98, play.getHeight());
        const int randomY = juce::roundToInt (layout.toolbarY() + 68);
        randomPanel.setBounds (0, randomY, getWidth(), getHeight() - randomY);
        grid.setBounds (0, static_cast<int> (layout.gridY()), getWidth(), static_cast<int> (layout.gridHeight()));
        preview.setBounds (0, static_cast<int> (layout.previewY()), getWidth(), 164);
        preview.setInspectorBounds (juce::Rectangle<float> (layout.gridLeft() - 16.0f, 12, layout.gridWidth() + 32.0f, 140).toNearestInt());
        dc.setBounds (getWidth() - 128, getHeight() - 36, 36, 32);
        scale.setBounds (getWidth() - 78, getHeight() - 36, 66, 32);
    }
    void NativeEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::app);
        g.setColour (Palette::border.withAlpha (0.5f));
        for (auto y : { layout.transportHeight(), layout.toolbarY(), layout.gridY(), layout.height() - 40 })
            g.drawHorizontalLine (static_cast<int> (y), 0, static_cast<float> (getWidth()));
        text (g, controller.copySource >= 0 ? "Copy P" + juce::String (controller.copySource + 1) + juce::String::fromUTF8 (" → choose destination • Escape to cancel") : "Source " + juce::String (controller.source() + 1) + juce::String::fromUTF8 (" · ") + juce::String (layout.channels) + juce::String::fromUTF8 (" channels · Shift select · Option chance / copy"), { 12, static_cast<float> (getHeight() - 40), static_cast<float> (getWidth() - 156), 40 }, 11, Palette::muted, false, juce::Justification::centredLeft);
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
    bool NativeEditor::textFocused() const { return dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr; }
    bool NativeEditor::keyPressed (const juce::KeyPress& key)
    {
        if (textFocused())
            return false;
        if (key.getModifiers().isCommandDown() && juce::CharacterFunctions::toLowerCase (key.getTextCharacter()) == 'z')
        {
            if (key.getModifiers().isShiftDown())
                controller.redo();
            else
                controller.undo();
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
        {
            controller.copySource = -1;
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
            play.triggerClick();
            return true;
        }
        const auto character = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        if (character >= '1' && character <= '2')
        {
            controller.layer = static_cast<Layer> (character - '1');
            sync();
            return true;
        }
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
            grid.cancelGesture();
    }
    void NativeEditor::timerCallback()
    {
        if (!isShowing())
            return;
        // Drain hits immediately before repainting, without an audio-thread wakeup
        // or an additional frame of delay between the processor and editor timers.
        controller.processor.emitPendingNoteHitsForUi();
        controller.poll();
        grid.modifierKeysChanged (juce::ModifierKeys::getCurrentModifiersRealtime());
        grid.updatePlayback();
        preview.updatePlayback();
        if (layout.randomVisible)
            randomPanel.updatePlayback();
        keyStateChanged (false);
    }
}
