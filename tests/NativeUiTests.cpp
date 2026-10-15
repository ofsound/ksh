#include <PluginEditor.h>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>
#include <ui/KshEditorController.h>
#include <ui/KshLayout.h>

using namespace ksh;
using namespace ksh::ui;

TEST_CASE ("Native batch edits are atomic and reject stale cells", "[native-ui]")
{
    PluginProcessor processor;
    auto state = processor.nativeUiState();
    Cell active;
    active.enabled = true;
    std::vector<PluginProcessor::CellEdit> edits { { 0, 0, 0, {}, active }, { 0, 1, 4, {}, active } };
    REQUIRE (processor.applyNativeCellEdits (edits, state.documentEpoch));
    auto after = processor.nativeUiState();
    CHECK (after.playback.sources[0][0][0].enabled);
    CHECK (after.playback.sources[0][1][4].enabled);
    CHECK (after.revision > state.revision);
    edits[0] = { 0, 2, 0, {}, active };
    REQUIRE_FALSE (processor.applyNativeCellEdits (edits, state.documentEpoch));
    CHECK_FALSE (processor.nativeUiState().playback.sources[0][2][0].enabled);
    processor.resetProject();
    REQUIRE_FALSE (processor.applyNativeCellEdits ({ { 0, 0, 0, {}, active } }, state.documentEpoch));
}

TEST_CASE ("Native gestures undo once and preserve unrelated recorded cells", "[native-ui]")
{
    PluginProcessor processor;
    EditorController editor (processor);
    editor.begin ("Paint");
    for (int s : { 0, 4, 8, 12 })
    {
        Cell c;
        c.enabled = true;
        editor.editCell ({ 0, s }, c);
    }
    editor.flush();
    Cell extra;
    extra.enabled = true;
    extra.velocity = 72;
    REQUIRE (processor.applyNativeCellEdits ({ { 0, 3, 1, {}, extra } }, editor.state.documentEpoch));
    editor.commit();
    REQUIRE (editor.canUndo());
    editor.undo();
    CHECK_FALSE (editor.cell ({ 0, 0 }).enabled);
    CHECK_FALSE (editor.cell ({ 0, 12 }).enabled);
    CHECK (editor.cell ({ 3, 1 }).velocity == 72);
    CHECK (editor.cell ({ 3, 1 }).enabled);
    CHECK_FALSE (editor.canUndo());
    editor.redo();
    CHECK (editor.cell ({ 0, 4 }).enabled);
    CHECK (editor.cell ({ 3, 1 }).enabled);
}

TEST_CASE ("Native cancelled gesture restores only its own fields", "[native-ui]")
{
    PluginProcessor processor;
    EditorController editor (processor);
    editor.begin ("Draw");
    Cell c;
    c.enabled = true;
    editor.editCell ({ 0, 0 }, c);
    editor.flush();
    REQUIRE (processor.dispatchUiEngineCommand ("channel_note", { 2, 60 }));
    editor.cancel();
    CHECK_FALSE (editor.cell ({ 0, 0 }).enabled);
    CHECK (editor.state.playback.channels[1].note == 60);
    CHECK_FALSE (editor.canUndo());
}

TEST_CASE ("Native draft yields to recording and document restoration", "[native-ui]")
{
    PluginProcessor processor;
    EditorController editor (processor);
    editor.begin ("Draw");
    Cell draft;
    draft.enabled = true;
    draft.velocity = 33;
    editor.editCell ({ 0, 0 }, draft);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 119, 100, 1, 1, 1 }));
    editor.flush();
    CHECK (editor.cell ({ 0, 0 }).velocity == 119);
    CHECK_FALSE (editor.editing());
    editor.begin ("Draft after recording");
    editor.editCell ({ 0, 1 }, draft);
    processor.resetProject();
    editor.poll();
    CHECK_FALSE (editor.cell ({ 0, 1 }).enabled);
    CHECK_FALSE (editor.canUndo());
}

