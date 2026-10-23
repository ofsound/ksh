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

    juce::MouseEvent phoneEvent (juce::Component& component, juce::Point<float> point, juce::Point<float> origin)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), point, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, &component, &component, now, origin, now, 1, point.getDistanceFrom (origin) > 0 };
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
    phoneButton (editor, "Open controls").onClick();
    CHECK_FALSE (grid.isVisible());
    for (const auto* tab : { "Project", "Pattern", "Feel", "Generate", "View", "Help" })
    {
        phoneButton (editor, tab).onClick();
        for (auto* child : editor.getChildren())
            if (child->isVisible())
                CHECK (editor.getLocalBounds().contains (child->getBounds()));
        capturePhone (editor, "iphone-panel-" + juce::String (tab).toLowerCase());
    }
    phoneButton (editor, "Done").onClick();
    CHECK (grid.isVisible());
    grid.channelSettings (1);
    CHECK_FALSE (grid.isVisible());
    capturePhone (editor, "iphone-panel-channel");
    phoneButton (editor, "Done").onClick();
    phoneButton (editor, "Pattern bank").onClick();
    capturePhone (editor, "iphone-pattern-bank");
    phoneButton (editor, "Choose pattern 2").onClick();
    CHECK (editor.controller.source() == 1);
    CHECK (grid.isVisible());
    editor.controller.selectLatched = true;
    phoneButton (editor, "Pattern bank").onClick();
    phoneButton (editor, "Choose pattern 1").onClick();
    CHECK (editor.controller.source() == 0);
    CHECK (editor.controller.copySource == -1);
    CHECK (grid.isVisible());
    phoneButton (editor, "Pattern bank").onClick();
    phoneButton (editor, "Duplicate pattern").onClick();
    phoneButton (editor, "Choose pattern 3").onClick();
    CHECK (editor.controller.source() == 0);
    CHECK (editor.controller.copySource == -1);
    capturePhone (editor, "iphone-pattern-copy");
    phoneButton (editor, "Duplicate pattern").onClick();
    CHECK (editor.controller.copySource == -1);
    CHECK (editor.controller.source() == 2);
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    CHECK (grid.isVisible());

    editor.setCompactViewport (false, 1338, 900);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    CHECK (phoneButton (editor, "NEW").isVisible());
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Open controls" || child->getTitle() == "Pattern picker")
            CHECK_FALSE (child->isVisible());
    CHECK (grid.layout.gridLeft() == 346.0f);
}

TEST_CASE ("iPhone second step page edits the correct cells and cancels a resized gesture", "[native-ui][iphone]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, 32 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 409);
    editor.setSize (814, 409);
    auto& grid = phoneGrid (editor);
    phoneButton (editor, "Switch step page").onClick();
    REQUIRE (grid.layout.firstStep == 16);
    REQUIRE (grid.layout.at (grid.layout.cell ({ 0, 16 }).getCentre()) == Location { 0, 16 });
    CHECK_FALSE (grid.layout.at ({ grid.layout.gridLeft() - 1, grid.layout.cell ({ 0, 16 }).getCentreY() }).valid());
    auto tap = [&] (Location location)
    {
        const auto point = grid.layout.cell (location).getCentre();
        grid.mouseDown (phoneEvent (grid, point, point));
        grid.mouseUp (phoneEvent (grid, point, point));
    };
    tap ({ 0, 16 });
    CHECK (editor.controller.cell ({ 0, 16 }).enabled);
    CHECK_FALSE (editor.controller.cell ({ 0, 0 }).enabled);
    editor.controller.undo();
    CHECK_FALSE (editor.controller.cell ({ 0, 16 }).enabled);
    const auto first = grid.layout.cell ({ 0, 16 }).getCentre();
    const auto last = grid.layout.cell ({ 0, 19 }).getCentre();
    grid.mouseDown (phoneEvent (grid, first, first));
    grid.mouseDrag (phoneEvent (grid, last, first));
    REQUIRE (editor.controller.editing());
    editor.setCompactViewport (true, 850, 409);
    grid.mouseUp (phoneEvent (grid, last, first));
    CHECK_FALSE (editor.controller.editing());
    CHECK_FALSE (editor.controller.cell ({ 0, 16 }).enabled);
    CHECK_FALSE (editor.controller.cell ({ 0, 19 }).enabled);
    CHECK_FALSE (editor.controller.canUndo());
    editor.controller.command ("source_steps", { 1, 16 }, "Shorten");
    CHECK (grid.layout.firstStep == 0);
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Switch step page")
            CHECK_FALSE (child->isVisible());
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

TEST_CASE ("iPhone Copy toggle duplicates selections without a keyboard modifier", "[native-ui][iphone]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 87, 75, 3, 5, 2 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 409);
    editor.setSize (814, 409);
    editor.controller.selected.set (0);
    editor.sync();
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

TEST_CASE ("iPhone pattern navigation never borrows selection mode and copy can be canceled or undone", "[native-ui][iphone]")
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
    CHECK (grid.isVisible());
    phoneButton (editor, "Choose pattern 3").onClick();
    CHECK (editor.controller.source() == 2);
    CHECK (editor.controller.copySource == -1);
    CHECK (editor.controller.cell ({ 0, 0 }).velocity == 44);
    phoneButton (editor, "Pattern bank").onClick();
    phoneButton (editor, "Duplicate pattern").onClick();
    phoneButton (editor, "Choose pattern 1").onClick();
    CHECK (editor.controller.source() == 2);
    phoneButton (editor, "Close pattern picker").onClick();
    CHECK (editor.controller.copySource == -1);
    phoneButton (editor, "Pattern bank").onClick();
    phoneButton (editor, "Choose pattern 1").onClick();
    CHECK (editor.controller.cell ({ 0, 0 }).velocity == 111);
    phoneButton (editor, "Pattern bank").onClick();
    phoneButton (editor, "Duplicate pattern").onClick();
    phoneButton (editor, "Choose pattern 3").onClick();
    phoneButton (editor, "Duplicate pattern").onClick();
    CHECK (editor.controller.cell ({ 0, 0 }).velocity == 111);
    editor.controller.undo();
    CHECK (editor.controller.cell ({ 0, 0 }).velocity == 44);
    phoneButton (editor, "Pattern bank").onClick();
    phoneButton (editor, "Duplicate pattern").onClick();
    processor.resetProject();
    editor.controller.poll();
    editor.sync();
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Pattern picker")
            CHECK_FALSE (child->isVisible());
    CHECK (editor.controller.copySource == -1);
}

TEST_CASE ("iPhone settings remember context and pattern clearing requires the same pattern twice", "[native-ui][iphone]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 2, 1, 1, 1, 90 }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 390);
    editor.setSize (814, 390);
    CHECK (phoneButton (editor, "Arm Record").isVisible());
    phoneButton (editor, "Open controls").onClick();
    CHECK (phoneButton (editor, "Clear pattern").isVisible());
    phoneButton (editor, "Clear pattern").onClick();
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    editor.controller.selectSource (1);
    phoneButton (editor, "Clear pattern").onClick();
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    phoneButton (editor, "Clear pattern").onClick();
    CHECK_FALSE (editor.controller.cell ({ 0, 0 }).enabled);
    editor.controller.undo();
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    phoneButton (editor, "Feel").onClick();
    phoneButton (editor, "Done").onClick();
    phoneButton (editor, "Open controls").onClick();
    CHECK (phoneButton (editor, "Feel").getToggleState());
    phoneButton (editor, "Done").onClick();
    phoneGrid (editor).channelSettings (1);
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Open controls" || dynamic_cast<PreviewPanel*> (child) != nullptr)
            CHECK_FALSE (child->isVisible());
    phoneButton (editor, "Done").onClick();
    phoneButton (editor, "Open controls").onClick();
    CHECK (phoneButton (editor, "Feel").getToggleState());
}

TEST_CASE ("iPhone toolbar avoids overlapping page recording and selection controls at its minimum size", "[native-ui][iphone][render]")
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
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
    capturePhone (editor, "iphone-32-minimum");
}

