#include <PluginEditor.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace ksh;
using namespace ksh::ui;

namespace
{
    juce::TextButton& phoneButton (NativeEditor& editor, const juce::String& label)
    {
        std::function<juce::TextButton* (juce::Component&)> find = [&] (juce::Component& parent) -> juce::TextButton*
        {
            for (auto* child : parent.getChildren())
            {
                if (!child->isVisible())
                    continue;
                if (auto* button = dynamic_cast<juce::TextButton*> (child); button && (button->getTitle() == label || button->getButtonText() == label))
                    return button;
                if (auto* nested = find (*child))
                    return nested;
            }
            return nullptr;
        };
        if (auto* result = find (editor))
            return *result;
        FAIL ("Missing phone button: " + label.toStdString());
        throw std::runtime_error ("Missing phone button");
    }

    PatternGrid& phoneGrid (NativeEditor& editor)
    {
        for (auto* child : editor.getChildren())
            if (auto* grid = dynamic_cast<PatternGrid*> (child))
                return *grid;
        throw std::runtime_error ("Missing phone grid");
    }

    juce::MouseEvent phoneEvent (juce::Component& component, juce::Point<float> point, juce::Point<float> origin,
                                int modifiers = juce::ModifierKeys::leftButtonModifier)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), point, juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &component, &component, now, origin, now, 1, point.getDistanceFrom (origin) > 0 };
    }

    void capturePhone (NativeEditor& editor, const juce::String& name)
    {
        const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
        if (directory.isEmpty())
            return;
        juce::File root (directory);
        REQUIRE (root.createDirectory().wasOk());
        auto stream = root.getChildFile (name + ".png").createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (stream->setPosition (0));
        REQUIRE (stream->truncate().wasOk());
        REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f), *stream));
    }
}

TEST_CASE ("An iPad AUv3 viewport transition preserves modal state and restores grid access", "[native-ui][auv3][resize]")
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AudioUnitv3);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    const auto setCompact = [&] (bool compact)
    {
        editor.setCompactViewport (compact, 814, 390, false);
        editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    };
    auto& grid = phoneGrid (editor);
    NoteChooser* chooser = nullptr;
    DiscardChanges* discard = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* noteChooser = dynamic_cast<NoteChooser*> (child)) chooser = noteChooser;
        if (auto* discardDialog = dynamic_cast<DiscardChanges*> (child)) discard = discardDialog;
    }
    REQUIRE (chooser != nullptr);
    REQUIRE (discard != nullptr);
    setCompact (true);

    SECTION ("A dismissed overlay stays closed")
    {
        editor.confirmDiscardingChanges ([] { FAIL ("Unexpected project replacement"); });
        REQUIRE (discard->isVisible());
        discard->dismiss();
        setCompact (false);
        CHECK_FALSE (chooser->isVisible());
        CHECK_FALSE (discard->isVisible());
    }
    SECTION ("An open note chooser can still be dismissed after expansion")
    {
        REQUIRE (grid.rowTitle (0) != nullptr);
        grid.noteChooserRequested (0, *grid.rowTitle (0));
        REQUIRE (chooser->isVisible());
        CHECK_FALSE (grid.isVisible());
        CHECK_FALSE (chooser->isAlwaysOnTop());
        setCompact (false);
        CHECK (chooser->isVisible());
        CHECK (chooser->isAlwaysOnTop());
        CHECK (chooser->getBounds() == editor.getLocalBounds());
        CHECK (editor.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        CHECK_FALSE (chooser->isVisible());
        CHECK_FALSE (discard->isVisible());
    }
    SECTION ("An anchored color callout closes when its viewport changes")
    {
        REQUIRE (grid.rowTitle (0) != nullptr);
        grid.trackColourRequested (0, *grid.rowTitle (0));
        auto* callout = dynamic_cast<juce::CallOutBox*> (juce::Component::getCurrentlyModalComponent());
        REQUIRE (callout != nullptr);
        editor.sync();
        CHECK (callout->isVisible());
        setCompact (false);
        CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
        for (auto* child : editor.getChildren())
            CHECK (dynamic_cast<juce::CallOutBox*> (child) == nullptr);
    }

    CHECK (grid.isVisible());
    const auto cell = grid.layout.cell ({ 0, 0 }).getCentre();
    const auto initiallyEnabled = editor.controller.cell ({ 0, 0 }).enabled;
    grid.mouseDown (phoneEvent (grid, cell, cell));
    grid.mouseUp (phoneEvent (grid, cell, cell, 0));
    CHECK (editor.controller.cell ({ 0, 0 }).enabled != initiallyEnabled);
}

TEST_CASE ("iPhone landscape fits eight channels sixteen steps and the inspector", "[native-ui][iphone][render]")
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    REQUIRE (processor.dispatchUiEngineCommand ("channels", { 8 }));
    const std::array<const char*, 8> names { "Kick", "Snare", "Hat", "Open", "Clap", "Rim", "Tom", "Perc" };
    for (int ch = 0; ch < 8; ++ch)
    {
        REQUIRE (processor.dispatchUiEngineCommand ("channel_label", { ch + 1, names[static_cast<size_t> (ch)] }));
        for (int s = 0; s < 16; ++s)
            if ((s + ch) % (ch % 4 + 2) == 0)
                REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, ch + 1, s + 1, 1, 75 + (s * 7 + ch * 11) % 52, 100, 1, 1, 1 }));
    }
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 409);
    editor.setSize (814, 409);
    editor.controller.selected.set (0);
    editor.controller.selected.set (4);
    editor.sync();
    auto& grid = phoneGrid (editor);
    CHECK (grid.isVisible());
    CHECK (grid.layout.cellWidth() >= 42.0f);
    CHECK (grid.layout.cellHeight() >= 33.0f);
    CHECK (grid.getY() == Layout::compactHeaderHeight);
    CHECK (grid.getBottom() + Layout::compactInspectorHeight == editor.getHeight());
    for (int ch = 0; ch < 8; ++ch)
        for (int s = 0; s < 16; ++s)
        {
            const Location location { ch, s };
            const auto bounds = grid.layout.cell (location);
            CHECK (grid.layout.at (bounds.getCentre()) == location);
            CHECK (grid.getLocalBounds().toFloat().contains (bounds));
        }
    for (auto* child : editor.getChildren())
    {
        if (!child->isVisible())
            continue;
        CHECK (editor.getLocalBounds().contains (child->getBounds()));
        if (auto* inspector = dynamic_cast<PreviewPanel*> (child))
        {
            CHECK (inspector->getHeight() == Layout::compactInspectorHeight);
            for (auto* control : inspector->getChildren())
                if (control->isVisible())
                    CHECK (inspector->getLocalBounds().contains (control->getBounds()));
        }
    }
    capturePhone (editor, "iphone-16-inspector");
    auto& tempoButton = phoneButton (editor, "Set tempo");
    CHECK (tempoButton.getButtonText() == "120 BPM");
    CHECK (tempoButton.getProperties().contains ("transport"));
    CHECK (tempoButton.getY() == 0);
    tempoButton.onClick();
    CHECK (grid.isVisible());
    juce::Slider* fader = nullptr;
    for (auto* child : editor.getChildren())
        if (child->isVisible() && child->getTitle() == "Tempo")
            for (auto* control : child->getChildren())
                if (control->getTitle() == "Tempo BPM fader")
                    fader = dynamic_cast<juce::Slider*> (control);
    REQUIRE (fader != nullptr);
    CHECK (fader->isVisible());
    CHECK (fader->getWidth() < editor.getWidth() / 2);
    CHECK (fader->getSliderSnapsToMousePosition());
    fader->setValue (168, juce::sendNotificationSync);
    CHECK (processor.getStandaloneTempoBpm() == 168.0);
    phoneButton (editor, "Increase tempo by 1 BPM").onClick();
    CHECK (processor.getStandaloneTempoBpm() == 169.0);
    phoneButton (editor, "Decrease tempo by 1 BPM").onClick();
    CHECK (processor.getStandaloneTempoBpm() == 168.0);
    CHECK (phoneButton (editor, "Tap Tempo").getHeight() >= 44);
    capturePhone (editor, "iphone-tempo");
    CHECK (editor.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (fader->getParentComponent()->isVisible());
    CHECK (grid.isVisible());
    CHECK (tempoButton.getButtonText() == "168 BPM");
    auto& menu = phoneButton (editor, "Open menu");
    const auto menuBounds = menu.getBounds();
    CHECK (menu.getProperties().contains ("dropdown"));
    CHECK_FALSE (menu.getToggleState());
    menu.onClick();
    CHECK_FALSE (grid.isVisible());
    for (const auto* tab : { "Project", "Feel", "Settings", "About" })
    {
        phoneButton (editor, tab).onClick();
        CHECK (menu.getBounds() == menuBounds);
        CHECK (menu.getToggleState());
        CHECK (menu.getButtonText() == "Menu");
        for (auto* child : editor.getChildren())
            if (child->isVisible())
            {
                CHECK (editor.getLocalBounds().contains (child->getBounds()));
                if (child->getY() < Layout::compactHeaderHeight)
                    CHECK (child == &menu);
            }
        capturePhone (editor, "iphone-panel-" + juce::String (tab).toLowerCase());
    }
    auto& backdrop = phoneButton (editor, "Background tone");
    CHECK (phoneButton (editor, "Text brightness").isVisible());
    const juce::Point<int> motifCentre { editor.getWidth() * 3 / 4, editor.getHeight() / 2 };
    CHECK (backdrop.getScreenBounds().contains (editor.localPointToGlobal (motifCentre)));
    phoneButton (editor, "Close menu").onClick();
    CHECK (grid.isVisible());
    CHECK_FALSE (menu.getToggleState());
    grid.channelSettings (1);
    CHECK_FALSE (grid.isVisible());
    phoneButton (editor, "Reverse").onClick();
    phoneButton (editor, "Ping-pong").onClick();
    CHECK (editor.controller.state.playback.channels[1].playbackMode == PlaybackMode::reverse_ping_pong);
    CHECK (phoneButton (editor, "Reverse").getX() > 356);
    CHECK (phoneButton (editor, "Ping-pong").getToggleState());
    capturePhone (editor, "iphone-panel-channel");
    phoneButton (editor, "Done").onClick();
    auto& patternBank = phoneButton (editor, "Pattern bank");
    const auto patternBankBounds = patternBank.getBounds();
    patternBank.onClick();
    CHECK (patternBank.getBounds() == patternBankBounds);
    CHECK (patternBank.getToggleState());
    for (auto* child : editor.getChildren())
        if (child->isVisible() && child->getY() < Layout::compactHeaderHeight)
            CHECK (child == &patternBank);
    CHECK_FALSE (grid.isVisible());
    CHECK (phoneButton (editor, "Muted Pattern").getButtonText() == "Muted Pattern");
    auto* bank = phoneButton (editor, "Choose pattern 1").getParentComponent();
    REQUIRE (bank != nullptr);
    CHECK (bank->getBounds() == editor.getLocalBounds().withTrimmedTop (Layout::compactHeaderHeight));
    std::vector<juce::Rectangle<int>> padBounds;
    for (int i = 1; i <= 16; ++i)
    {
        const auto bounds = phoneButton (editor, "Choose pattern " + juce::String (i)).getBounds();
        CHECK (bank->getLocalBounds().contains (bounds));
        CHECK (bounds.getWidth() >= 88);
        CHECK (bounds.getHeight() >= 120);
        for (const auto& other : padBounds)
            CHECK_FALSE (other.intersects (bounds));
        padBounds.push_back (bounds);
    }
    CHECK (padBounds.back().getRight() == bank->getWidth() - 12);
    capturePhone (editor, "iphone-pattern-bank");
    phoneButton (editor, "Muted Pattern").onClick();
    CHECK (editor.controller.source() == -1);
    CHECK (grid.isVisible());
    CHECK (patternBank.getButtonText() == "M");
    CHECK (patternBank.getBounds() == patternBankBounds);
    patternBank.onClick();
    phoneButton (editor, "Choose pattern 16").onClick();
    CHECK (phoneButton (editor, "Pattern bank").getButtonText() == "16");
    phoneButton (editor, "Pattern bank").onClick();
    phoneButton (editor, "Choose pattern 2").onClick();
    CHECK (phoneButton (editor, "Pattern bank").getButtonText() == "2");
    CHECK (editor.controller.source() == 1);
    CHECK (grid.isVisible());
    editor.controller.selectLatched = true;
    phoneButton (editor, "Pattern bank").onClick();
    phoneButton (editor, "Choose pattern 1").onClick();
    CHECK (editor.controller.source() == 0);
    CHECK (editor.controller.copySource == -1);
    CHECK (grid.isVisible());
    phoneButton (editor, "Pattern bank").onClick();
    for (auto* child : bank->getChildren())
        if (child->getTitle() == "Duplicate pattern" || child->getTitle() == "Close pattern picker")
            CHECK_FALSE (child->isVisible());
    juce::Component& sourceTile = phoneButton (editor, "Choose pattern 1");
    const auto tileCentre = sourceTile.getLocalBounds().getCentre().toFloat();
    sourceTile.mouseDown (phoneEvent (sourceTile, tileCentre, tileCentre));
    const auto now = juce::Time::getCurrentTime();
    sourceTile.mouseUp ({ juce::Desktop::getInstance().getMainMouseSource(), tileCentre, juce::ModifierKeys(),
                          1, 0, 0, 0, 0, &sourceTile, &sourceTile, now, tileCentre, now - juce::RelativeTime::milliseconds (600), 1, false });
    REQUIRE (phoneButton (editor, "Cancel copy").isVisible());
    CHECK (patternBank.getToggleState());
    CHECK_FALSE (phoneButton (editor, "Duplicate pattern").isEnabled());
    phoneButton (editor, "Choose pattern 3").onClick();
    REQUIRE (phoneButton (editor, "Copy to P3").isEnabled());
    phoneButton (editor, "Copy to P3").onClick();
    CHECK (editor.controller.source() == 2);
    CHECK (editor.controller.state.playback.sources[2] == editor.controller.state.playback.sources[0]);
    patternBank.onClick();
    patternBank.onClick();
    CHECK_FALSE (patternBank.getToggleState());
    CHECK (grid.isVisible());

    editor.setCompactViewport (false, 1338, 900);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    CHECK (phoneButton (editor, "NEW").isVisible());
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Open controls" || child->getTitle() == "Pattern picker")
            CHECK_FALSE (child->isVisible());
    CHECK (grid.layout.gridLeft() == Catch::Approx (Layout {}.gridLeft()));
}