TEST_CASE ("Native move and copy preserve wrapped stencil and overlap", "[native-ui]")
{
    PluginProcessor processor;
    EditorController editor (processor);
    editor.begin ("Setup");
    Cell a;
    a.enabled = true;
    a.velocity = 87;
    Cell b = a;
    b.velocity = 42;
    editor.editCell ({ 0, 14 }, a);
    editor.editCell ({ 0, 15 }, b);
    editor.commit();
    editor.selected.set (14);
    editor.selected.set (15);
    editor.begin ("Move");
    editor.moveSelection ({ 0, 14 }, { 7, 15 }, false);
    editor.commit();
    CHECK_FALSE (editor.cell ({ 0, 14 }).enabled);
    CHECK (editor.cell ({ 7, 15 }).velocity == 87);
    CHECK (editor.cell ({ 7, 0 }).velocity == 42);
    editor.undo();
    CHECK (editor.cell ({ 0, 14 }).velocity == 87);
    editor.selected.reset();
    editor.selected.set (14);
    editor.selected.set (15);
    editor.begin ("Copy");
    editor.moveSelection ({ 0, 14 }, { 0, 15 }, true);
    editor.commit();
    CHECK (editor.cell ({ 0, 14 }).velocity == 87);
    CHECK (editor.cell ({ 0, 15 }).velocity == 87);
    CHECK (editor.cell ({ 0, 0 }).velocity == 42);
}

TEST_CASE ("Native layout and gesture constants match reference", "[native-ui]")
{
    CHECK (dragOffset (4, 2) == 0);
    CHECK (dragOffset (5, 2) == 1);
    CHECK (dragOffset (-5, 2) == -1);
    CHECK (jsRound (-0.5) == 0);
    CHECK (jsRound (0.5) == 1);
    for (int steps : { 1, 16, 32 })
        for (float scale : { 1.0f, 1.5f })
        {
            Layout layout;
            layout.steps = steps;
            layout.patternScale = scale;
            for (int ch = 0; ch < 8; ++ch)
                for (int s = 0; s < steps; ++s)
                {
                    const Location p { ch, s };
                    CHECK (layout.at (layout.cell (p).getCentre()) == p);
                    CHECK (layout.cell (p).getRight() < layout.width());
                }
        }
    Cell c;
    c.velocity = 120;
    c.cycle = 64;
    c.cycleMask = 7;
    CHECK (withValue (c, Layer::velocity, 140).velocity == 127);
    CHECK (withValue (c, Layer::chance, -10).probability == 0);
    CHECK (withValue (c, Layer::chance, 2).cycleMask == c.cycleMask);
}

TEST_CASE ("Native editor constructs renders and tears down without a browser", "[native-ui][render]")
{
    PluginProcessor processor;
    processor.setProjectMetadata ("Untitled Project", "", "2026-10-14T12:00:00Z", "2026-10-14T23:15:00Z");
    for (int ch = 0; ch < 4; ++ch)
        for (int s = 0; s < 16; ++s)
            if ((s + ch) % (ch + 2) == 0)
                REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, ch + 1, s + 1, 1, 100 - s, 100, 1, 1, 1 }));
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        REQUIRE (editor->getWidth() > 600);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        REQUIRE (image.isValid());
        CHECK (image.getPixelAt (2, 2) == Palette::app);
        const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
        if (iteration == 0 && directory.isNotEmpty())
        {
            juce::File root (directory);
            REQUIRE (root.createDirectory().wasOk());
            auto stream = root.getChildFile ("native-16.png").createOutputStream();
            REQUIRE (stream != nullptr);
            REQUIRE (juce::PNGImageFormat().writeImageToStream (image, *stream));
        }
    }
}

namespace
{
    juce::MouseEvent gridEvent (PatternGrid& grid, juce::Point<float> position, juce::Point<float> origin, int modifiers, bool dragged = false)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &grid, &grid, now, origin, now, 1, dragged };
    }
}

