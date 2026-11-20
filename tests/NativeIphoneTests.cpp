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
    fader->setValue (168, juce::sendNotificationSync);
    CHECK (processor.getStandaloneTempoBpm() == 168.0);
    phoneButton (editor, "Increase tempo by 1 BPM").onClick();
    CHECK (processor.getStandaloneTempoBpm() == 169.0);
    phoneButton (editor, "Decrease tempo by 1 BPM").onClick();
    CHECK (processor.getStandaloneTempoBpm() == 168.0);
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

TEST_CASE ("iPhone Chance keeps cycle editing in the inspector", "[native-ui][iphone]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 90, 50, 4, 5, 1 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 390);
    editor.setSize (814, 390);
    auto& chance = phoneButton (editor, "Chance editing layer");
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
    for (auto* child : grid.getChildren())
        if (child->getTitle() == "Step presets" || child->getTitle() == "Step cycle editor")
            CHECK_FALSE (child->isVisible());
}

TEST_CASE ("iPhone applies triplets from the selected step inspector across step pages", "[native-ui][iphone][triplets]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, 32 }));
    REQUIRE (processor.dispatchUiEngineCommand ("channel_loop_length", { 1, 32, 1 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 15, 1, 90 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 390);
    editor.setSize (814, 390);
    auto& grid = phoneGrid (editor);
    const Location step { 0, 14 };
    auto& rhythm = phoneButton (editor, "Step rhythm and triplets");
    CHECK_FALSE (rhythm.isEnabled());
    const auto select = [&] (Location location)
    {
        editor.controller.selectLatched = true;
        const auto point = grid.layout.cell (location).getCentre();
        grid.mouseDown (phoneEvent (grid, point, point));
        grid.mouseUp (phoneEvent (grid, point, point, 0));
    };
    select (step);
    REQUIRE (rhythm.isEnabled());
    rhythm.onClick();
    REQUIRE (phoneButton (editor, "Quarter triplet").isEnabled());
    editor.sync();
    REQUIRE (phoneButton (editor, "Quarter triplet").isEnabled());
    phoneButton (editor, "Quarter triplet").onClick();
    CHECK (grid.openPresetMenu() == nullptr);
    REQUIRE (editor.controller.cell (step).span == 8);
    CHECK (editor.controller.cell (step).roll == 3);
    phoneButton (editor, "Switch step page").onClick();
    REQUIRE (grid.layout.firstStep == 16);
    editor.controller.selected.reset();
    editor.sync();
    select ({ 0, 17 });
    CHECK (editor.controller.selected.test (static_cast<size_t> (step.key())));
    REQUIRE (rhythm.isEnabled());
    rhythm.onClick();
    CHECK (phoneButton (editor, "Quarter triplet").getToggleState());
    CHECK (phoneButton (editor, "Quarter triplet").getHeight() == 42);
    REQUIRE (grid.openPresetMenu() != nullptr);
    ValueControl* span = nullptr;
    for (auto* child : grid.openPresetMenu()->getChildren())
        if (child->getTitle() == "Step span")
            span = dynamic_cast<ValueControl*> (child);
    REQUIRE (span != nullptr);
    CHECK (span->value == 8);
    const auto origin = span->getLocalBounds().getCentre().toFloat();
    span->mouseDown (phoneEvent (*span, origin, origin));
    span->mouseUp (phoneEvent (*span, origin, origin, 0));
    REQUIRE (grid.openPresetMenu() != nullptr);

    const auto target = origin.translated (0.0f, 8.0f);
    span->mouseDown (phoneEvent (*span, origin, origin));
    span->mouseDrag (phoneEvent (*span, target, origin));
    CHECK (span->value == 7);
    CHECK (editor.controller.cell (step).span == 8);
    span->mouseUp (phoneEvent (*span, target, origin, 0));
    CHECK (grid.openPresetMenu() == nullptr);
    CHECK (editor.controller.cell (step).span == 7);
    CHECK (editor.controller.cell (step).roll == 3);
    editor.controller.undo();
    CHECK (editor.controller.cell (step).span == 8);
}

TEST_CASE ("Holding an empty cell opens the rhythm menu and sliding releases onto a preset", "[native-ui][step-presets][touch]")
{
    const bool phone = GENERATE (false, true);
    PluginProcessor processor;
    NativeEditor editor (processor);
    if (phone)
        editor.setCompactViewport (true, 814, 390);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
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
    grid.mouseDown (event (origin, pressed));
    CHECK (grid.openPresetMenu() == nullptr);
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    while (grid.openPresetMenu() == nullptr && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        juce::Thread::sleep (10);
        juce::Timer::callPendingTimersSynchronously();
    }
    REQUIRE (grid.openPresetMenu() != nullptr);
    CHECK_FALSE (editor.controller.cell (cell).enabled);
    auto& preset = phoneButton (editor, "8th triplet");
    const auto target = grid.getLocalPoint (&preset, preset.getLocalBounds().getCentre().toFloat());
    grid.mouseDrag (event (target, pressed));
    CHECK (grid.openPresetMenu() != nullptr);
    CHECK_FALSE (editor.controller.cell (cell).enabled);
    grid.mouseUp (event (target, 0));
    CHECK (grid.openPresetMenu() == nullptr);
    CHECK (editor.controller.cell (cell).enabled);
    CHECK (editor.controller.cell (cell).span == 4);
    CHECK (editor.controller.cell (cell).roll == 3);
    editor.controller.undo();
    CHECK_FALSE (editor.controller.cell (cell).enabled);
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

TEST_CASE ("iPhone inspector relative edits undo and cancel without leaking into the next gesture", "[native-ui][iphone]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 80, 100, 1, 1, 1 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 409);
    editor.setSize (814, 409);
    editor.controller.selected.set (0);
    editor.sync();
    ValueControl* velocity = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* preview = dynamic_cast<PreviewPanel*> (child))
            for (auto* control : preview->getChildren())
                if (control->getTitle() == "Selected velocity offset")
                    velocity = dynamic_cast<ValueControl*> (control);
    REQUIRE (velocity != nullptr);
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
    CHECK (recordButton.isVisible());
    auto& pattern = phoneButton (editor, "Open pattern settings");
    const auto patternBounds = pattern.getBounds();
    CHECK (pattern.getProperties().contains ("dropdown"));
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
    editor.controller.undo();
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    phoneButton (editor, "Close pattern settings").onClick();
    CHECK (phoneButton (editor, "Arm Record").isVisible());
    phoneButton (editor, "Open menu").onClick();
    CHECK_FALSE (pattern.isVisible());
    CHECK_FALSE (clearButton.isVisible());
    phoneButton (editor, "Feel").onClick();
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
    const auto& metro = phoneButton (editor, "Metronome");
    const auto& play = phoneButton (editor, "Play");
    const auto& pattern = phoneButton (editor, "Open pattern settings");
    const auto& menu = phoneButton (editor, "Open menu");
    const int referenceGap = phoneButton (editor, "Velocity editing layer").getX() - phoneButton (editor, "Pattern bank").getRight();
    CHECK (undo.getRight() == redo.getX());
    CHECK (redo.getRight() < generate.getX());
    CHECK (generate.getRight() < pads.getX());
    const int padGap = record.getX() - pads.getRight();
    CHECK (padGap > 0);
    CHECK (record.getRight() < tempo.getX());
    CHECK (referenceGap > 0);
    CHECK (metro.getX() - tempo.getRight() == referenceGap);
    CHECK (play.getX() - metro.getRight() == referenceGap);
    CHECK (menu.getX() - pattern.getRight() == referenceGap);
    CHECK (play.getRight() < pattern.getX());
    capturePhone (editor, "iphone-32-minimum");
    phoneButton (editor, "Open menu").onClick();
    for (const auto* page : { "Project", "Feel", "Super Secret", "Settings", "About" })
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
    auto& superSecret = phoneButton (editor, "Super Secret");
    CHECK (midiOutput.getY() == phoneButton (editor, "About").getY());
    midiOutput.onClick();
    CHECK (openedMidi);
    phoneButton (editor, "Close menu").onClick();
    CHECK_FALSE (superSecret.isVisible());
    generate.onClick();
    juce::Viewport* generatorViewport = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* viewport = dynamic_cast<juce::Viewport*> (child); viewport != nullptr && viewport->isVisible())
            generatorViewport = viewport;
    REQUIRE (generatorViewport != nullptr);
    auto* generator = dynamic_cast<PreviewPanel*> (generatorViewport->getViewedComponent());
    REQUIRE (generator != nullptr);
    REQUIRE (generator->isXYRandomizer());
    PatternXYPad* pad = nullptr;
    ValueControl* patternLength = nullptr;
    for (auto* child : generator->getChildren())
    {
        if (auto* xy = dynamic_cast<PatternXYPad*> (child))
            pad = xy;
        if (child->getTitle() == "Randomizer pattern length")
            patternLength = dynamic_cast<ValueControl*> (child);
    }
    REQUIRE (pad != nullptr);
    REQUIRE (patternLength != nullptr);
    CHECK_FALSE (patternLength->isVisible());
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
    CHECK_FALSE (patternLength->isVisible());
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
    REQUIRE (superSecret.isVisible());
    superSecret.onClick();
    CHECK_FALSE (pad->isVisible());
    CHECK (patternLength->isVisible());
    CHECK (generatorViewport->getViewArea().contains (patternLength->getBounds()));
    CHECK (patternLength->value == 16);
    setLength (*patternLength, 36);
    CHECK (editor.controller.state.playback.generatedStepCount == 36);
    CHECK (editor.controller.state.playback.stepCount == 64);
    editor.controller.undo();
    CHECK (patternLength->value == 16);
    editor.controller.redo();
    CHECK (patternLength->value == 36);
    editor.controller.command ("cell", { 1, 1, 36, 1, 96 });
    phoneButton (editor, "Random mode").onClick();
    CHECK (editor.controller.state.playback.generated[0][35].enabled);
    CHECK (editor.controller.state.playback.generated[0][35].velocity == 96);
    CHECK (phoneButton (editor, "Random mode").getX() > generator->getWidth() / 2);
    phoneButton (editor, "Settings").onClick();
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
    CHECK (record.getX() - pads.getRight() > padGap);

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
    CHECK (velocityMode.getWidth() == (compact ? 56 : 64));
    CHECK (velocityMode.getHeight() == (compact ? 56 : phoneButton (editor, "Trigger channel 8").getHeight()));
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