TEST_CASE ("iPhone Jam opens the pads and exposes a larger target to finish the take", "[native-ui][iphone][jam]")
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 390);
    editor.setSize (814, 390);
    auto& generate = phoneButton (editor, "Generate");
    auto& jam = phoneButton (editor, "Jam");
    auto& pads = phoneButton (editor, "Show pads");
    const auto waitPulse = [&jam] { return static_cast<float> (jam.getProperties()["jamWaitPulse"]); };
    CHECK (jam.isVisible());
    CHECK (jam.getButtonText().isEmpty());
    CHECK (jam.getX() > generate.getRight());
    CHECK (pads.getX() > jam.getRight());
    processor.setStandaloneTransportPlaying (true);
    jam.onClick();
    CHECK (jam.getTitle() == "Jam waiting for first hit");
    const float initialPulse = waitPulse();
    CHECK (initialPulse > 0.0f);
    juce::Thread::sleep (120);
    editor.sync();
    CHECK (waitPulse() > 0.0f);
    CHECK (std::abs (waitPulse() - initialPulse) > 0.01f);
    CHECK (pads.getToggleState());
    CHECK_FALSE (processor.isStandaloneTransportPlaying());
    jam.onClick();
    CHECK (jam.getTitle() == "Jam");
    CHECK (waitPulse() == Catch::Approx (0.0f));
    CHECK_FALSE (pads.getToggleState());
    CHECK_FALSE (editor.controller.canUndo());

    jam.onClick();
    CHECK (editor.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (jam.getTitle() == "Jam");
    CHECK_FALSE (pads.getToggleState());
    CHECK_FALSE (editor.controller.canUndo());

    pads.setToggleState (true, juce::sendNotificationSync);
    auto& velocity = phoneButton (editor, "Pad velocity mode");
    auto* strip = velocity.getParentComponent();
    const auto& controls = strip->getChildren();
    const auto finishControl = std::find_if (controls.begin(), controls.end(), [] (auto* child) { return child->getTitle() == "Finish Jam"; });
    REQUIRE (finishControl != controls.end());
    auto* finish = dynamic_cast<RowTriggerButton*> (*finishControl);
    REQUIRE (finish != nullptr);
    const auto performanceBounds = [&]
    {
        std::array<juce::Rectangle<int>, 9> bounds;
        for (int ch = 0; ch < 8; ++ch)
        {
            const auto& pad = phoneButton (editor, "Trigger channel " + juce::String (ch + 1));
            bounds[static_cast<size_t> (ch)] = editor.getLocalArea (&pad, pad.getLocalBounds());
        }
        bounds.back() = editor.getLocalArea (&velocity, velocity.getLocalBounds());
        return bounds;
    };
    const auto idleBounds = performanceBounds();
    CHECK_FALSE (finish->isVisible());
    jam.onClick();
    CHECK_FALSE (finish->isVisible());
    CHECK (performanceBounds() == idleBounds);
    const auto& upperPad = phoneButton (editor, "Trigger channel 8");
    const auto& lowerPad = phoneButton (editor, "Trigger channel 4");
    REQUIRE (editor.keyPressed (juce::KeyPress ('a', {}, 'a')));
    editor.sync();
    REQUIRE (finish->isVisible());
    CHECK (waitPulse() == Catch::Approx (0.0f));
    CHECK (jam.getTitle() == "Jam recording");
    CHECK (performanceBounds() == idleBounds);
    CHECK (velocity.getY() == upperPad.getY());
    CHECK (velocity.getHeight() == upperPad.getHeight());
    CHECK (finish->getY() == lowerPad.getY());
    CHECK (finish->getHeight() == lowerPad.getHeight());
    CHECK (finish->getX() == velocity.getX());
    CHECK (finish->getWidth() == velocity.getWidth());
    CHECK (velocity.getWidth() > 56);
    const auto artworkWidth = [] (juce::TextButton& button)
    {
        const auto image = button.createComponentSnapshot (button.getLocalBounds());
        int width = 0;
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt (x, image.getHeight() / 2).getAlpha() > 128)
                ++width;
        return width;
    };
    const int velocityWidth = artworkWidth (velocity), stopWidth = artworkWidth (*finish);
    CHECK (velocityWidth <= stopWidth);
    CHECK (velocityWidth >= stopWidth - 2);
    for (auto* button : std::array<juce::TextButton*, 2> { &velocity, finish })
    {
        CHECK (button->getX() > upperPad.getRight());
        CHECK (strip->getLocalBounds().contains (button->getBounds()));
        CHECK (strip->getComponentAt (button->getBounds().reduced (1).getTopLeft()) == button);
        CHECK (strip->getComponentAt (button->getBounds().getBottomRight().translated (-2, -2)) == button);
    }
    CHECK (jam.isVisible());
    capturePhone (editor, "iphone-jam-stop-pad");
    juce::Thread::sleep (850);
    finish->onClick();
    CHECK_FALSE (finish->isVisible());
    CHECK (performanceBounds() == idleBounds);
    CHECK (processor.isStandaloneTransportPlaying());
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
}

TEST_CASE ("Jam closes Generate and keeps it unavailable until the take ends", "[native-ui][jam]")
{
    const bool compact = GENERATE (false, true);
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    NativeEditor editor (processor);
    if (compact)
        editor.setCompactViewport (true, 814, 390);
    editor.setSize (compact ? 814 : Layout::fullWidth, compact ? 390 : Layout::fullHeight);

    auto& generate = phoneButton (editor, "Generate");
    auto& jam = phoneButton (editor, "Jam");
    generate.onClick();
    REQUIRE (generate.getToggleState());
    jam.onClick();
    REQUIRE (jam.getToggleState());
    CHECK_FALSE (generate.getToggleState());
    CHECK_FALSE (generate.isEnabled());

    REQUIRE (editor.keyPressed (juce::KeyPress ('a', {}, 'a')));
    editor.sync();
    REQUIRE (jam.getTitle() == "Jam recording");
    generate.onClick();
    CHECK (jam.getToggleState());
    CHECK_FALSE (generate.getToggleState());
    CHECK_FALSE (generate.isEnabled());

    jam.onClick();
    CHECK_FALSE (jam.getToggleState());
    REQUIRE (generate.isEnabled());
    generate.onClick();
    CHECK (generate.getToggleState());
}

TEST_CASE ("Jam, Pads and Arm Record settle on the Jam stop press", "[native-ui][jam][render]")
{
    const bool compact = GENERATE (false, true);
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    NativeEditor editor (processor);
    if (compact)
        editor.setCompactViewport (true, 814, 390);
    editor.setSize (compact ? 814 : Layout::fullWidth, compact ? 390 : Layout::fullHeight);
    editor.sync();

    auto& jam = phoneButton (editor, "Jam");
    auto& pads = phoneButton (editor, "Show pads");
    auto& record = phoneButton (editor, "Arm Record");
    jam.onClick();
    REQUIRE (jam.getToggleState());
    REQUIRE (pads.getToggleState());
    REQUIRE_FALSE (record.isEnabled());
    auto* strip = phoneButton (editor, "Pad velocity mode").getParentComponent();

    juce::Component& jamTarget = jam;
    const auto point = jam.getLocalBounds().toFloat().getCentre();
    jamTarget.mouseDown (phoneEvent (jam, point, point));
    REQUIRE (jam.isDown());
    CHECK_FALSE (jam.getToggleState());
    CHECK_FALSE (pads.getToggleState());
    CHECK_FALSE (strip->isVisible());
    CHECK (record.isEnabled());
    CHECK_FALSE (record.getToggleState());
    const auto held = jam.createComponentSnapshot (jam.getLocalBounds());

    jamTarget.mouseUp (phoneEvent (jam, point, point, 0));
    jam.setState (juce::Button::buttonOver); // Compare with the pointer still over the button.
    const auto released = jam.createComponentSnapshot (jam.getLocalBounds());
    bool unchanged = true;
    for (int y = 0; y < held.getHeight() && unchanged; ++y)
        for (int x = 0; x < held.getWidth() && unchanged; ++x)
            unchanged = held.getPixelAt (x, y) == released.getPixelAt (x, y);
    CHECK (unchanged);
}

TEST_CASE ("The desktop and iPad footer pad finishes Jam on press", "[native-ui][jam]")
{
    const bool touch = GENERATE (false, true);
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    processor.showStandaloneMidiOutputMenu = [] (juce::Component&) {};
    processor.showStandaloneLinkSettings = [] (juce::Component&) {};
    NativeEditor editor (processor);
    editor.layout.touchRows = touch;
    editor.setSize (Layout::fullWidth, Layout::fullHeight);
    editor.sync();

    juce::Component* help = nullptr;
    RowTriggerButton* finish = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (child->getTitle() == "Control help")
            help = child;
        if (child->getTitle() == "Finish Jam")
            finish = dynamic_cast<RowTriggerButton*> (child);
    }
    REQUIRE (help != nullptr);
    REQUIRE (finish != nullptr);
    const auto footer = help->getBounds();
    const auto& settings = phoneButton (editor, "Settings");
    CHECK (finish->getButtonText() == "STOP");
    CHECK (help->isVisible());
    CHECK_FALSE (finish->isVisible());
    auto& jam = phoneButton (editor, "Jam");
    jam.onClick();
    CHECK_FALSE (finish->isVisible());
    CHECK (static_cast<float> (jam.getProperties()["jamWaitPulse"]) > 0.0f);
    REQUIRE (editor.keyPressed (juce::KeyPress ('a', {}, 'a')));
    editor.sync();
    REQUIRE (finish->isVisible());
    CHECK (static_cast<float> (jam.getProperties()["jamWaitPulse"]) == Catch::Approx (0.0f));
    CHECK_FALSE (help->isVisible());
    CHECK (finish->getBounds() == footer);
    CHECK (finish->getRight() < settings.getX());
    CHECK (phoneButton (editor, "Ableton Link settings").isVisible());
    CHECK (phoneButton (editor, "MIDI Out").isVisible());
    capturePhone (editor, touch ? "ipad-jam-footer" : "desktop-jam-footer");
    juce::Thread::sleep (850);

    const auto point = finish->getLocalBounds().toFloat().getCentre();
    finish->mouseDown (phoneEvent (*finish, point, point));
    CHECK_FALSE (finish->isVisible());
    CHECK (help->isVisible());
    CHECK (processor.isStandaloneTransportPlaying());
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
}

TEST_CASE ("Jam shortcuts finish capture and stop loop playback", "[native-ui][jam]")
{
    using juce::KeyPress;
    const auto [finishKey, stopKey] = GENERATE (std::pair (KeyPress::spaceKey, KeyPress::escapeKey),
                                              std::pair (KeyPress::returnKey, KeyPress::spaceKey),
                                              std::pair (KeyPress::escapeKey, KeyPress::stopKey),
                                              std::pair (KeyPress::stopKey, KeyPress::escapeKey));
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    NativeEditor editor (processor);
    auto& jam = phoneButton (editor, "Jam");
    auto& pads = phoneButton (editor, "Show pads");
    jam.onClick();
    REQUIRE (editor.keyPressed (juce::KeyPress ('a', {}, 'a')));
    editor.sync();
    REQUIRE (jam.getTitle() == "Jam recording");
    juce::Thread::sleep (850); // A one-bar take at the maximum tempo needs at least 800 ms.

    REQUIRE (editor.keyPressed (KeyPress (finishKey)));
    CHECK (jam.getTitle() == "Jam");
    CHECK_FALSE (pads.getToggleState());
    CHECK (processor.isStandaloneTransportPlaying());
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    REQUIRE (editor.keyPressed (KeyPress (stopKey)));
    CHECK_FALSE (processor.isStandaloneTransportPlaying());
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    REQUIRE (editor.controller.canUndo());
    editor.controller.undo();
    CHECK_FALSE (editor.controller.cell ({ 0, 0 }).enabled);
}

TEST_CASE ("Jam shortcuts take precedence over focused controls", "[native-ui][jam]")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        SKIP ("Keyboard focus requires a display.");

    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    NativeEditor editor (processor);
    editor.setSize (814, 390);
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    editor.setVisible (true);
    auto& jam = phoneButton (editor, "Jam");
    juce::TextEditor field;
    editor.addAndMakeVisible (field);
    field.setBounds (20, 20, 120, 30);
    bool submitted = false;
    bool escaped = false;
    field.onReturnKey = [&] { submitted = true; };
    field.onEscapeKey = [&] { escaped = true; };

    jam.onClick();
    REQUIRE (editor.keyPressed (juce::KeyPress ('a', {}, 'a')));
    field.grabKeyboardFocus();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (850);
    REQUIRE (field.hasKeyboardFocus (false));
    CHECK (field.isReadOnly());
    REQUIRE (editor.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::returnKey)));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
    CHECK (jam.getTitle() == "Jam");
    CHECK_FALSE (submitted);
    CHECK_FALSE (field.isReadOnly());
    REQUIRE (processor.isStandaloneTransportPlaying());
    REQUIRE (editor.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (processor.isStandaloneTransportPlaying());
    CHECK_FALSE (escaped);
}

TEST_CASE ("Desktop and iPad standalone share a tempo popup", "[native-ui][tempo][touch]")
{
    const std::string_view viewport = GENERATE ("desktop", "ipad", "compact-ipad");
    const bool desktop = viewport == "desktop";
    const bool compact = viewport == "compact-ipad";
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    NativeEditor editor (processor);
    editor.layout.touchRows = !desktop;
    if (compact)
        editor.setCompactViewport (true, 1024, 480, false);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    editor.sync();

    auto& button = phoneButton (editor, "Set tempo");
    CHECK (button.getButtonText() == "120 BPM");
    button.onClick();
    auto& plus = phoneButton (editor, "Increase tempo by 1 BPM");
    auto& minus = phoneButton (editor, "Decrease tempo by 1 BPM");
    auto& tap = phoneButton (editor, "Tap Tempo");
    auto* popup = plus.getParentComponent();
    REQUIRE (popup != nullptr);
    CHECK (popup->isVisible());
    CHECK (plus.getWidth() >= 44);
    CHECK (plus.getWidth() == plus.getHeight());
    CHECK (minus.getWidth() == plus.getWidth());
    CHECK (minus.getHeight() == plus.getHeight());
    CHECK (tap.getHeight() == plus.getHeight());
    juce::Slider* fader = nullptr;
    for (auto* child : popup->getChildren())
        if (child->getTitle() == "Tempo BPM fader")
            fader = dynamic_cast<juce::Slider*> (child);
    REQUIRE (fader != nullptr);
    CHECK (fader->getSliderSnapsToMousePosition());
    fader->setValue (299, juce::sendNotificationSync);
    plus.onClick();
    CHECK (processor.getStandaloneTempoBpm() == 300.0);
    CHECK_FALSE (plus.isEnabled());
    minus.onClick();
    CHECK (processor.getStandaloneTempoBpm() == 299.0);
    CHECK (plus.isEnabled());
    CHECK (popup->isVisible());
    if (desktop)
    {
        const auto hit = [&]
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            tap.onClick();
            return now;
        };
        const auto first = hit();
        juce::Thread::sleep (320);
        hit();
        CHECK (processor.getStandaloneTempoBpm() == 299.0);
        juce::Thread::sleep (320);
        const auto third = hit();
        CHECK (processor.getStandaloneTempoBpm() == Catch::Approx (120000.0 / (third - first)).margin (2.0));
        juce::Thread::sleep (560);
        const auto fourth = hit();
        CHECK (processor.getStandaloneTempoBpm() == Catch::Approx (180000.0 / (fourth - first)).margin (2.0));
        juce::Thread::sleep (3050);
        const double beforeNewTaps = processor.getStandaloneTempoBpm();
        hit();
        juce::Thread::sleep (300);
        hit();
        CHECK (processor.getStandaloneTempoBpm() == Catch::Approx (beforeNewTaps));
        fader->setValue (299, juce::sendNotificationSync);
        juce::Thread::sleep (300);
        hit();
        CHECK (processor.getStandaloneTempoBpm() == 299.0);
    }
    if (!compact)
        capturePhone (editor, desktop ? "desktop-tempo" : "ipad-tempo");
    popup->mouseDown (phoneEvent (*popup, { 0, 0 }, { 0, 0 }));
    CHECK_FALSE (popup->isVisible());
    CHECK (button.getButtonText() == "299 BPM");
    const auto centre = button.getLocalBounds().toFloat().getCentre();
    const auto lower = centre.translated (0, 40);
    const auto upper = centre.translated (0, -20);
    auto& scrub = dynamic_cast<GestureButton&> (button);
    scrub.mouseDown (phoneEvent (scrub, centre, centre));
    scrub.mouseDrag (phoneEvent (scrub, lower, centre));
    CHECK (processor.getStandaloneTempoBpm() == 279.0);
    scrub.mouseDrag (phoneEvent (scrub, upper, centre));
    scrub.mouseUp (phoneEvent (scrub, upper, centre, 0));
    CHECK (processor.getStandaloneTempoBpm() == 300.0);
    CHECK_FALSE (popup->isVisible());
    scrub.mouseDown (phoneEvent (scrub, centre, centre));
    scrub.mouseUp (phoneEvent (scrub, centre, centre, 0));
    CHECK (popup->isVisible());
}