TEST_CASE ("Native mouse gestures respect thresholds cancellation and one-step undo", "[native-ui]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const auto start = grid.layout.cell ({ 0, 0 }).getCentre();
    auto down = gridEvent (grid, start, start, juce::ModifierKeys::leftButtonModifier);
    grid.mouseDown (down);
    grid.mouseUp (down);
    REQUIRE (controller.cell ({ 0, 0 }).enabled);
    controller.undo();
    REQUIRE_FALSE (controller.cell ({ 0, 0 }).enabled);
    grid.mouseDown (down);
    grid.mouseDrag (gridEvent (grid, grid.layout.cell ({ 0, 4 }).getCentre(), start, juce::ModifierKeys::leftButtonModifier, true));
    grid.mouseUp (down);
    for (int s = 0; s <= 4; ++s)
        CHECK (controller.cell ({ 0, s }).enabled);
    controller.undo();
    for (int s = 0; s <= 4; ++s)
        CHECK_FALSE (controller.cell ({ 0, s }).enabled);
    CHECK_FALSE (controller.canUndo());
    grid.mouseDown (down);
    grid.mouseDrag (gridEvent (grid, start.translated (0, -20), start, juce::ModifierKeys::leftButtonModifier, true));
    CHECK (controller.cell ({ 0, 0 }).velocity == 108);
    grid.cancelGesture();
    CHECK_FALSE (controller.cell ({ 0, 0 }).enabled);
    bool opened = false;
    grid.showCycle = [&] (Location location, juce::Rectangle<int>) { opened = location == Location { 0, 0 }; };
    grid.mouseDown (gridEvent (grid, start, start, juce::ModifierKeys::rightButtonModifier));
    CHECK (opened);
}

TEST_CASE ("Grid clicks outside a selection deselect without editing cells", "[native-ui]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const Location selected { 0, 0 }, other { 0, 4 };
    const auto point = grid.layout.cell (other).getCentre();
    const int left = juce::ModifierKeys::leftButtonModifier;
    auto click = [&] (juce::Point<float> position, int modifiers)
    {
        const auto event = gridEvent (grid, position, position, modifiers);
        grid.mouseDown (event);
        grid.mouseUp (event);
    };

    SECTION ("Enabled and empty cells only clear selection")
    {
        const bool enabled = GENERATE (false, true);
        if (enabled)
            click (point, left);
        controller.selected.set (static_cast<size_t> (selected.key()));
        click (point, left);
        CHECK (controller.selected.none());
        CHECK (controller.cell (other).enabled == enabled);
    }
    SECTION ("Deselecting does not begin a paint or value drag")
    {
        controller.selected.set (static_cast<size_t> (selected.key()));
        grid.mouseDown (gridEvent (grid, point, point, left));
        grid.mouseDrag (gridEvent (grid, grid.layout.cell ({ 0, 6 }).getCentre(), point, left, true));
        grid.mouseUp (gridEvent (grid, point, point, left));
        CHECK (controller.selected.none());
        for (int step = 4; step <= 6; ++step)
            CHECK_FALSE (controller.cell ({ 0, step }).enabled);
        CHECK_FALSE (controller.canUndo());
    }
    SECTION ("Shift-click adds and removes selection without editing cells")
    {
        controller.selected.set (static_cast<size_t> (selected.key()));
        click (point, left | juce::ModifierKeys::shiftModifier);
        CHECK (controller.selected.count() == 2);
        CHECK (controller.selected.test (static_cast<size_t> (other.key())));
        CHECK_FALSE (controller.cell (other).enabled);
        click (point, left | juce::ModifierKeys::shiftModifier);
        CHECK (controller.selected.count() == 1);
        CHECK (controller.selected.test (static_cast<size_t> (selected.key())));
        CHECK_FALSE (controller.cell (other).enabled);
        CHECK_FALSE (controller.canUndo());
    }
}

TEST_CASE ("Host replacement invalidates undo before the next timer tick", "[native-ui]")
{
    PluginProcessor processor;
    EditorController editor (processor);
    editor.begin ("Edit");
    Cell c;
    c.enabled = true;
    editor.editCell ({ 0, 0 }, c);
    editor.commit();
    processor.resetProject();
    editor.undo();
    CHECK_FALSE (editor.canUndo());
    CHECK_FALSE (editor.canRedo());
    CHECK_FALSE (editor.cell ({ 0, 0 }).enabled);
}