TEST_CASE ("iOS numbered row pads fit beside names without reducing grid cells", "[native-ui][iphone][ipad][row-trigger][render]")
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
    for (int ch = 0; ch < 8; ++ch)
    {
        auto& trigger = phoneButton (editor, "Trigger channel " + juce::String (ch + 1));
        CHECK (trigger.getButtonText() == juce::String (ch + 1));
        CHECK (trigger.getRight() < grid.layout.gridLeft());
        CHECK (grid.getLocalBounds().contains (trigger.getBounds()));
        CHECK (std::abs (static_cast<float> (trigger.getBounds().getCentreY()) - grid.layout.cell ({ ch, 0 }).getCentreY()) <= 1.0f);
        for (auto* sibling : grid.getChildren())
            if (sibling != &trigger && sibling->isVisible())
                CHECK_FALSE (trigger.getBounds().intersects (sibling->getBounds()));
    }
    if (compact)
    {
        CHECK (grid.layout.cellWidth() >= 42.0f);
        CHECK (grid.layout.cellHeight() >= 33.0f);
    }
    phoneButton (editor, "Trigger channel 1").onClick();
    capturePhone (editor, compact ? "iphone-row-triggers" : "ipad-row-triggers");
    REQUIRE (processor.dispatchUiEngineCommand ("channels", { 3 }));
    editor.controller.poll();
    editor.sync();
    int visible = 0;
    for (auto* child : grid.getChildren())
        if (auto* pad = dynamic_cast<RowTriggerButton*> (child))
        {
            CHECK (pad->isVisible() == (pad->getButtonText().getIntValue() <= 3));
            visible += pad->isVisible() ? 1 : 0;
        }
    CHECK (visible == 3);
    editor.layout.touchRows = false;
    editor.setCompactViewport (false, 1338, 900);
    editor.sync();
    for (auto* child : grid.getChildren())
        if (auto* pad = dynamic_cast<RowTriggerButton*> (child))
            CHECK_FALSE (pad->isVisible());
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
    editor.controller.command ("pattern_record_enabled", { 1, 2 });
    juce::AudioBuffer<float> audio (2, 64);
    juce::MidiBuffer midi;
    processor.processBlock (audio, midi);
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
    auto& rowsButton = phoneButton (editor, "Visible rows");
    CHECK (rowsButton.getButtonText() == "Rows: 8");
    juce::ScrollBar* scroll = nullptr;
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "Scroll rows") scroll = dynamic_cast<juce::ScrollBar*> (child);
    REQUIRE (scroll != nullptr);
    CHECK_FALSE (scroll->isVisible());
    for (const int count : { 1, 2, 4, 6, 8 })
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
    phoneButton (editor, "Open controls").onClick();
    phoneButton (editor, "View").onClick();
    CHECK (phoneButton (editor, "Visible rows").isVisible());
    editor.setCompactViewport (true, 814, 409, false);
    editor.setCompactVisibleRows (2);
    CHECK (editor.layout.compactVisibleRows == 8);
    CHECK_FALSE (rowsButton.isVisible());
    CHECK_FALSE (scroll->isVisible());
    editor.setCompactViewport (false, 1338, 900);
    CHECK_FALSE (rowsButton.isVisible());
    CHECK (editor.layout.firstRow == 0);
}