TEST_CASE ("iPhone Prob keeps cycle editing in the inspector", "[native-ui][iphone]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 90, 50, 4, 5, 1 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 390);
    editor.setSize (814, 390);
    auto& chance = phoneButton (editor, "Prob editing layer");
    CHECK (chance.getButtonText() == "Prob");
    chance.onClick();
    CHECK (chance.getProperties()["shortcutHint"].toString().isEmpty());
    CHECK (phoneButton (editor, "Select modifier").getProperties()["shortcutHint"].toString().isEmpty());
    auto& grid = phoneGrid (editor);
    const auto bounds = grid.cellBounds ({ 0, 2 });
    const auto point = juce::Point<float> (bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.9f);
    grid.mouseDown (phoneEvent (grid, point, point, juce::ModifierKeys::rightButtonModifier));
    grid.mouseUp (phoneEvent (grid, point, point, 0));
    grid.mouseDown (phoneEvent (grid, point, point));
    grid.mouseUp (phoneEvent (grid, point, point, 0));
    CHECK (editor.controller.selection() == std::vector<Location> { { 0, 2 } });
    CHECK_FALSE (editor.controller.canUndo());
    CHECK_FALSE (grid.hasActiveGesture());
    bool inspectorEnabled = false;
    for (auto* child : editor.getChildren())
        if (auto* inspector = dynamic_cast<PreviewPanel*> (child); inspector != nullptr && inspector->isVisible())
            for (auto* control : inspector->getChildren())
                if (auto* cycle = dynamic_cast<CycleEditor*> (control); cycle != nullptr && cycle->isVisible())
                    inspectorEnabled = cycle->isEnabled();
    CHECK (inspectorEnabled);
    for (auto* child : grid.getChildren())
        if (child->getTitle() == "Step presets")
            CHECK_FALSE (child->isVisible());
}

TEST_CASE ("iPhone reaches triplet presets by holding a step across page boundaries", "[native-ui][iphone][triplets]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, 32 }));
    REQUIRE (processor.dispatchUiEngineCommand ("channel_loop_length", { 1, 32, 1 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 15, 1, 90 }));
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    editor.setCompactViewport (true, 814, 390);
    editor.setSize (814, 390);
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    auto* peer = editor.getPeer();
    REQUIRE (peer != nullptr);
    peer->handleMouseEvent (juce::MouseInputSource::pen, {}, {}, 0.0f, 0.0f, juce::Time::getCurrentTime().toMilliseconds());
    const auto& sources = juce::Desktop::getInstance().getMouseSources();
    const auto source = std::find_if (sources.begin(), sources.end(), [] (const auto& pointer) { return pointer.isPen(); });
    REQUIRE (source != sources.end());
    const auto pointer = *source;
    auto& grid = phoneGrid (editor);
    const Location step { 0, 14 };
    constexpr int pressed = juce::ModifierKeys::leftButtonModifier;
    const auto hold = [&] (Location location)
    {
        const auto point = grid.layout.cell (location).getCentre();
        const auto start = juce::Time::getCurrentTime();
        const auto event = [&] (int modifiers)
        {
            return juce::MouseEvent (pointer, point, juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &grid, &grid,
                                     juce::Time::getCurrentTime(), point, start, 1, false);
        };
        grid.mouseDown (event (pressed));
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2500.0;
        while (grid.openPresetMenu() == nullptr && juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            juce::Thread::sleep (10);
            juce::Timer::callPendingTimersSynchronously();
        }
        REQUIRE (grid.openPresetMenu() != nullptr);
        grid.mouseUp (event (0));
    };
    hold (step);
    REQUIRE (phoneButton (editor, "Quarter triplet").isEnabled());
    editor.sync();
    REQUIRE (phoneButton (editor, "Quarter triplet").isEnabled());
    phoneButton (editor, "Quarter triplet").onClick();
    CHECK (grid.openPresetMenu() != nullptr);
    REQUIRE (editor.controller.cell (step).span == 8);
    CHECK (editor.controller.cell (step).roll == 3);
    phoneButton (editor, "Switch step page").onClick();
    REQUIRE (grid.layout.firstStep == 16);
    editor.controller.selected.reset();
    editor.sync();
    hold ({ 0, 17 });
    CHECK (phoneButton (editor, "Quarter triplet").getToggleState());
    CHECK (phoneButton (editor, "Quarter triplet").getHeight() == 52);
    REQUIRE (grid.openPresetMenu() != nullptr);
    capturePhone (editor, "iphone-step-presets");
    CHECK (grid.openPresetMenu()->getNumChildComponents() == 6);
    editor.controller.selected.set (static_cast<size_t> (step.key()));
    editor.sync();
    ValueControl* span = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* preview = dynamic_cast<PreviewPanel*> (child))
            for (auto* control : preview->getChildren())
                if (control->getTitle() == "Selected span")
                    span = dynamic_cast<ValueControl*> (control);
    REQUIRE (span != nullptr);
    CHECK_FALSE (grid.handleCellPopupMouseDown (span));
    CHECK (grid.openPresetMenu() == nullptr);
    CHECK (span->value == 8);
    const auto origin = span->valueFaceBounds().getCentre();
    span->mouseDown (phoneEvent (*span, origin, origin));
    span->mouseUp (phoneEvent (*span, origin, origin, 0));
    CHECK (editor.controller.cell (step).span == 8);

    const auto target = origin.translated (-24.0f, 0.0f);
    span->mouseDown (phoneEvent (*span, origin, origin));
    span->mouseDrag (phoneEvent (*span, target, origin));
    CHECK (span->value == 7);
    CHECK (editor.controller.cell (step).span == 7);
    span->mouseUp (phoneEvent (*span, target, origin, 0));
    CHECK (editor.controller.cell (step).span == 7);
    CHECK (editor.controller.cell (step).roll == 3);
    editor.controller.undo();
    CHECK (editor.controller.cell (step).span == 8);
}

TEST_CASE ("A second pointer cannot change or end an active grid edit", "[native-ui][iphone][gesture]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 390);
    editor.setSize (814, 390);
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    auto* peer = editor.getPeer();
    REQUIRE (peer != nullptr);
    const auto now = juce::Time::getCurrentTime();
    peer->handleMouseEvent (juce::MouseInputSource::pen, {}, {}, 0.0f, 0.0f, now.toMilliseconds());
    const auto& sources = juce::Desktop::getInstance().getMouseSources();
    const auto pen = std::find_if (sources.begin(), sources.end(), [] (const auto& pointer) { return pointer.isPen(); });
    REQUIRE (pen != sources.end());
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource();
    auto& grid = phoneGrid (editor);
    const auto start = grid.layout.cell ({ 0, 2 }).getCentre();
    const auto middle = grid.layout.cell ({ 0, 4 }).getCentre();
    const auto end = grid.layout.cell ({ 0, 6 }).getCentre();
    const auto otherCell = grid.layout.cell ({ 1, 3 }).getCentre();
    const auto secondPoint = GENERATE_COPY (juce::Point<float> (0, 0), otherCell);
    constexpr int pressed = juce::ModifierKeys::leftButtonModifier;
    const auto event = [&] (juce::MouseInputSource pointer, juce::Point<float> point, juce::Point<float> origin, int mods)
    {
        return juce::MouseEvent (pointer, point, juce::ModifierKeys (mods), 1, 0, 0, 0, 0,
                                 &grid, &grid, now, origin, now, 1, point != origin);
    };

    grid.mouseDown (event (*pen, start, start, pressed));
    grid.mouseDrag (event (*pen, middle, start, pressed));
    REQUIRE (editor.controller.editing());
    grid.mouseDown (event (mouse, secondPoint, secondPoint, pressed));
    grid.mouseDrag (event (mouse, otherCell, secondPoint, pressed));
    grid.mouseUp (event (mouse, otherCell, secondPoint, 0));
    REQUIRE (editor.controller.editing());
    grid.mouseDrag (event (*pen, end, start, pressed));
    grid.mouseUp (event (*pen, end, start, 0));

    for (int step = 2; step <= 6; ++step)
        CHECK (editor.controller.cell ({ 0, step }).enabled);
    CHECK_FALSE (editor.controller.cell ({ 1, 3 }).enabled);
    CHECK_FALSE (editor.controller.editing());
    editor.controller.undo();
    for (int step = 2; step <= 6; ++step)
        CHECK_FALSE (editor.controller.cell ({ 0, step }).enabled);

    grid.mouseDown (event (mouse, otherCell, otherCell, pressed));
    grid.mouseUp (event (mouse, otherCell, otherCell, 0));
    CHECK (editor.controller.cell ({ 1, 3 }).enabled);
}

TEST_CASE ("Touch on an empty step draws immediately and a swipe paints across the next step", "[native-ui][touch]")
{
    const int viewport = GENERATE (0, 1, 2);
    PluginProcessor processor;
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    if (viewport != 0)
        editor.setCompactViewport (true, 814, 390, viewport == 2);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    editor.sync();
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    auto* peer = editor.getPeer();
    REQUIRE (peer != nullptr);
    const auto time = juce::Time::getCurrentTime();
    peer->handleMouseEvent (juce::MouseInputSource::pen, {}, {}, 0.0f, 0.0f, time.toMilliseconds());
    const auto& sources = juce::Desktop::getInstance().getMouseSources();
    const auto pointer = std::find_if (sources.begin(), sources.end(), [] (const auto& source) { return source.isPen(); });
    REQUIRE (pointer != sources.end());

    auto& grid = phoneGrid (editor);
    const Location first { 0, 3 }, second { 0, 4 };
    const auto cell = grid.layout.cell (first);
    const auto centre = cell.getCentre();
    const auto event = [&] (juce::Point<float> position, juce::Point<float> origin, int modifiers)
    {
        return juce::MouseEvent (*pointer, position, juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &grid, &grid,
                                 juce::Time::getCurrentTime(), origin, time, 1, position != origin);
    };
    const auto pixel = [&]
    {
        return grid.createComponentSnapshot ({ juce::roundToInt (centre.x), juce::roundToInt (centre.y), 1, 1 }).getPixelAt (0, 0);
    };
    constexpr int pressed = juce::ModifierKeys::leftButtonModifier;
    const auto empty = pixel();
    grid.mouseDown (event (centre, centre, pressed));
    const auto preview = pixel();
    CHECK (preview != empty);
    CHECK_FALSE (editor.controller.cell (first).enabled);
    grid.cancelGesture();
    CHECK (pixel() == empty);
    grid.mouseUp (event (centre, centre, 0));
    CHECK_FALSE (editor.controller.cell (first).enabled);

    grid.mouseDown (event (centre, centre, pressed));
    grid.mouseUp (event (centre, centre, 0));
    CHECK (editor.controller.cell (first).enabled);
    CHECK (pixel() == preview);
    editor.controller.undo();
    CHECK_FALSE (editor.controller.cell (first).enabled);

    const juce::Point<float> origin { cell.getRight() - 3.0f, centre.y };
    const auto next = origin.translated (6.0f, 0.0f);
    grid.mouseDown (event (origin, origin, pressed));
    grid.mouseDrag (event (next, origin, pressed));
    CHECK (editor.controller.cell (first).enabled);
    CHECK (editor.controller.cell (second).enabled);
    grid.mouseUp (event (next, origin, 0));
    editor.controller.undo();
    CHECK_FALSE (editor.controller.cell (first).enabled);
    CHECK_FALSE (editor.controller.cell (second).enabled);
}

TEST_CASE ("Holding an empty cell opens the rhythm menu and sliding releases onto a preset", "[native-ui][step-presets][touch]")
{
    const bool phone = GENERATE (false, true);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("channel_cc", { 1, 1, 74, 4, 0 }));
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    if (phone)
        editor.setCompactViewport (true, 814, 390);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    editor.sync();
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    auto* peer = editor.getPeer();
    REQUIRE (peer != nullptr);

    // macOS JUCE has no touch sources. Pen events use the same non-mouse gesture path.
    const auto time = juce::Time::getCurrentTime();
    peer->handleMouseEvent (juce::MouseInputSource::pen, {}, {}, 0.0f, 0.0f, time.toMilliseconds());
    const auto& sources = juce::Desktop::getInstance().getMouseSources();
    const auto source = std::find_if (sources.begin(), sources.end(), [] (const auto& pointer) { return pointer.isPen(); });
    REQUIRE (source != sources.end());
    const auto pointer = *source;

    auto& grid = phoneGrid (editor);
    const Location cell { 0, 3 };
    const auto origin = grid.cellBounds (cell).getCentre();
    const auto event = [&] (juce::Point<float> position, int modifiers)
    {
        return juce::MouseEvent (pointer, position, juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &grid, &grid,
                                 juce::Time::getCurrentTime(), origin, time, 1, position != origin);
    };
    const int pressed = juce::ModifierKeys::leftButtonModifier;
    const auto beganAt = juce::Time::getMillisecondCounterHiRes();
    grid.mouseDown (event (origin, pressed));
    CHECK (grid.openPresetMenu() == nullptr);
    const auto deadline = beganAt + (phone ? 2000.0 : 1250.0);
    while (grid.openPresetMenu() == nullptr && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        juce::Thread::sleep (10);
        juce::Timer::callPendingTimersSynchronously();
    }
    REQUIRE (grid.openPresetMenu() != nullptr);
    if (phone)
        CHECK (juce::Time::getMillisecondCounterHiRes() - beganAt >= 1500.0);
    CHECK_FALSE (editor.controller.cell (cell).enabled);
    auto& preset = phoneButton (editor, "8th triplet");
    CHECK (preset.getHeight() == (phone ? 52 : Layout::touchTarget));
    if (!phone)
        capturePhone (editor, "ipad-step-presets");
    const auto target = grid.getLocalPoint (&preset, preset.getLocalBounds().getCentre().toFloat());
    grid.mouseDrag (event (target, pressed));
    CHECK (grid.openPresetMenu() != nullptr);
    CHECK_FALSE (editor.controller.cell (cell).enabled);
    grid.mouseUp (event (target, 0));
    auto* menu = grid.openPresetMenu();
    REQUIRE (menu != nullptr);
    CHECK (editor.controller.cell (cell).enabled);
    CHECK (editor.controller.cell (cell).span == 4);
    CHECK (editor.controller.cell (cell).roll == 3);
    CHECK (preset.getToggleState());
    menu->mouseExit (phoneEvent (*menu, { -5, 0 }, {}, 0));
    CHECK (grid.openPresetMenu() == menu);
    const Location outside { 0, 12 };
    const auto outsidePoint = grid.cellBounds (outside).getCentre();
    grid.mouseDown (event (outsidePoint, pressed));
    CHECK (grid.openPresetMenu() == nullptr);
    grid.mouseUp (event (outsidePoint, 0));
    CHECK_FALSE (editor.controller.cell (outside).enabled);
    editor.controller.undo();
    CHECK_FALSE (editor.controller.cell (cell).enabled);

    editor.controller.begin ("Seed CC steps");
    editor.controller.editCell (cell, Cell { .enabled = true });
    editor.controller.editCell (outside, Cell { .enabled = true });
    editor.controller.commit();
    editor.controller.selected.set (static_cast<size_t> (cell.key()));
    editor.controller.selected.set (static_cast<size_t> (outside.key()));
    editor.controller.notify();
    grid.mouseDown (event (origin, pressed));
    const auto glideDeadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    while (grid.openPresetMenu() == nullptr && juce::Time::getMillisecondCounterHiRes() < glideDeadline)
    {
        juce::Thread::sleep (10);
        juce::Timer::callPendingTimersSynchronously();
    }
    REQUIRE (grid.openPresetMenu() != nullptr);
    juce::Component* glide = nullptr;
    for (auto* child : grid.openPresetMenu()->getChildren())
        if (child->getTitle() == "Glide to next")
            glide = child;
    REQUIRE (glide != nullptr);
    CHECK (glide->getBottom() < preset.getY());
    capturePhone (editor, phone ? "cc-preset-menu-compact" : "cc-preset-menu-tablet");
    const auto glideTarget = grid.getLocalPoint (glide, glide->getLocalBounds().getCentre().toFloat());
    grid.mouseDrag (event (glideTarget, pressed));
    grid.mouseUp (event (glideTarget, 0));
    CHECK (editor.controller.glidesToNext (cell));
    CHECK (editor.controller.glidesToNext (outside));
    editor.controller.undo();
    CHECK_FALSE (editor.controller.glidesToNext (cell));
    CHECK_FALSE (editor.controller.glidesToNext (outside));
}