TEST_CASE ("Native dense layouts render selection cycles and scaling", "[native-ui][render]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, 32 }));
    for (int ch = 0; ch < 8; ++ch)
        for (int step = 0; step < 32; ++step)
            REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, ch + 1, step + 1, step % 3 != 1 ? 1 : 0, 50 + step * 2, 60 + step, 4, 5, 1 << (step % 4) }));
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    for (int scale : { 50, 100, 120 })
    for (auto layer : { Layer::velocity, Layer::chance })
    {
        processor.setProjectUiScalePercent (scale);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        auto* view = dynamic_cast<NativeEditor*> (editor->getChildComponent (0));
        REQUIRE (view != nullptr);
        view->controller.selected.set (0);
        view->controller.selected.set (2);
        view->controller.layer = layer;
        view->sync();
        auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        REQUIRE (image.isValid());
        CHECK (editor->getWidth() == juce::roundToInt (1338.0f * static_cast<float> (scale) / 100));
        if (directory.isNotEmpty())
        {
            auto stream = juce::File (directory).getChildFile ("native-32-" + juce::String (scale) + "-" + juce::String (static_cast<int> (layer)) + ".png").createOutputStream();
            REQUIRE (stream != nullptr);
            REQUIRE (juce::PNGImageFormat().writeImageToStream (image, *stream));
        }
    }
}

TEST_CASE ("Random controls overlay the grid without resizing the editor", "[native-ui][render]")
{
    PluginProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* view = dynamic_cast<NativeEditor*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    juce::TextButton* toggle = nullptr;
    PatternGrid* grid = nullptr;
    PreviewPanel* panel = nullptr;
    for (auto* child : view->getChildren())
    {
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "Random")
            toggle = button;
        if (auto* candidate = dynamic_cast<PatternGrid*> (child))
            grid = candidate;
        if (auto* candidate = dynamic_cast<PreviewPanel*> (child); candidate && !candidate->isVisible())
            panel = candidate;
    }
    REQUIRE (toggle != nullptr);
    REQUIRE (grid != nullptr);
    REQUIRE (panel != nullptr);
    const auto editorBounds = editor->getBounds();
    const auto gridBounds = grid->getBounds();
    toggle->onClick();
    CHECK (panel->isVisible());
    CHECK (panel->getBounds().intersects (gridBounds));
    CHECK (panel->getX() == 0);
    CHECK (panel->getRight() == view->getWidth());
    CHECK (panel->getBottom() == view->getHeight());
    CHECK (toggle->getBottom() <= view->layout.toolbarY());
    CHECK (view->getIndexOfChildComponent (panel) > view->getIndexOfChildComponent (grid));
    CHECK (grid->getBounds() == gridBounds);
    CHECK (editor->getBounds() == editorBounds);
    toggle->onClick();
    CHECK_FALSE (panel->isVisible());
    CHECK (grid->getBounds() == gridBounds);
}

TEST_CASE ("Selected value controls temporarily switch the editing layer", "[native-ui]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    PreviewPanel panel (controller);
    for (auto* child : panel.getChildren())
    {
        auto* control = dynamic_cast<ValueControl*> (child);
        if (control == nullptr || (control->caption != "Velocity" && control->caption != "Probability" && control->caption != "Roll"))
            continue;

        for (auto original : { Layer::velocity, Layer::chance })
        {
            controller.layer = original;
            control->onBegin();
            const auto expected = control->caption == "Velocity" ? Layer::velocity : control->caption == "Probability" ? Layer::chance : original;
            CHECK (controller.layer == expected);
            control->onEnd();
            CHECK (controller.layer == original);
        }
    }
}

