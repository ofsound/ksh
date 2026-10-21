#include <PluginEditor.h>
#include <catch2/catch_test_macros.hpp>

using namespace ksh;
using namespace ksh::ui;

namespace
{
    juce::TextButton& phoneButton (NativeEditor& editor, const juce::String& label)
    {
        for (auto* child : editor.getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && (button->getTitle() == label || button->getButtonText() == label))
                return *button;
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
    CHECK (grid.layout.cellHeight() >= 29.0f);
    CHECK (grid.getBottom() + 84 == editor.getHeight());
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
            CHECK (inspector->getHeight() == 84);
            for (auto* control : inspector->getChildren())
                if (control->isVisible())
                    CHECK (inspector->getLocalBounds().contains (control->getBounds()));
        }
    }
    capturePhone (editor, "iphone-16-inspector");
    phoneButton (editor, "Open controls").onClick();
    CHECK_FALSE (grid.isVisible());
    for (const auto* tab : { "Project", "Pattern", "Feel", "Generate", "Channel", "Help" })
    {
        phoneButton (editor, tab).onClick();
        for (auto* child : editor.getChildren())
            if (child->isVisible())
                CHECK (editor.getLocalBounds().contains (child->getBounds()));
        capturePhone (editor, "iphone-panel-" + juce::String (tab).toLowerCase());
    }
    phoneButton (editor, "Done").onClick();
    CHECK (grid.isVisible());
    phoneButton (editor, "Pattern bank").onClick();
    capturePhone (editor, "iphone-pattern-bank");
    phoneButton (editor, "2").onClick();
    CHECK (editor.controller.source() == 1);
    CHECK (grid.isVisible());
    editor.controller.selectLatched = true;
    phoneButton (editor, "Pattern bank").onClick();
    phoneButton (editor, "1").onClick();
    REQUIRE (editor.controller.copySource == 0);
    CHECK_FALSE (editor.controller.selectLatched);
    CHECK_FALSE (grid.isVisible());
    phoneButton (editor, "3").onClick();
    CHECK (editor.controller.copySource == -1);
    CHECK (editor.controller.source() == 2);
    CHECK (editor.controller.cell ({ 0, 0 }).enabled);
    CHECK (grid.isVisible());

    editor.setCompactViewport (false, 1338, 900);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    CHECK (phoneButton (editor, "NEW").isVisible());
    CHECK_FALSE (phoneButton (editor, "Open controls").isVisible());
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
    CHECK_FALSE (phoneButton (editor, "Switch step page").isVisible());
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