TEST_CASE ("iPad can scrub velocity and open a roll near the last step's handle", "[native-ui][touch][step-presets]")
{
    const int steps = GENERATE (16, 64);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, steps }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, steps, 1, 90 }));
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    editor.sync();
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    auto* peer = editor.getPeer();
    REQUIRE (peer != nullptr);
    const auto time = juce::Time::getCurrentTime();
    peer->handleMouseEvent (juce::MouseInputSource::pen, {}, {}, 0.0f, 0.0f, time.toMilliseconds());
    const auto& sources = juce::Desktop::getInstance().getMouseSources();
    const auto source = std::find_if (sources.begin(), sources.end(), [] (const auto& pointer) { return pointer.isPen(); });
    REQUIRE (source != sources.end());

    auto& grid = phoneGrid (editor);
    const Location step { 0, steps - 1 };
    const auto cell = grid.cellBounds (step);
    const auto origin = cell.getCentre().withX (cell.getRight() - juce::jmin (17.0f, cell.getWidth() * 0.34f));
    const auto event = [&] (juce::Point<float> position, int modifiers)
    {
        return juce::MouseEvent (*source, position, juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &grid, &grid,
                                 juce::Time::getCurrentTime(), origin, time, 1, position != origin);
    };
    constexpr int pressed = juce::ModifierKeys::leftButtonModifier;
    auto& controller = editor.controller;
    grid.mouseDown (event (origin, pressed));
    CHECK_FALSE (controller.editing());
    const auto raised = origin.translated (0, -25.0f);
    grid.mouseDrag (event (raised, pressed));
    grid.mouseUp (event (raised, 0));
    CHECK (controller.cell (step).velocity > 90);
    CHECK (controller.loop (0).loopLength == steps);

    grid.mouseDown (event (origin, pressed));
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    while (grid.openPresetMenu() == nullptr && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        juce::Thread::sleep (10);
        juce::Timer::callPendingTimersSynchronously();
    }
    REQUIRE (grid.openPresetMenu() != nullptr);
    auto& roll = phoneButton (editor, "3-hit roll");
    const auto target = grid.getLocalPoint (&roll, roll.getLocalBounds().getCentre().toFloat());
    grid.mouseDrag (event (target, pressed));
    grid.mouseUp (event (target, 0));
    CHECK (controller.cell (step).roll == 3);
    CHECK (controller.loop (0).loopLength == steps);
}

TEST_CASE ("iPad velocity anchor hold pulses outside the touch point and stays armed", "[native-ui][touch][velocity-draw]")
{
    const int firstStep = GENERATE (0, 3);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, firstStep + 1, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 7, 1, 90 }));
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    editor.sync();
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    auto* peer = editor.getPeer();
    REQUIRE (peer != nullptr);

    // A pen source exercises the touch gesture path on macOS.
    const auto time = juce::Time::getCurrentTime();
    peer->handleMouseEvent (juce::MouseInputSource::pen, {}, {}, 0.0f, 0.0f, time.toMilliseconds());
    const auto& sources = juce::Desktop::getInstance().getMouseSources();
    const auto source = std::find_if (sources.begin(), sources.end(), [] (const auto& pointer) { return pointer.isPen(); });
    REQUIRE (source != sources.end());

    auto& grid = phoneGrid (editor);
    const auto cell = grid.cellBounds ({ 0, firstStep });
    const auto origin = juce::Point<float> { cell.getX(), cell.getCentreY() };
    const auto event = [&] (juce::Point<float> position, int modifiers)
    {
        return juce::MouseEvent (*source, position, juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &grid, &grid,
                                 juce::Time::getCurrentTime(), origin, time, 1, position != origin);
    };
    constexpr int pressed = juce::ModifierKeys::leftButtonModifier;
    const auto beganAt = juce::Time::getMillisecondCounterHiRes();
    grid.mouseDown (event (origin, pressed));
    CHECK_FALSE (editor.controller.editing());
    const auto area = juce::Rectangle<int> (120, 120).withCentre (origin.roundToInt());
    const auto before = grid.createComponentSnapshot (area);
    while (!editor.controller.editing() && juce::Time::getMillisecondCounterHiRes() < beganAt + 700.0)
    {
        juce::Thread::sleep (10);
        juce::Timer::callPendingTimersSynchronously();
    }
    REQUIRE (editor.controller.editing());
    juce::Thread::sleep (280);
    const auto ripple = grid.createComponentSnapshot (area);
    bool rippleOutsideTouch = false;
    for (int x = 60 + 30; x < 60 + 55; ++x)
        rippleOutsideTouch |= ripple.getPixelAt (x, 60) != before.getPixelAt (x, 60);
    CHECK (rippleOutsideTouch);

    // The rhythm-menu timeout should no longer compete with the armed drag.
    while (juce::Time::getMillisecondCounterHiRes() < beganAt + 1450.0)
    {
        juce::Thread::sleep (10);
        juce::Timer::callPendingTimersSynchronously();
    }
    CHECK (grid.openPresetMenu() == nullptr);
    REQUIRE (editor.controller.editing());

    const auto sameEdge = origin.translated (0, 18.0f);
    const auto markerArea = juce::Rectangle<int> (16, 16).withCentre (sameEdge.roundToInt());
    const auto beforeDrag = grid.createComponentSnapshot (markerArea);
    grid.mouseDrag (event (sameEdge, pressed));
    const auto afterDrag = grid.createComponentSnapshot (markerArea);
    bool secondMarkerAppeared = false;
    for (int y = 0; y < markerArea.getHeight(); ++y)
        for (int x = 0; x < markerArea.getWidth(); ++x)
            secondMarkerAppeared |= beforeDrag.getPixelAt (x, y) != afterDrag.getPixelAt (x, y);
    CHECK_FALSE (secondMarkerAppeared);

    const auto destination = grid.cellBounds ({ 0, 7 }).getTopLeft().translated (0, 2);
    grid.mouseDrag (event (destination, pressed));
    grid.mouseUp (event (destination, 0));
    CHECK (editor.controller.cell ({ 0, firstStep }).velocity == 64);
}

TEST_CASE ("iPad separates the loop grip, velocity scrub and split hold", "[native-ui][touch][velocity-draw]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    const Location first { 0, 0 }, split { 0, 4 };
    const Cell clip { .enabled = true, .velocity = 84, .roll = 3, .span = 2 };
    editor.controller.begin ("Set up steps");
    editor.controller.editCell (first, Cell { .enabled = true, .velocity = 90 });
    editor.controller.editCell (split, clip);
    editor.controller.commit();
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    auto* peer = editor.getPeer();
    REQUIRE (peer != nullptr);
    const auto time = juce::Time::getCurrentTime();
    peer->handleMouseEvent (juce::MouseInputSource::pen, {}, {}, 0.0f, 0.0f, time.toMilliseconds());
    const auto& sources = juce::Desktop::getInstance().getMouseSources();
    const auto source = std::find_if (sources.begin(), sources.end(), [] (const auto& pointer) { return pointer.isPen(); });
    REQUIRE (source != sources.end());

    auto& grid = phoneGrid (editor);
    const auto event = [&] (juce::Point<float> position, juce::Point<float> origin, int modifiers)
    {
        return juce::MouseEvent (*source, position, juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &grid, &grid,
                                 juce::Time::getCurrentTime(), origin, time, 1, position != origin);
    };
    constexpr int pressed = juce::ModifierKeys::leftButtonModifier;
    SECTION ("The grip sits in the gutter and still resizes the loop")
    {
        const auto grip = grid.loopHandleBounds (0, false);
        CHECK (grip.getRight() < grid.cellBounds (first).getX());
        CHECK (grid.rowNoteInput (0).getRight() < grip.getX());
        const auto origin = grip.getCentre();
        const auto end = origin.translated (grid.layout.cellWidth(), 0);
        grid.mouseDown (event (origin, origin, pressed));
        grid.mouseDrag (event (end, origin, pressed));
        grid.mouseUp (event (end, origin, 0));
        CHECK (editor.controller.loop (0).loopStart == 1);
    }
    SECTION ("A quick vertical drag at the first seam scrubs relative velocity")
    {
        const auto cell = grid.cellBounds (first);
        const auto origin = juce::Point<float> { cell.getX() + 1.0f, cell.getCentreY() };
        const auto end = origin.translated (0, -8.0f);
        grid.mouseDown (event (origin, origin, pressed));
        grid.mouseDrag (event (end, origin, pressed));
        grid.mouseUp (event (end, origin, 0));
        CHECK (editor.controller.cell (first).velocity > 90);
        CHECK (editor.controller.cell (split) == clip);
        CHECK (editor.controller.loop (0).loopStart == 0);
    }
    SECTION ("A touch drag from either gutter starts a line without waiting for a hold")
    {
        const bool shortened = GENERATE (false, true);
        const int start = shortened ? 2 : 0;
        const int end = shortened ? 12 : grid.layout.steps;
        editor.controller.command ("channel_loop_length", { 1, end - start, start + 1 });
        editor.controller.begin ("Set up edge steps");
        editor.controller.editCell ({ 0, start }, Cell { .enabled = true, .velocity = 90 });
        editor.controller.editCell ({ 0, end - 1 }, Cell { .enabled = true, .velocity = 90 });
        editor.controller.commit();
        for (const bool closing : { false, true })
        {
            const Location location { 0, closing ? end - 1 : start };
            const auto cell = grid.cellBounds (location);
            const auto origin = juce::Point<float> { closing ? cell.getRight() + 5.0f : cell.getX() - 5.0f, cell.getCentreY() };
            const auto destination = grid.layout.cell ({ 0, closing ? end - 3 : start + 3 }).getTopLeft().translated (0, 2.0f);
            grid.mouseDown (event (origin, origin, pressed));
            grid.mouseDrag (event (destination, origin, pressed));
            grid.mouseUp (event (destination, origin, 0));
            CHECK (editor.controller.cell (location).velocity == 64);
            CHECK (editor.controller.loop (0).loopStart == start);
            CHECK (editor.controller.loop (0).loopLength == end - start);
            editor.controller.undo();
        }
    }
    SECTION ("Holding a split divider selects its clip")
    {
        const auto body = grid.layout.cellArtwork (grid.cellBounds (split));
        const auto origin = juce::Point<float> { body.getX() + body.getWidth() / 3.0f, body.getCentreY() };
        grid.mouseDown (event (origin, origin, pressed));
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 800.0;
        while (editor.controller.selected.none() && juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            juce::Thread::sleep (10);
            juce::Timer::callPendingTimersSynchronously();
        }
        CHECK (editor.controller.selection() == std::vector { split });
        CHECK_FALSE (editor.controller.editing());
        grid.mouseUp (event (origin, origin, 0));
        CHECK (editor.controller.cell (split) == clip);
    }
}

TEST_CASE ("iPhone step pages edit the correct cells and cancel a resized gesture", "[native-ui][iphone]")
{
    PluginProcessor processor;
    const int steps = GENERATE (32, 60, 64);
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, steps }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 409);
    editor.setSize (814, 409);
    auto& grid = phoneGrid (editor);
    auto& page = phoneButton (editor, "Switch step page");
    const auto pageCount = juce::String ((steps + 15) / 16);
    CHECK (page.getButtonText() == "1:" + pageCount);
    for (int start = 16; start < steps; start += 16)
    {
        page.onClick();
        REQUIRE (grid.layout.firstStep == start);
        CHECK (page.getButtonText() == juce::String (start / 16 + 1) + ":" + pageCount);
        CHECK (grid.layout.visibleSteps() == juce::jmin (16, steps - start));
    }
    const int lastPage = grid.layout.firstStep;
    page.onClick();
    REQUIRE (grid.layout.firstStep == 0);
    CHECK (page.getButtonText() == "1:" + pageCount);
    for (int start = 0; start < lastPage; start += 16)
        page.onClick();
    REQUIRE (grid.layout.firstStep == lastPage);
    const Location lastStep { 0, steps - 1 };
    REQUIRE (grid.layout.at (grid.layout.cell (lastStep).getCentre()) == lastStep);
    CHECK_FALSE (grid.layout.at ({ grid.layout.gridLeft() - 1, grid.layout.cell (lastStep).getCentreY() }).valid());
    auto tap = [&] (Location location)
    {
        const auto point = grid.layout.cell (location).getCentre();
        grid.mouseDown (phoneEvent (grid, point, point));
        grid.mouseUp (phoneEvent (grid, point, point));
    };
    tap (lastStep);
    CHECK (editor.controller.cell (lastStep).enabled);
    CHECK_FALSE (editor.controller.cell ({ 0, 0 }).enabled);
    CHECK_FALSE (editor.controller.cell ({ 1, steps - 1 }).enabled);
    editor.controller.undo();
    CHECK_FALSE (editor.controller.cell (lastStep).enabled);
    const auto first = grid.layout.cell ({ 0, lastPage }).getCentre();
    const auto last = grid.layout.cell ({ 0, lastPage + 3 }).getCentre();
    grid.mouseDown (phoneEvent (grid, first, first));
    grid.mouseDrag (phoneEvent (grid, last, first));
    REQUIRE (editor.controller.editing());
    editor.setCompactViewport (true, 850, 409);
    grid.mouseUp (phoneEvent (grid, last, first));
    CHECK (grid.layout.firstStep == lastPage);
    CHECK_FALSE (editor.controller.editing());
    CHECK_FALSE (editor.controller.cell ({ 0, lastPage }).enabled);
    CHECK_FALSE (editor.controller.cell ({ 0, lastPage + 3 }).enabled);
    CHECK_FALSE (editor.controller.canUndo());
    editor.controller.command ("source_steps", { 1, 16 }, "Shorten");
    CHECK (grid.layout.firstStep == 0);
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Switch step page")
            CHECK_FALSE (child->isVisible());
}