TEST_CASE ("Roll is edited in the inspector while grid layers remain velocity and chance", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 80, 4, 5, 1 }));
    NativeEditor editor (processor);
    for (auto [key, layer] : { std::pair { '1', Layer::velocity }, std::pair { '2', Layer::chance } })
    {
        CHECK (editor.keyPressed (juce::KeyPress (key, {}, static_cast<juce::juce_wchar> (key))));
        CHECK (editor.controller.layer == layer);
    }
    CHECK_FALSE (editor.keyPressed (juce::KeyPress ('3', {}, '3')));
    CHECK_FALSE (editor.keyPressed (juce::KeyPress ('4', {}, '4')));
    CHECK (editor.controller.layer == Layer::chance);
    for (auto* child : editor.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getProperties().contains ("heading"))
        {
            CHECK (button->getButtonText().contains ("Velocity / Chance"));
            CHECK (button->getToggleState());
            button->onClick();
            CHECK (editor.controller.layer == Layer::velocity);
            CHECK_FALSE (button->getToggleState());
            CHECK (button->getButtonText().contains ("Velocity / Chance"));
        }

    auto& controller = editor.controller;
    controller.selected.set (0);
    const auto before = controller.cell ({ 0, 0 });
    PreviewPanel inspector (controller);
    ValueControl* roll = nullptr;
    for (auto* child : inspector.getChildren())
        if (auto* control = dynamic_cast<ValueControl*> (child); control && control->caption == "Roll")
            roll = control;
    REQUIRE (roll != nullptr);
    for (auto layer : { Layer::velocity, Layer::chance })
    {
        controller.layer = layer;
        roll->onBegin();
        roll->onChange (4);
        roll->onEnd();
        CHECK (controller.layer == layer);
        auto expected = before;
        expected.roll = 4;
        CHECK (controller.cell ({ 0, 0 }) == expected);
        controller.undo();
        CHECK (controller.cell ({ 0, 0 }) == before);
    }
}

TEST_CASE ("Chance grid drags adjust probability and preserve cycle and roll", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 60, 4, 5, 3 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 2, 1, 70, 80, 3, 2, 4 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    controller.layer = Layer::chance;
    const auto before = controller.cell ({ 0, 0 });
    const auto second = controller.cell ({ 0, 1 });
    const bool selected = GENERATE (false, true);
    if (selected)
    {
        controller.selected.set (0);
        controller.selected.set (1);
    }
    const auto start = grid.layout.cell ({ 0, 0 }).getCentre();
    const int modifiers = juce::ModifierKeys::leftButtonModifier | (selected ? juce::ModifierKeys::shiftModifier : 0);
    const auto down = gridEvent (grid, start, start, modifiers);
    grid.mouseDown (down);
    grid.mouseDrag (gridEvent (grid, start.translated (0, -20), start, modifiers, true));
    grid.mouseUp (down);
    auto expected = before;
    expected.probability += 8;
    CHECK (controller.cell ({ 0, 0 }) == expected);
    auto expectedSecond = second;
    if (selected)
        expectedSecond.probability += 8;
    CHECK (controller.cell ({ 0, 1 }) == expectedSecond);
    controller.undo();
    CHECK (controller.cell ({ 0, 0 }) == before);
    CHECK (controller.cell ({ 0, 1 }) == second);
}

TEST_CASE ("Inspector cycle edits temporarily show Chance", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 60, 4, 5, 3 }));
    EditorController controller (processor);
    controller.selected.set (0);
    PreviewPanel inspector (controller);
    CycleEditor* cycle = nullptr;
    for (auto* child : inspector.getChildren())
        if (auto* editor = dynamic_cast<CycleEditor*> (child))
            cycle = editor;
    REQUIRE (cycle != nullptr);
    cycle->sync();
    bool sawChance = false;
    controller.changed = [&] { sawChance = sawChance || controller.layer == Layer::chance; };
    juce::Component* handle = nullptr;
    for (auto* child : cycle->getChildren())
        if (child->getTitle() == "Cycle length")
            handle = child;
    REQUIRE (handle != nullptr);
    CHECK (handle->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (sawChance);
    CHECK (controller.layer == Layer::velocity);
    CHECK (controller.cell ({ 0, 0 }).cycle == 5);
    CHECK (controller.cell ({ 0, 0 }).probability == 60);
    controller.undo();
    CHECK (controller.cell ({ 0, 0 }).cycle == 4);
    controller.changed = {};
}