TEST_CASE ("Grid arrow keys reveal pages and rows and Return toggles the focused step", "[native-ui][iphone]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, 32 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 409);
    editor.setSize (814, 409);
    editor.setCompactVisibleRows (4);
    editor.layout.reverseRows = true;
    editor.sync();
    auto& grid = phoneGrid (editor);
    const auto point = grid.layout.cell ({ 4, 15 }).getCentre();
    grid.mouseDown (phoneEvent (grid, point, point));
    grid.mouseUp (phoneEvent (grid, point, point, 0));
    REQUIRE (editor.controller.cell ({ 4, 15 }).enabled);
    REQUIRE (grid.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (grid.layout.firstStep == 16);
    REQUIRE (grid.keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (grid.layout.firstRow == 1);
    REQUIRE (grid.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
    CHECK (editor.controller.cell ({ 3, 16 }).enabled);
    REQUIRE (grid.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
    CHECK_FALSE (editor.controller.cell ({ 3, 16 }).enabled);
    CHECK (editor.controller.cell ({ 4, 15 }).enabled);
}

TEST_CASE ("iPhone inspector value edits respect drag speed, undo, and viewport changes", "[native-ui][iphone]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 80, 75, 1, 1, 1 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 409);
    editor.setSize (814, 409);
    editor.controller.selected.set (0);
    editor.sync();
    ValueControl* velocity = nullptr;
    ValueControl* probability = nullptr;
    ValueControl* nudge = nullptr;
    ValueControl* span = nullptr;
    ValueControl* split = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* preview = dynamic_cast<PreviewPanel*> (child))
            for (auto* control : preview->getChildren())
            {
                if (control->getTitle() == "Selected velocity offset")
                    velocity = dynamic_cast<ValueControl*> (control);
                else if (control->getTitle() == "Selected probability offset")
                    probability = dynamic_cast<ValueControl*> (control);
                else if (control->getTitle() == "Selected timing nudge")
                    nudge = dynamic_cast<ValueControl*> (control);
                else if (control->getTitle() == "Selected span")
                    span = dynamic_cast<ValueControl*> (control);
                else if (control->getTitle() == "Selected split")
                    split = dynamic_cast<ValueControl*> (control);
            }
    REQUIRE (velocity != nullptr);
    REQUIRE (probability != nullptr);
    REQUIRE (nudge != nullptr);
    REQUIRE (span != nullptr);
    REQUIRE (split != nullptr);
    CHECK (span->getRight() < split->getX());
    const juce::Point<float> origin { 30, 42 };
    velocity->mouseDown (phoneEvent (*velocity, origin, origin));
    velocity->mouseDrag (phoneEvent (*velocity, origin.translated (0, -44), origin));
    CHECK (editor.controller.cell ({ 0, 0 }).velocity == 90);
    editor.setCompactViewport (true, 850, 409);
    CHECK (editor.controller.cell ({ 0, 0 }).velocity == 80);
    velocity->mouseUp (phoneEvent (*velocity, origin, origin));
    CHECK_FALSE (editor.controller.canUndo());
    velocity->mouseDown (phoneEvent (*velocity, origin, origin));
    velocity->mouseDrag (phoneEvent (*velocity, origin.translated (0, -44), origin));
    velocity->mouseUp (phoneEvent (*velocity, origin.translated (0, -44), origin));
    CHECK (editor.controller.cell ({ 0, 0 }).velocity == 90);
    editor.controller.undo();
    CHECK (editor.controller.cell ({ 0, 0 }).velocity == 80);

    const auto dragRight = [&] (ValueControl& control)
    {
        // Exercise the horizontal gesture with a synthetic desktop mouse source.
        control.horizontalAdjustment = true;
        const auto end = origin.translated (44, 0);
        control.mouseDown (phoneEvent (control, origin, origin));
        control.mouseDrag (phoneEvent (control, end, origin));
        int formatted = 0;
        auto previousFormat = std::exchange (control.format, [&] (int value)
        {
            ++formatted;
            return juce::String (value);
        });
        CHECK_FALSE (control.createComponentSnapshot (control.getLocalBounds()).isNull());
        CHECK (formatted == 0);
        control.format = std::move (previousFormat);
        control.mouseUp (phoneEvent (control, end, origin, 0));
    };
    for (const bool phone : { true, false })
    {
        editor.setCompactViewport (true, 850, 409, phone);
        CHECK (probability->caption == (phone ? "Prob" : "Chance"));
        CHECK (split->alwaysShowHorizontalArrows == phone);
        CHECK (split->horizontalAdjustment == phone);
        if (phone)
            for (const auto* control : { velocity, probability, nudge, span, split })
            {
                const int captionWidth = juce::GlyphArrangement::getStringWidthInt (font (control->captionFontSize, true), control->caption);
                CHECK (control->captionWidth - captionWidth == 7);
                CHECK (control->valueFaceBounds().getWidth() == 42.0f);
                CHECK (control->valueFaceBounds().getHeight() == 42.0f);
            }
        dragRight (*velocity);
        CHECK (editor.controller.cell ({ 0, 0 }).velocity == (phone ? 95 : 90));
        editor.controller.undo();
        dragRight (*probability);
        CHECK (editor.controller.cell ({ 0, 0 }).probability == (phone ? 90 : 85));
        editor.controller.undo();
    }

    editor.setCompactViewport (true, 850, 409);
    split->mouseDown (phoneEvent (*split, origin, origin));
    split->mouseDrag (phoneEvent (*split, origin.translated (0, -80), origin));
    CHECK (editor.controller.cell ({ 0, 0 }).roll == 1);
    for (const auto [distance, count] : { std::pair { 12, 1 }, { 24, 2 }, { 48, 3 }, { 72, 4 }, { 200, 8 }, { 72, 4 } })
    {
        split->mouseDrag (phoneEvent (*split, origin.translated (distance, 0), origin));
        CHECK (editor.controller.cell ({ 0, 0 }).roll == count);
    }
    split->mouseUp (phoneEvent (*split, origin.translated (72, 0), origin, 0));
    CHECK (editor.controller.cell ({ 0, 0 }).roll == 4);
    editor.controller.undo();
    CHECK (editor.controller.cell ({ 0, 0 }).roll == 1);
}

TEST_CASE ("Pencil activates touch scrubbing and long press on numeric controls", "[native-ui][ipad][pen]")
{
    ValueControl control;
    control.setSize (100, 100);
    control.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    control.setVisible (true);
    auto* peer = control.getPeer();
    REQUIRE (peer != nullptr);

    const auto now = juce::Time::getCurrentTime();
    peer->handleMouseEvent (juce::MouseInputSource::pen, {}, {}, 0.0f, 0.0f, now.toMilliseconds());
    const auto& sources = juce::Desktop::getInstance().getMouseSources();
    const auto pen = std::find_if (sources.begin(), sources.end(), [] (const auto& pointer) { return pointer.isPen(); });
    REQUIRE (pen != sources.end());
    const auto pointer = *pen;

    const juce::Point<float> origin { 32, 40 };
    const auto right = origin.translated (20, 0);
    const auto up = origin.translated (0, -20);
    const auto event = [&] (juce::Point<float> position, int modifiers = juce::ModifierKeys::leftButtonModifier)
    {
        return juce::MouseEvent (pointer, position, juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &control, &control,
                                 juce::Time::getCurrentTime(), origin, now, 1, position != origin);
    };

    control.value = 50;
    control.horizontalDragOnTouch = true;
    control.mouseDown (event (origin));
    control.mouseDrag (event (up));
    CHECK (control.value == 50);
    control.mouseDrag (event (right));
    control.mouseUp (event (right, 0));
    CHECK (control.value == 54);

    control.mouseDown (phoneEvent (control, origin, origin));
    control.mouseDrag (phoneEvent (control, right, origin));
    CHECK (control.value == 54);
    control.mouseDrag (phoneEvent (control, up, origin));
    control.mouseUp (phoneEvent (control, up, origin, 0));
    CHECK (control.value == 58);

    int menuOpens = 0;
    control.onContextMenu = [&] { ++menuOpens; };
    control.mouseDown (event (origin));
    CHECK (menuOpens == 0);
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1250.0;
    while (menuOpens == 0 && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        juce::Thread::sleep (10);
        juce::Timer::callPendingTimersSynchronously();
    }
    CHECK (menuOpens == 1);
    control.mouseUp (event (origin, 0));
    CHECK (menuOpens == 1);
}

TEST_CASE ("Compact iPad AUv3 uses the phone layout with iPad touch behavior", "[native-ui][iphone][ipad][auv3][render]")
{
    const bool phone = GENERATE (false, true);
    const bool standalone = GENERATE (false, true);
    juce::AudioProcessor::setTypeOfNextNewPlugin (standalone ? juce::AudioProcessor::wrapperType_Standalone
                                                           : juce::AudioProcessor::wrapperType_AudioUnitv3);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    editor.setCompactViewport (true, 814, 390, phone);
    editor.setSize (814, 390);

    const bool usePhoneLayout = phone || !standalone;
    CHECK (editor.layout.phoneRowControls == usePhoneLayout);
    CHECK (editor.layout.compactOnPhone == phone);
    CHECK (phoneButton (editor, "Pattern bank").getButtonText() == (usePhoneLayout ? "1" : "Pattern 1  v"));
    CHECK (phoneButton (editor, "Select modifier").getTooltip() == (phone ? "Tap to toggle Select" : "Hold for temporary Select"));
    const auto visible = [&editor] (const juce::String& title)
    {
        const auto& children = editor.getChildren();
        return std::any_of (children.begin(), children.end(), [&title] (auto* child) { return child->isVisible() && child->getTitle() == title; });
    };
    CHECK (visible ("Play") == standalone);
    const ValueControl* probability = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* inspector = dynamic_cast<PreviewPanel*> (child))
            for (auto* control : inspector->getChildren())
                if (auto* value = dynamic_cast<ValueControl*> (control); value && value->caption == (usePhoneLayout ? "Prob" : "Chance"))
                    probability = value;
    REQUIRE (probability != nullptr);
    CHECK (probability->horizontalDragMultiplier == Catch::Approx (phone ? 1.5f : 1.0f));

    if (!usePhoneLayout)
    {
        CHECK (phoneButton (editor, "Open controls").getButtonText() == "Settings");
        return;
    }

    if (!phone)
        capturePhone (editor, "ipad-auv3-compact-grid");
    auto& grid = phoneGrid (editor);
    const int headerY = standalone ? 0 : 4;
    CHECK (grid.getY() == Layout::compactHeaderHeight + headerY);
    CHECK (phoneButton (editor, "Pattern bank").getX() == 0);
    CHECK (phoneButton (editor, "Pattern bank").getY() == headerY);
    CHECK (phoneButton (editor, "Pattern bank").getHeight() == Layout::compactHeaderHeight);
    CHECK (phoneButton (editor, "Open menu").getRight() == editor.getWidth());
    if (!standalone)
    {
        const auto& bank = phoneButton (editor, "Pattern bank");
        const auto& velocity = phoneButton (editor, "Velocity editing layer");
        const auto& select = phoneButton (editor, "Select modifier");
        const auto& undo = phoneButton (editor, "undo");
        const auto& redo = phoneButton (editor, "redo");
        const auto& generate = phoneButton (editor, "Generate");
        const auto& pads = phoneButton (editor, "Show pads");
        const auto& record = phoneButton (editor, "Arm Record");
        const auto& pattern = phoneButton (editor, "Open pattern settings");
        const auto& menu = phoneButton (editor, "Open menu");
        const int gap = velocity.getX() - bank.getRight();
        CHECK (gap == 10);
        CHECK (undo.getX() - select.getRight() == gap);
        CHECK (redo.getX() == undo.getRight());
        CHECK (pads.getX() - generate.getRight() == gap);
        CHECK (record.getX() - pads.getRight() == gap);
        CHECK (menu.getX() - pattern.getRight() == gap);
        CHECK (pattern.getX() - record.getRight() == 2 * gap);
        CHECK (generate.getX() > redo.getRight());
    }
    phoneButton (editor, "Open menu").onClick();
    CHECK (phoneButton (editor, "Close menu").getRight() == editor.getWidth());
    CHECK (phoneButton (editor, "Close menu").getY() == headerY);
    CHECK_FALSE (grid.isVisible());
    phoneButton (editor, "Settings").onClick();
    CHECK (visible ("Visible rows") == phone);
    if (!phone)
        capturePhone (editor, "ipad-auv3-compact-settings");
    phoneButton (editor, "Close menu").onClick();
    CHECK (grid.isVisible());
    phoneButton (editor, "Pattern bank").onClick();
    CHECK_FALSE (grid.isVisible());
    phoneButton (editor, "Choose pattern 3").onClick();
    CHECK (editor.controller.source() == 2);
    CHECK (grid.isVisible());

    auto& padsToggle = phoneButton (editor, "Show pads");
    auto& generateToggle = phoneButton (editor, "Generate");
    padsToggle.setToggleState (true, juce::sendNotificationSync);
    const auto& performancePad = phoneButton (editor, "Trigger channel 1");
    REQUIRE (performancePad.isVisible());
    generateToggle.onClick();
    CHECK (generateToggle.getToggleState());
    CHECK_FALSE (padsToggle.getToggleState());
    CHECK_FALSE (performancePad.getParentComponent()->isVisible());
    generateToggle.onClick();
    CHECK (grid.isVisible());
    CHECK_FALSE (performancePad.getParentComponent()->isVisible());

    editor.setCompactViewport (false, 1200, 700, phone);
    CHECK_FALSE (editor.layout.phoneRowControls);
    CHECK_FALSE (editor.layout.compactOnPhone);
    CHECK (editor.layout.settingsInHeader == (!phone && !standalone));
    if (editor.layout.settingsInHeader)
    {
        editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
        const auto& settings = phoneButton (editor, "Settings");
        const auto& mute = phoneButton (editor, "M");
        const PatternButton* lastPattern = nullptr;
        for (auto* child : editor.getChildren())
            if (auto* button = dynamic_cast<PatternButton*> (child); button && button->getButtonText() == "8")
                lastPattern = button;
        REQUIRE (lastPattern != nullptr);
        CHECK (settings.getY() == mute.getY());
        CHECK (settings.getX() == mute.getRight() + Layout::headerRowGap);
        CHECK (lastPattern->getRight() == mute.getX() - Layout::headerRowGap);
        CHECK (settings.getRight() == editor.getWidth() - Layout::outerGutter);
        CHECK (editor.getHeight() == Layout::fullHeight - Layout::footerHeight);
        CHECK (editor.getHeight() == juce::roundToInt (editor.layout.previewY()) + Layout::previewHeight);
        CHECK_FALSE (visible ("Control help"));
        capturePhone (editor, "ipad-auv3-full");

        editor.setCompactViewport (true, 814, 390, phone);
        CHECK_FALSE (editor.layout.settingsInHeader);
        CHECK (phoneButton (editor, "Open menu").getButtonText() == "Menu");
    }
}

TEST_CASE ("iPhone and compact AUv3 can edit a CC row through the note chooser", "[native-ui][iphone][auv3][cc][render]")
{
    const bool phone = GENERATE (true, false);
    juce::AudioProcessor::setTypeOfNextNewPlugin (phone ? juce::AudioProcessor::wrapperType_Standalone
                                                       : juce::AudioProcessor::wrapperType_AudioUnitv3);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    editor.setCompactViewport (true, 814, 390, phone);
    editor.setSize (814, 390);
    auto& grid = phoneGrid (editor);
    const int pitch = editor.controller.state.playback.channels[0].note;
    editor.controller.begin ("Seed notes");
    editor.controller.editCell ({ 0, 0 }, Cell { .enabled = true, .velocity = 81, .roll = 3 });
    editor.controller.editCell ({ 0, 6 }, Cell { .enabled = true, .velocity = 36 });
    editor.controller.commit();

    phoneButton (editor, "Settings channel 1").onClick();
    NoteInput* input = nullptr;
    NoteChooser* chooser = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* choice = dynamic_cast<NoteInput*> (child)) input = choice;
        if (auto* overlay = dynamic_cast<NoteChooser*> (child)) chooser = overlay;
    }
    REQUIRE (input != nullptr);
    REQUIRE (input->isVisible());
    REQUIRE (chooser != nullptr);
    input->onChoose();
    REQUIRE (chooser->isVisible());
    CHECK_FALSE (chooser->isAlwaysOnTop());
    CHECK_FALSE (input->isVisible());
    CHECK_FALSE (grid.isVisible());
    auto& back = phoneButton (editor, "Back from MIDI chooser");
    CHECK (back.getBottom() < chooser->getY());
    auto& noteMode = phoneButton (editor, "Note mode");
    auto& ccMode = phoneButton (editor, "CC mode");
    const auto switchBounds = noteMode.getBounds().getUnion (ccMode.getBounds());
    CHECK (chooser->getLocalBounds().contains (switchBounds));
    CHECK (noteMode.getRight() == ccMode.getX() + 1);
    CHECK (noteMode.getHeight() >= 42);
    CHECK (noteMode.getToggleState());
    capturePhone (editor, phone ? "iphone-note-mode-chooser" : "compact-auv3-note-mode-chooser");
    ccMode.onClick();
    REQUIRE (editor.controller.isControllerRow (0));
    CHECK (ccMode.getToggleState());
    CHECK_FALSE (noteMode.getToggleState());
    CHECK (noteMode.getBounds().getUnion (ccMode.getBounds()) == switchBounds);
    REQUIRE (input->caption == "MIDI CC");
    ValueControl* number = nullptr;
    for (auto* child : chooser->getChildren())
        if (child->getTitle() == "CC number")
            number = dynamic_cast<ValueControl*> (child);
    REQUIRE (number != nullptr);
    REQUIRE (number->isVisible());
    CHECK (number->getY() > switchBounds.getBottom());
    number->createAccessibilityHandler()->getValueInterface()->setValue (74.0);
    CHECK (editor.controller.state.playback.channels[0].ccNumber == 74);
    CHECK_FALSE (editor.controller.glidesToNext ({ 0, 0 }));
    CHECK (std::ranges::none_of (chooser->getChildren(), [] (auto* child) { return child->getTitle().containsIgnoreCase ("Glide"); }));
    capturePhone (editor, phone ? "iphone-cc-chooser" : "compact-auv3-cc-chooser");
    back.onClick();
    CHECK_FALSE (chooser->isVisible());
    CHECK_FALSE (back.isVisible());
    CHECK (input->isVisible());
    phoneButton (editor, "Done").onClick();
    REQUIRE (grid.isVisible());

    editor.controller.selected.set (0);
    editor.controller.notify();
    ValueControl* value = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* panel = dynamic_cast<PreviewPanel*> (child); panel != nullptr && panel->isVisible())
            for (auto* control : panel->getChildren())
                if (control->getTitle() == "Selected CC value")
                    value = dynamic_cast<ValueControl*> (control);
    REQUIRE (value != nullptr);
    CHECK (value->showAdjustmentArrows);
    value->createAccessibilityHandler()->getValueInterface()->setValue (0.0);
    CHECK (editor.controller.cell ({ 0, 0 }).controllerValue() == 0);
    CHECK (editor.controller.cell ({ 0, 0 }).velocity == 81);
    CHECK (editor.controller.cell ({ 0, 0 }).roll == 3);
    editor.controller.selected.set (6);
    editor.controller.notify();
    auto& glide = phoneButton (editor, "Glide to next");
    CHECK (glide.getWidth() == 42);
    CHECK (glide.getHeight() == 42);
    CHECK (glide.getX() > value->getRight());
    CHECK_FALSE (glide.getToggleState());
    glide.onClick();
    CHECK (glide.getToggleState());
    CHECK (editor.controller.glidesToNext ({ 0, 0 }));
    CHECK (editor.controller.glidesToNext ({ 0, 6 }));
    capturePhone (editor, phone ? "iphone-cc-grid" : "compact-auv3-cc-grid");
    glide.onClick();
    CHECK_FALSE (editor.controller.glidesToNext ({ 0, 0 }));
    CHECK_FALSE (editor.controller.glidesToNext ({ 0, 6 }));
    capturePhone (editor, phone ? "iphone-cc-hold-grid" : "compact-auv3-cc-hold-grid");

    phoneButton (editor, "Settings channel 1").onClick();
    input->onChoose();
    noteMode.onClick();
    CHECK_FALSE (editor.controller.isControllerRow (0));
    CHECK (editor.controller.state.playback.channels[0].note == pitch);
    CHECK (editor.controller.cell ({ 0, 0 }).velocity == 81);
    CHECK (editor.controller.cell ({ 0, 0 }).roll == 3);
    REQUIRE (editor.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (input->isVisible());
}

TEST_CASE ("Compact Copy toggle stays available outside iPhone mode", "[native-ui][iphone]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 87, 75, 3, 5, 2 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 409);
    editor.setSize (814, 409);
    editor.controller.selected.set (0);
    editor.sync();
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Copy selected cells when dragging")
            CHECK_FALSE (child->isVisible());
    editor.setCompactViewport (true, 814, 409, false);
    phoneButton (editor, "Copy selected cells when dragging").onClick();
    auto& grid = phoneGrid (editor);
    const auto first = grid.layout.cell ({ 0, 0 }).getCentre();
    const auto last = grid.layout.cell ({ 1, 4 }).getCentre();
    grid.mouseDown (phoneEvent (grid, first, first));
    grid.mouseDrag (phoneEvent (grid, last, first));
    grid.mouseUp (phoneEvent (grid, last, first));
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    CHECK (editor.controller.cell ({ 1, 4 }) == editor.controller.cell ({ 0, 0 }));
    editor.controller.undo();
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    CHECK_FALSE (editor.controller.cell ({ 1, 4 }).enabled);
}

TEST_CASE ("iPhone pattern navigation ignores selection mode and closes when the project changes", "[native-ui][iphone]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 111, 100, 1, 1, 1 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 3, 1, 1, 1, 44, 100, 1, 1, 1 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 390);
    editor.setSize (814, 390);
    editor.controller.selectLatched = true;
    editor.controller.selected.set (0);
    editor.sync();
    auto& grid = phoneGrid (editor);
    phoneButton (editor, "Pattern bank").onClick();
    CHECK_FALSE (grid.isVisible());
    phoneButton (editor, "Choose pattern 3").onClick();
    CHECK (grid.isVisible());
    CHECK (editor.controller.source() == 2);
    CHECK (editor.controller.copySource == -1);
    CHECK (editor.controller.cell ({ 0, 0 }).velocity == 44);
    phoneButton (editor, "Pattern bank").onClick();
    phoneButton (editor, "Pattern bank").onClick();
    CHECK (editor.controller.source() == 2);
    phoneButton (editor, "Pattern bank").onClick();
    phoneButton (editor, "Choose pattern 1").onClick();
    CHECK (editor.controller.cell ({ 0, 0 }).velocity == 111);
    phoneButton (editor, "Pattern bank").onClick();
    processor.resetProject();
    editor.controller.poll();
    editor.sync();
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Pattern picker")
            CHECK_FALSE (child->isVisible());
    CHECK (editor.controller.copySource == -1);

    editor.setCompactViewport (true, 814, 390, false);
    phoneButton (editor, "Pattern bank").onClick();
    auto& tile = phoneButton (editor, "Choose pattern 1");
    CHECK (grid.isVisible());
    CHECK_FALSE (tile.getParentComponent()->isOpaque());
    CHECK (tile.getHeight() == 44);
}

TEST_CASE ("iPhone settings remember context and pattern clearing requires the same pattern twice", "[native-ui][iphone]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 2, 1, 1, 1, 90 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 390);
    editor.setSize (814, 390);
    auto& recordButton = phoneButton (editor, "Arm Record");
    auto& menuButton = phoneButton (editor, "Open menu");
    auto& select = dynamic_cast<GestureButton&> (phoneButton (editor, "Select modifier"));
    const auto tapSelect = [&]
    {
        const auto bounds = select.getBounds();
        const auto point = juce::Point<float> { static_cast<float> (bounds.getWidth() - 4), 18 };
        select.mouseDown (phoneEvent (select, point, point));
        CHECK (select.getBounds() == bounds);
        select.mouseUp (phoneEvent (select, point, point, 0));
    };
    tapSelect();
    CHECK (editor.controller.selectLatched);
    CHECK (select.getToggleState());
    tapSelect();
    CHECK_FALSE (editor.controller.selectLatched);
    CHECK (recordButton.isVisible());
    auto& pattern = phoneButton (editor, "Open pattern settings");
    const auto patternBounds = pattern.getBounds();
    CHECK (pattern.getProperties().contains ("dropdown"));
    CHECK (pattern.getWidth() < dropdownWidth (pattern.getButtonText()));
    CHECK (menuButton.getWidth() < dropdownWidth (menuButton.getButtonText()));
    pattern.onClick();
    CHECK (pattern.getBounds() == patternBounds);
    CHECK (pattern.getToggleState());
    CHECK_FALSE (recordButton.isVisible());
    CHECK_FALSE (menuButton.isVisible());
    auto& clearButton = phoneButton (editor, "Clear pattern");
    CHECK (clearButton.isVisible());
    capturePhone (editor, "iphone-panel-pattern");
    phoneButton (editor, "Clear pattern").onClick();
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    editor.controller.selectSource (1);
    phoneButton (editor, "Clear pattern").onClick();
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    phoneButton (editor, "Clear pattern").onClick();
    CHECK_FALSE (editor.controller.cell ({ 0, 0 }).enabled);
    CHECK (phoneGrid (editor).isVisible());
    CHECK_FALSE (clearButton.isVisible());
    editor.controller.undo();
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    CHECK (phoneButton (editor, "Arm Record").isVisible());
    phoneButton (editor, "Open menu").onClick();
    CHECK_FALSE (pattern.isVisible());
    CHECK_FALSE (clearButton.isVisible());
    phoneButton (editor, "Feel").onClick();
    ValueControl* swingTarget = nullptr;
    for (auto* child : editor.getChildren())
        if (child->isVisible() && child->getTitle() == "Swing target")
            swingTarget = dynamic_cast<ValueControl*> (child);
    REQUIRE (swingTarget != nullptr);
    CHECK (swingTarget->minimum == 0);
    CHECK (swingTarget->maximum == 1);
    CHECK (swingTarget->format (0) == "1x");
    REQUIRE (processor.dispatchUiEngineCommand ("swing_subdivision", { 2 }));
    editor.controller.refresh();
    CHECK (swingTarget->value == 1);
    CHECK (swingTarget->format (swingTarget->value) == "2x");
    phoneButton (editor, "Close menu").onClick();
    phoneButton (editor, "Open menu").onClick();
    CHECK (phoneButton (editor, "Feel").getToggleState());
    phoneButton (editor, "Close menu").onClick();
    phoneGrid (editor).channelSettings (1);
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Open menu" || dynamic_cast<PreviewPanel*> (child) != nullptr)
            CHECK_FALSE (child->isVisible());
    phoneButton (editor, "Done").onClick();
    phoneButton (editor, "Open menu").onClick();
    CHECK (phoneButton (editor, "Feel").getToggleState());
}

TEST_CASE ("iPhone clears step selection without changing the mode controls' bounds", "[native-ui][iphone]")
{
    const int steps = GENERATE (16, 32);
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, steps }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 100 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 390);
    editor.setSize (814, 390);

    auto& grid = phoneGrid (editor);
    auto& velocity = phoneButton (editor, "Velocity editing layer");
    auto& probability = phoneButton (editor, "Prob editing layer");
    auto& select = phoneButton (editor, "Select modifier");
    const auto modeBounds = [&] { return std::array { velocity.getBounds(), probability.getBounds(), select.getBounds() }; };
    const auto idleBounds = modeBounds();
    const auto tap = [] (juce::Component& component, juce::Point<float> point)
    {
        component.mouseDown (phoneEvent (component, point, point));
        component.mouseUp (phoneEvent (component, point, point, 0));
    };
    const auto active = grid.layout.cell ({ 0, 2 }).getCentre();
    const auto empty = grid.layout.cell ({ 0, 3 }).getCentre();

    tap (select, select.getLocalBounds().toFloat().getCentre());
    REQUIRE (editor.controller.selectLatched);
    tap (grid, active);
    REQUIRE (editor.controller.selected.count() == 1);
    CHECK (modeBounds() == idleBounds);
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Deselect cells")
            CHECK_FALSE (child->isVisible());

    tap (grid, empty);
    CHECK (editor.controller.selected.none());
    CHECK (editor.controller.selectLatched);

    for (auto* layer : { &probability, &velocity })
    {
        if (!editor.controller.selectLatched)
            tap (select, select.getLocalBounds().toFloat().getCentre());
        tap (grid, active);
        REQUIRE (editor.controller.selected.count() == 1);
        tap (*layer, layer->getLocalBounds().toFloat().getCentre());
        CHECK (editor.controller.selected.none());
        CHECK_FALSE (editor.controller.selectLatched);
        CHECK (layer->getToggleState());
        CHECK (modeBounds() == idleBounds);
    }
}

TEST_CASE ("iPhone toolbar avoids overlapping page recording and selection controls at its minimum size", "[native-ui][iphone][render]")
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    bool openedMidi = false;
    processor.showStandaloneMidiOutputMenu = [&openedMidi] (juce::Component&) { openedMidi = true; };
    bool openedLink = false;
    processor.showStandaloneLinkSettings = [&openedLink] (juce::Component&) { openedLink = true; };
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, 32 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 390);
    editor.setSize (814, 390);
    editor.controller.selected.set (0);
    editor.sync();
    std::vector<juce::Rectangle<int>> buttons;
    for (auto* child : editor.getChildren())
    {
        if (!child->isVisible() || child->getY() >= Layout::compactHeaderHeight)
            continue;
        CHECK (editor.getLocalBounds().contains (child->getBounds()));
        for (const auto& bounds : buttons)
            CHECK_FALSE (bounds.intersects (child->getBounds()));
        buttons.push_back (child->getBounds());
    }
    CHECK (phoneGrid (editor).layout.cellHeight() >= 31.0f);
    const auto& undo = phoneButton (editor, "undo");
    const auto& redo = phoneButton (editor, "redo");
    const auto& pads = phoneButton (editor, "Show pads");
    auto& generate = phoneButton (editor, "Generate");
    const auto& record = phoneButton (editor, "Arm Record");
    const auto& tempo = phoneButton (editor, "Set tempo");
    const auto& children = editor.getChildren();
    const auto metro = std::find_if (children.begin(), children.end(),
                                     [] (auto* child) { return child->getTitle() == "Metronome"; });
    REQUIRE (metro != children.end());
    CHECK_FALSE ((*metro)->isVisible());
    const auto& play = phoneButton (editor, "Play");
    const auto& pattern = phoneButton (editor, "Open pattern settings");
    const auto& menu = phoneButton (editor, "Open menu");
    CHECK (phoneButton (editor, "Pattern bank").getX() == 0);
    CHECK (menu.getRight() == editor.getWidth());
    std::sort (buttons.begin(), buttons.end(), [] (const auto& a, const auto& b) { return a.getX() < b.getX(); });
    std::vector<int> gaps;
    for (size_t i = 1; i < buttons.size(); ++i)
        if (const int gap = buttons[i].getX() - buttons[i - 1].getRight(); gap > 0)
            gaps.push_back (gap);
    REQUIRE_FALSE (gaps.empty());
    const auto [smallestGap, largestGap] = std::minmax_element (gaps.begin(), gaps.end());
    CHECK (*largestGap - *smallestGap <= 1);
    CHECK (undo.getRight() == redo.getX());
    CHECK (redo.getRight() < generate.getX());
    CHECK (generate.getRight() < pads.getX());
    CHECK (generate.getButtonText().isEmpty());
    CHECK (generate.getBounds().withZeroOrigin() == pads.getBounds().withZeroOrigin());
    const int padGap = record.getX() - pads.getRight();
    CHECK (padGap > 0);
    CHECK (record.getRight() < tempo.getX());
    CHECK (tempo.getRight() < play.getX());
    CHECK (pattern.getRight() < menu.getX());
    CHECK (play.getRight() < pattern.getX());
    capturePhone (editor, "iphone-32-minimum");
    phoneButton (editor, "Open menu").onClick();
    for (const auto* page : { "Project", "Feel", "Settings", "About" })
    {
        CAPTURE (page);
        auto& tab = phoneButton (editor, page);
        tab.onClick();
        CHECK (tab.getToggleState());
        CHECK (tab.getProperties().contains ("phoneMenuIcon"));
        buttons.clear();
        for (auto* child : editor.getChildren())
        {
            if (!child->isVisible())
                continue;
            CHECK (editor.getLocalBounds().contains (child->getBounds()));
            for (const auto& bounds : buttons)
                CHECK_FALSE (bounds.intersects (child->getBounds()));
            buttons.push_back (child->getBounds());
        }
        capturePhone (editor, "iphone-minimum-menu-" + juce::String (page).toLowerCase());
    }
    auto& midiOutput = phoneButton (editor, "MIDI output destination");
    CHECK (midiOutput.getY() == phoneButton (editor, "About").getY());
    midiOutput.onClick();
    CHECK (openedMidi);
    phoneButton (editor, "Close menu").onClick();
    generate.onClick();
    juce::Viewport* generatorViewport = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* viewport = dynamic_cast<juce::Viewport*> (child); viewport != nullptr && viewport->isVisible())
            generatorViewport = viewport;
    REQUIRE (generatorViewport != nullptr);
    auto* generator = dynamic_cast<PreviewPanel*> (generatorViewport->getViewedComponent());
    REQUIRE (generator != nullptr);
    PatternXYPad* pad = nullptr;
    for (auto* child : generator->getChildren())
    {
        if (auto* xy = dynamic_cast<PatternXYPad*> (child))
            pad = xy;
    }
    REQUIRE (pad != nullptr);
    generate.onClick();
    pattern.onClick();
    ValueControl* gridLength = nullptr;
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Grid Length")
            gridLength = dynamic_cast<ValueControl*> (child);
    REQUIRE (gridLength != nullptr);
    REQUIRE (gridLength->isVisible());
    CHECK (gridLength->captionFontSize == 10.0f);
    CHECK (gridLength->value == 32);
    const auto setLength = [] (ValueControl& control, int count)
    {
        auto accessibility = control.createAccessibilityHandler();
        accessibility->getValueInterface()->setValue (static_cast<double> (count));
    };
    setLength (*gridLength, 64);
    CHECK (editor.controller.state.playback.stepCount == 64);
    pattern.onClick();
    generate.onClick();
    REQUIRE (pad->isVisible());
    CHECK (pad->getX() > generator->getWidth() / 2);
    CHECK (generator->getLocalBounds().contains (pad->getBounds()));
    const auto centre = pad->getLocalBounds().getCentre().toFloat();
    REQUIRE_FALSE (editor.controller.cell ({ 0, 0 }).enabled);
    pad->mouseDown (phoneEvent (*pad, centre, centre));
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    CHECK (editor.controller.cell ({ 0, 16 }).enabled);
    CHECK (editor.controller.cell ({ 0, 48 }).enabled);
    pad->mouseUp (phoneEvent (*pad, centre, centre, 0));
    const auto firstSeedPattern = editor.controller.state.playback.sources[0];
    auto& firstSeed = phoneButton (editor, "Generator seed A");
    auto& nextSeed = phoneButton (editor, "Generator seed B");
    CHECK (firstSeed.getButtonText() == "A");
    CHECK (phoneButton (editor, "Generator seed E").getButtonText() == "E");
    CHECK (firstSeed.getToggleState());
    CHECK (nextSeed.getY() - pad->getBottom() >= 16);
    capturePhone (editor, "iphone-generate");
    nextSeed.onClick();
    CHECK (nextSeed.getToggleState());
    CHECK_FALSE (firstSeed.getToggleState());
    CHECK (editor.controller.state.playback.sources[0] != firstSeedPattern);
    editor.controller.undo();
    CHECK (editor.controller.state.playback.sources[0] == firstSeedPattern);
    editor.controller.undo();
    CHECK_FALSE (editor.controller.cell ({ 0, 0 }).enabled);
    generate.onClick();
    phoneButton (editor, "Open menu").onClick();
    CHECK_FALSE (generate.isVisible());
    phoneButton (editor, "Settings").onClick();
    auto& metronome = phoneButton (editor, "Metronome");
    const auto& metronomeSound = phoneButton (editor, "Metronome sound...");
    CHECK (metronome.getRight() < metronomeSound.getX());
    CHECK (metronome.getY() == metronomeSound.getY());
    metronome.setToggleState (true, juce::sendNotificationSync);
    CHECK (processor.isStandaloneMetronomeEnabled());
    auto& link = phoneButton (editor, "Ableton Link settings");
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Switch accent")
            CHECK_FALSE (child->isVisible());
    CHECK (link.isVisible());
    CHECK (midiOutput.isVisible());
    CHECK (link.getY() >= midiOutput.getBottom());
    link.onClick();
    CHECK (openedLink);
    phoneButton (editor, "Close menu").onClick();
    editor.setCompactViewport (true, 850, 390);
    editor.setSize (850, 390);
    CHECK (record.getX() - pads.getRight() >= padGap);

    // The iPad standalone has the same Link settings action in the full editor footer.
    openedLink = false;
    editor.layout.touchRows = true;
    editor.setCompactViewport (false, 0, 0);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    CHECK (link.isVisible());
    CHECK (midiOutput.isVisible());
    CHECK (link.getRight() <= midiOutput.getX());
    CHECK (link.getBounds().getCentreY() == midiOutput.getBounds().getCentreY());
    link.onClick();
    CHECK (openedLink);
}

TEST_CASE ("iOS performance pads replace the inspector independently of record arm", "[native-ui][iphone][ipad][row-trigger][render]")
{
    const bool compact = GENERATE (false, true);
    const bool reversed = GENERATE (false, true);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("channels", { 8 }));
    const std::array<const char*, 8> names { "Kick", "Snare", "Hat", "Open", "Clap", "Rim", "Tom", "Perc" };
    for (int ch = 0; ch < 8; ++ch)
    {
        REQUIRE (processor.dispatchUiEngineCommand ("channel_label", { ch + 1, names[static_cast<size_t> (ch)] }));
        for (int step = 0; step < 16; ++step)
            if ((step + ch) % (ch % 4 + 2) == 0)
                REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, ch + 1, step + 1, 1, 80 + step % 40 }));
    }
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    editor.layout.reverseRows = reversed;
    editor.setCompactViewport (compact, 814, 409);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    editor.sync();
    auto& grid = phoneGrid (editor);
    const auto gridBounds = grid.getBounds();
    for (int ch = 0; ch < 8; ++ch)
    {
        auto* title = dynamic_cast<juce::TextButton*> (grid.rowTitle (ch));
        REQUIRE (title != nullptr);
        CHECK (title->getButtonText() == juce::String (ch + 1));
        CHECK (title->getTriggeredOnMouseDown());
    }
    auto& showPads = phoneButton (editor, "Show pads");
    const auto& select = phoneButton (editor, "Select modifier");
    const auto selectBounds = select.getBounds();
    CHECK (showPads.getRight() <= phoneButton (editor, "Arm Record").getX());
    showPads.setToggleState (true, juce::sendNotificationSync);
    CHECK_FALSE (processor.isPatternRecordingEnabled());
    for (int ch = 0; ch < 8; ++ch)
    {
        auto& trigger = phoneButton (editor, "Trigger channel " + juce::String (ch + 1));
        CHECK (trigger.getWidth() == trigger.getHeight());
        CHECK (trigger.getParentComponent()->getLocalBounds().contains (trigger.getBounds()));
        if (compact)
            CHECK (trigger.getWidth() >= 120);
        else
            CHECK (trigger.getParentComponent()->getY() == grid.getBottom());
        CHECK (trigger.getButtonText().isEmpty());
    }
    CHECK (grid.getBounds() == gridBounds);
    auto& playbackPad = phoneButton (editor, "Trigger channel 1");
    const auto flashBrightness = [&]
    {
        const auto image = playbackPad.createComponentSnapshot (playbackPad.getLocalBounds());
        return image.getPixelAt (image.getWidth() / 2, image.getHeight() / 2).getBrightness();
    };
    const float idleBrightness = flashBrightness();
    processor.getUiBridge().emitNoteHit ({ .velocity = 40, .uiChannel = 1 });
    const float quietBrightness = flashBrightness();
    processor.getUiBridge().emitNoteHit ({ .velocity = 127, .uiChannel = 1 });
    CHECK (flashBrightness() > quietBrightness);
    CHECK (quietBrightness > idleBrightness);
    if (compact)
    {
        for (int ch = 1; ch <= 4; ++ch)
        {
            const auto& lower = phoneButton (editor, "Trigger channel " + juce::String (ch));
            const auto& higher = phoneButton (editor, "Trigger channel " + juce::String (ch + 4));
            CHECK (lower.getX() == higher.getX());
            CHECK (lower.getY() > higher.getBottom());
        }
        CHECK (select.isVisible());
        CHECK (select.getBounds() == selectBounds);
        CHECK_FALSE (grid.isVisible());
        const juce::Point<int> previewPoint { editor.getWidth() / 2, Layout::compactHeaderHeight + 30 };
        CHECK (editor.getComponentAt (previewPoint) == nullptr);
        const auto before = processor.editorDocument();
        editor.mouseDown (phoneEvent (editor, previewPoint.toFloat(), previewPoint.toFloat()));
        CHECK (processor.editorDocument() == before);
        CHECK (grid.layout.cellWidth() >= 42.0f);
        CHECK (grid.layout.cellHeight() >= 33.0f);
    }
    else
        CHECK (grid.isVisible());
    auto& velocityMode = phoneButton (editor, "Pad velocity mode");
    const auto velocityPreference = velocityMode.getToggleState();
    if (compact)
        CHECK (velocityMode.getWidth() > 56);
    else
        CHECK (velocityMode.getWidth() == 64);
    CHECK (velocityMode.getHeight() == phoneButton (editor, "Trigger channel 8").getHeight());
    CHECK (velocityMode.hitTest (1, 1));
    const auto captureName = compact ? "iphone-recording-pads" : "ipad-recording-pads";
    velocityMode.setToggleState (false, juce::dontSendNotification);
    capturePhone (editor, captureName);
    velocityMode.setToggleState (true, juce::dontSendNotification);
    capturePhone (editor, juce::String (captureName) + "-velocity-on");
    velocityMode.setToggleState (velocityPreference, juce::dontSendNotification);
    if (!compact)
    {
        const Location location { 0, 0 };
        const auto* padStrip = phoneButton (editor, "Trigger channel 1").getParentComponent();
        REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100 }));
        editor.controller.poll();
        editor.controller.selected.set (static_cast<size_t> (location.key()));
        editor.sync();
        CHECK_FALSE (padStrip->isVisible());
        CHECK (showPads.getToggleState());
        editor.controller.selected.reset();
        editor.sync();
        CHECK (padStrip->isVisible());
    }
    REQUIRE (processor.dispatchUiEngineCommand ("channels", { 3 }));
    editor.controller.poll();
    CHECK (phoneButton (editor, "Trigger channel 3").isEnabled());
    CHECK_FALSE (phoneButton (editor, "Trigger channel 4").isEnabled());
    editor.controller.command ("pattern_record_enabled", { true, 1 });
    auto* strip = phoneButton (editor, "Trigger channel 3").getParentComponent();
    showPads.setToggleState (false, juce::sendNotificationSync);
    CHECK_FALSE (strip->isVisible());
    CHECK (grid.isVisible());
    CHECK (processor.isPatternRecordingEnabled());
}

TEST_CASE ("Only standalone consumes QWERTY pad shortcuts", "[native-ui][row-trigger]")
{
    using Processor = juce::AudioProcessor;
    const auto wrapper = GENERATE (Processor::wrapperType_Standalone, Processor::wrapperType_VST3, Processor::wrapperType_AudioUnitv3);
    Processor::setTypeOfNextNewPlugin (wrapper);
    PluginProcessor processor;
    Processor::setTypeOfNextNewPlugin (Processor::wrapperType_Undefined);
    REQUIRE (processor.dispatchUiEngineCommand ("channels", { 8 }));
    NativeEditor editor (processor);

    const bool standalone = wrapper == Processor::wrapperType_Standalone;
    for (const auto key : juce::String ("asdfghjk"))
        CHECK (editor.keyPressed (juce::KeyPress (static_cast<int> (key), {}, key)) == standalone);
}

TEST_CASE ("Incoming MIDI flashes pads assigned to the note with transport stopped", "[native-ui][iphone][ipad][row-trigger][midi-input]")
{
    const bool compact = GENERATE (false, true);
    const bool standalone = GENERATE (false, true);
    if (standalone)
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    processor.setTimestampedStandaloneMidiInput (standalone);
    processor.prepareToPlay (48000.0, 64);
    REQUIRE (processor.dispatchUiEngineCommand ("channels", { 8 }));
    REQUIRE (processor.dispatchUiEngineCommand ("channel_note", { 1, 60 }));
    REQUIRE (processor.dispatchUiEngineCommand ("channel_note", { 2, 60 }));
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    editor.setCompactViewport (compact, 814, 409);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    editor.sync();
    phoneButton (editor, "Show pads").setToggleState (true, juce::sendNotificationSync);

    auto& first = phoneButton (editor, "Trigger channel 1");
    auto& second = phoneButton (editor, "Trigger channel 2");
    auto& third = phoneButton (editor, "Trigger channel 3");
    const auto centrePixel = [] (juce::Component& pad)
    {
        const auto centre = pad.getLocalBounds().getCentre();
        return pad.createComponentSnapshot ({ centre.x, centre.y, 1, 1 }).getPixelAt (0, 0);
    };
    const auto firstIdle = centrePixel (first), secondIdle = centrePixel (second), thirdIdle = centrePixel (third);
    const int thirdNote = editor.controller.state.playback.channels[2].note;
    juce::AudioBuffer<float> audio (2, 64);
    juce::MidiBuffer midi;
    for (const auto& message : { juce::MidiMessage::noteOn (6, 60, juce::uint8 (95)),
                                 juce::MidiMessage::noteOn (6, thirdNote, juce::uint8 (0)) })
    {
        if (standalone)
            processor.captureStandaloneMidiInput (message);
        midi.addEvent (message, 0);
    }
    if (standalone)
        REQUIRE (processor.emitPendingNoteHitsForUi() == 1);
    processor.processBlock (audio, midi);
    REQUIRE (processor.emitPendingNoteHitsForUi() == (standalone ? 0 : 1));
    CHECK (centrePixel (first) != firstIdle);
    CHECK (centrePixel (second) != secondIdle);
    CHECK (centrePixel (third) == thirdIdle);
    int noteOns = 0;
    for (const auto event : midi)
        if (event.getMessage().isNoteOn())
        {
            ++noteOns;
            CHECK (event.getMessage().getNoteNumber() == 60);
            CHECK (event.getMessage().getVelocity() == 95);
        }
    CHECK (noteOns == (standalone ? 0 : 1));
}

TEST_CASE ("Pads triggered by the same touch event share their MIDI timing", "[native-ui][iphone][ipad][row-trigger][latency]")
{
    const bool compact = GENERATE (false, true);
    constexpr int blockSize = 8192;
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, blockSize);
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    if (compact)
        editor.setCompactViewport (true, 814, 409);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    editor.sync();
    phoneButton (editor, "Show pads").setToggleState (true, juce::sendNotificationSync);
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    auto* peer = editor.getPeer();
    REQUIRE (peer != nullptr);

    // The macOS runner uses a pen source to exercise the non-mouse pad path.
    const auto time = juce::Time::getCurrentTime();
    peer->handleMouseEvent (juce::MouseInputSource::pen, {}, {}, 0.0f, 0.0f, time.toMilliseconds());
    const auto& sources = juce::Desktop::getInstance().getMouseSources();
    const auto source = std::find_if (sources.begin(), sources.end(), [] (const auto& pointer) { return pointer.isPen(); });
    REQUIRE (source != sources.end());
    const auto pointer = *source;
    const auto event = [&] (juce::Component& pad, juce::ModifierKeys mods)
    {
        const auto centre = pad.getLocalBounds().getCentre().toFloat();
        return juce::MouseEvent (pointer, centre, mods, 1, 0, 0, 0, 0, &pad, &pad, time, centre, time, 1, false);
    };
    auto& first = dynamic_cast<RowTriggerButton&> (phoneButton (editor, "Trigger channel 1"));
    auto& second = dynamic_cast<RowTriggerButton&> (phoneButton (editor, "Trigger channel 2"));
    const auto centrePixel = [] (RowTriggerButton& pad)
    {
        const auto centre = pad.getLocalBounds().getCentre();
        return pad.createComponentSnapshot ({ centre.x, centre.y, 1, 1 }).getPixelAt (0, 0);
    };
    const auto firstIdle = centrePixel (first), secondIdle = centrePixel (second);
    const juce::ModifierKeys pressed (juce::ModifierKeys::leftButtonModifier);
    first.mouseDown (event (first, pressed));
    juce::Thread::sleep (6); // Simulate a slow main thread between dispatches of the same event.
    second.mouseDown (event (second, pressed));

    juce::AudioBuffer<float> audio (2, blockSize);
    juce::MidiBuffer midi;
    processor.processBlock (audio, midi);
    std::vector<std::pair<int, int>> hits;
    for (const auto& message : midi)
        if (message.getMessage().isNoteOn())
            hits.emplace_back (message.getMessage().getNoteNumber(), message.samplePosition);
    REQUIRE (hits.size() == 2);
    CHECK (hits[0].first == 36);
    CHECK (hits[1].first == 37);
    CHECK (hits[0].second > 0);
    CHECK (hits[0].second == hits[1].second);
    CHECK (centrePixel (first) != firstIdle);
    CHECK (centrePixel (second) != secondIdle);
    first.mouseUp (event (first, {}));
    const auto afterFlash = juce::Time::getMillisecondCounterHiRes() + 101.0;
    first.updateFlash (afterFlash);
    second.updateFlash (afterFlash);
    CHECK (centrePixel (first) == firstIdle);
    CHECK (centrePixel (second) == secondIdle);
    second.mouseUp (event (second, {}));
}

TEST_CASE ("iOS row pads audition on touch down and record into the armed pattern with undo", "[native-ui][iphone][ipad][row-trigger][record]")
{
    const bool compact = GENERATE (false, true);
    class PadPlayHead final : public juce::AudioPlayHead
    {
    public:
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (20.0);
            info.setPpqPosition (0.0);
            info.setIsPlaying (playing);
            return info;
        }
    } playHead;
    PluginProcessor processor;
    processor.setPlayHead (&playHead);
    processor.prepareToPlay (48000.0, 64);
    REQUIRE (processor.dispatchUiEngineCommand ("channels", { 8 }));
    REQUIRE (processor.dispatchUiEngineCommand ("channel_note", { 3, 65 }));
    REQUIRE (processor.dispatchUiEngineCommand ("channel_note", { 6, 72 }));
    NativeEditor editor (processor);
    editor.layout.touchRows = true;
    editor.layout.reverseRows = true;
    editor.setCompactViewport (compact, 814, 409);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    editor.controller.clearPattern();
    editor.controller.selected.set (0);
    editor.controller.selectLatched = true;
    editor.sync();
    const auto before = processor.editorDocument();
    auto& rowNumber = *phoneGrid (editor).rowTitle (2);
    const auto rowPoint = rowNumber.getLocalBounds().toFloat().getCentre();
    rowNumber.mouseDown (phoneEvent (rowNumber, rowPoint, rowPoint));
    rowNumber.mouseUp (phoneEvent (rowNumber, rowPoint, rowPoint));
    phoneButton (editor, "Show pads").setToggleState (true, juce::sendNotificationSync);
    auto& velocityMode = phoneButton (editor, "Pad velocity mode");
    const auto velocityPreference = velocityMode.getToggleState();
    velocityMode.setToggleState (false, juce::sendNotificationSync);
    juce::AudioBuffer<float> audio (2, 64);
    juce::MidiBuffer midi;
    auto& practicePad = static_cast<juce::Component&> (phoneButton (editor, "Trigger channel 6"));
    const auto padPoint = practicePad.getLocalBounds().toFloat().getCentre();
    practicePad.mouseDown (phoneEvent (practicePad, padPoint, padPoint));
    practicePad.mouseUp (phoneEvent (practicePad, padPoint, padPoint));
    midi.clear();
    processor.processBlock (audio, midi);
    std::vector<int> practiceNotes;
    for (const auto event : midi)
        if (event.getMessage().isNoteOn())
            practiceNotes.push_back (event.getMessage().getNoteNumber());
    CHECK (practiceNotes == std::vector<int> { 65, 72 });
    editor.controller.poll();
    CHECK (processor.editorDocument() == before);

    editor.controller.command ("pattern_record_enabled", { 1, 2 });
    midi.clear();
    auto& first = static_cast<juce::Component&> (phoneButton (editor, "Trigger channel 3"));
    auto& second = static_cast<juce::Component&> (phoneButton (editor, "Trigger channel 6"));
    const auto firstPoint = first.getLocalBounds().toFloat().getCentre();
    const auto secondPoint = second.getLocalBounds().toFloat().getCentre();
    first.mouseDown (phoneEvent (first, firstPoint, firstPoint));
    second.mouseDown (phoneEvent (second, secondPoint, secondPoint));
    // Both fingers can remain down while the audio callback emits the hits.
    processor.processBlock (audio, midi);
    std::vector<int> notes;
    for (const auto event : midi)
        if (event.getMessage().isNoteOn())
        {
            notes.push_back (event.getMessage().getNoteNumber());
            CHECK (event.getMessage().getVelocity() == 100);
        }
    CHECK (notes == std::vector<int> { 65, 72 });
    editor.controller.poll();
    const auto recorded = processor.editorDocument();
    CHECK (recorded.sources[1][2][0].enabled);
    CHECK (recorded.sources[1][2][0].velocity == 100);
    CHECK (recorded.sources[1][5][0].enabled);
    CHECK (recorded.sources[0] == before.sources[0]);
    CHECK (editor.controller.selected.test (0));
    CHECK (editor.controller.selectLatched);
    first.mouseUp (phoneEvent (first, firstPoint, firstPoint));
    second.mouseUp (phoneEvent (second, secondPoint, secondPoint));
    midi.clear();
    processor.processBlock (audio, midi);
    for (const auto event : midi)
        CHECK_FALSE (event.getMessage().isNoteOn());
    // A fresh tap retriggers the row; holding, dragging, and releasing do not repeat it.
    first.mouseDown (phoneEvent (first, firstPoint, firstPoint));
    first.mouseDrag (phoneEvent (first, firstPoint.translated (0, -10), firstPoint));
    first.mouseUp (phoneEvent (first, firstPoint, firstPoint));
    midi.clear();
    processor.processBlock (audio, midi);
    int repeats = 0;
    for (const auto event : midi)
        if (event.getMessage().isNoteOn())
        {
            CHECK (event.getMessage().getNoteNumber() == 65);
            ++repeats;
        }
    CHECK (repeats == 1);
    editor.controller.command ("pattern_record_enabled", { 0, 2 });
    editor.controller.undo();
    CHECK (processor.editorDocument() == before);
    editor.controller.redo();
    CHECK (processor.editorDocument() == recorded);
    playHead.playing = false;
    midi.clear();
    processor.processBlock (audio, midi);
    editor.controller.command ("pattern_record_enabled", { 1, 2 });
    auto& stoppedPad = static_cast<juce::Component&> (phoneButton (editor, "Trigger channel 4"));
    const auto point = stoppedPad.getLocalBounds().toFloat().getCentre();
    stoppedPad.mouseDown (phoneEvent (stoppedPad, point, point));
    stoppedPad.mouseUp (phoneEvent (stoppedPad, point, point));
    midi.clear();
    processor.processBlock (audio, midi);
    int stoppedHits = 0;
    for (const auto event : midi)
        if (event.getMessage().isNoteOn())
        {
            CHECK (event.getMessage().getNoteNumber() == 39);
            ++stoppedHits;
        }
    CHECK (stoppedHits == 1);
    editor.controller.poll();
    CHECK (processor.editorDocument() == recorded);
    velocityMode.setToggleState (true, juce::sendNotificationSync);
    for (const int velocity : { 127, 1 })
    {
        const auto hit = point.withY (velocity == 127 ? 0.0f : static_cast<float> (stoppedPad.getHeight() - 1));
        stoppedPad.mouseDown (phoneEvent (stoppedPad, hit, hit));
        stoppedPad.mouseUp (phoneEvent (stoppedPad, hit, hit));
        midi.clear();
        processor.processBlock (audio, midi);
        std::vector<int> velocities;
        for (const auto event : midi)
            if (event.getMessage().isNoteOn())
                velocities.push_back (event.getMessage().getVelocity());
        CHECK (velocities == std::vector<int> { velocity });
    }
    velocityMode.setToggleState (velocityPreference, juce::sendNotificationSync);
}

TEST_CASE ("iPhone visible rows enlarge the grid and scroll without changing the pattern", "[native-ui][iphone][row-scroll][render]")
{
    const bool reversed = GENERATE (false, true);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("channels", { 8 }));
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, 32 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 409);
    editor.setSize (814, 409);
    editor.layout.reverseRows = reversed;
    editor.sync();
    auto& grid = phoneGrid (editor);
    const float fullHeight = grid.layout.rowHeight();
    processor.markDocumentClean();
    phoneButton (editor, "Open menu").onClick();
    phoneButton (editor, "Settings").onClick();
    auto& rowsButton = phoneButton (editor, "Visible rows");
    CHECK (rowsButton.getButtonText() == "Rows: 8");
    auto& shortcuts = phoneButton (editor, "Shift/Option shortcuts");
    const bool shortcutsEnabled = editor.controller.keyboardModifiersEnabled;
    CHECK (shortcuts.getToggleState() == shortcutsEnabled);
    CHECK (editor.getLocalBounds().contains (shortcuts.getBounds()));
    shortcuts.onClick();
    CHECK (editor.controller.keyboardModifiersEnabled == !shortcutsEnabled);
    CHECK (shortcuts.getToggleState() == !shortcutsEnabled);
    shortcuts.onClick();
    phoneButton (editor, "Close menu").onClick();
    CHECK_FALSE (rowsButton.isVisible());
    CHECK_FALSE (shortcuts.isVisible());
    CHECK (grid.layout.cellsY() == 18.0f);
    juce::ScrollBar* scroll = nullptr;
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Scroll rows") scroll = dynamic_cast<juce::ScrollBar*> (child);
    REQUIRE (scroll != nullptr);
    CHECK_FALSE (scroll->isVisible());
    for (const int count : { 4, 5, 6, 7, 8 })
    {
        editor.setCompactVisibleRows (count);
        CHECK (rowsButton.getButtonText() == "Rows: " + juce::String (count));
        CHECK (scroll->isVisible() == (count < 8));
        CHECK (grid.getBottom() + Layout::compactInspectorHeight == editor.getHeight());
        CHECK (grid.layout.rowHeight() == Catch::Approx (fullHeight * 8.0f / static_cast<float> (count)));
        for (int start = 0; start <= 8 - count; ++start)
        {
            scroll->setCurrentRangeStart (start, juce::sendNotificationSync);
            CHECK (editor.layout.firstRow == start);
            for (int channel = 0; channel < 8; ++channel)
            {
                const int display = reversed ? 7 - channel : channel;
                const bool visible = display >= start && display < start + count;
                CHECK (grid.layout.channelVisible (channel) == visible);
                CHECK (grid.rowTitle (channel)->isVisible() == visible);
                if (visible)
                {
                    const auto bounds = grid.layout.cell ({ channel, 0 });
                    CHECK (grid.getLocalBounds().toFloat().contains (bounds));
                    CHECK (grid.layout.at (bounds.getCentre()) == Location { channel, 0 });
                }
            }
        }
        CHECK_FALSE (processor.isDocumentModified());
    }
    editor.setCompactVisibleRows (4);
    scroll->setCurrentRangeStart (2, juce::sendNotificationSync);
    const int channel = reversed ? 5 : 2;
    const auto point = grid.layout.cell ({ channel, 0 }).getCentre();
    grid.mouseDown (phoneEvent (grid, point, point));
    grid.mouseUp (phoneEvent (grid, point, point));
    CHECK (editor.controller.cell ({ channel, 0 }).enabled);
    editor.controller.undo();
    CHECK_FALSE (editor.controller.cell ({ channel, 0 }).enabled);
    capturePhone (editor, reversed ? "iphone-four-rows-reversed" : "iphone-four-rows");
    phoneButton (editor, "Switch step page").onClick();
    CHECK (grid.layout.firstStep == 16);
    CHECK (grid.layout.at (grid.layout.cell ({ channel, 16 }).getCentre()) == Location { channel, 16 });
    REQUIRE (processor.dispatchUiEngineCommand ("channels", { 4 }));
    editor.controller.refresh();
    CHECK (editor.layout.firstRow == 0);
    CHECK_FALSE (scroll->isVisible());
    phoneButton (editor, "Open menu").onClick();
    CHECK (rowsButton.isVisible());
    CHECK (rowsButton.getButtonText() == "Rows: 4");
    phoneButton (editor, "Close menu").onClick();
    CHECK_FALSE (rowsButton.isVisible());
    CHECK (grid.isVisible());
    phoneButton (editor, "Open menu").onClick();
    CHECK (rowsButton.isVisible());
    editor.setCompactViewport (true, 814, 409, false);
    editor.setCompactVisibleRows (2);
    CHECK (editor.layout.compactVisibleRows == 8);
    CHECK_FALSE (rowsButton.isVisible());
    CHECK_FALSE (scroll->isVisible());
    editor.setCompactViewport (false, 1338, 900);
    CHECK_FALSE (rowsButton.isVisible());
    CHECK (editor.layout.firstRow == 0);
}
