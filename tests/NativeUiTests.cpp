#include <PluginEditor.h>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <ui/KshEditorController.h>
#include <ui/KshLayout.h>
#include <engine/KshMath.h>
#include "../standalone/StandalonePreferences.h"

using namespace ksh;
using namespace ksh::ui;

TEST_CASE ("Numeric controls repaint their outlines when focus and hover change", "[native-ui]")
{
    struct RepaintTracker : juce::CachedComponentImage
    {
        int invalidations = 0;
        void paint (juce::Graphics&) override {}
        bool invalidateAll() override { ++invalidations; return true; }
        bool invalidate (const juce::Rectangle<int>&) override { ++invalidations; return true; }
        void releaseResources() override {}
    };
    ValueControl control;
    control.setBounds (0, 0, 100, 42);
    control.setVisible (true);
    auto* tracker = new RepaintTracker;
    control.setCachedComponentImage (tracker);
    auto expectRepaint = [&] (auto callback)
    {
        const auto before = tracker->invalidations;
        callback();
        CHECK (tracker->invalidations > before);
    };
    expectRepaint ([&] { control.focusGained (juce::Component::focusChangedByTabKey); });
    expectRepaint ([&] { control.focusLost (juce::Component::focusChangedByMouseClick); });
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 1, 0, 0, 0, 0,
                                  &control, &control, now, {}, now, 0, false);
    expectRepaint ([&] { control.mouseEnter (event); });
    expectRepaint ([&] { control.mouseExit (event); });
}

TEST_CASE ("Fullscreen standalone fits small viewports despite saved zoom", "[native-ui][render]")
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    processor.setProjectUiScalePercent (120);
    PluginEditor editor (processor, true);
    auto* view = dynamic_cast<NativeEditor*> (editor.getChildComponent (0));
    REQUIRE (view != nullptr);
    CHECK_FALSE (editor.isResizable());
    for (const auto viewport : { juce::Rectangle<int> (1024, 768), juce::Rectangle<int> (768, 1024), juce::Rectangle<int> (320, 700) })
    {
        editor.setBounds (viewport);
        for (const int savedScale : { 50, 100, 120 })
        {
            processor.setProjectUiScalePercent (savedScale);
            view->sync();
            CHECK (editor.getBounds() == viewport);
            CHECK (processor.getProjectUiScalePercent() == savedScale);
            const auto content = view->getLocalBounds().toFloat().transformedBy (view->getTransform());
            CHECK (content.getX() >= -0.01f);
            CHECK (content.getY() >= -0.01f);
            CHECK (content.getRight() <= static_cast<float> (editor.getWidth()) + 0.01f);
            CHECK (content.getBottom() <= static_cast<float> (editor.getHeight()) + 0.01f);
        }
        const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
        if (directory.isNotEmpty())
        {
            auto stream = juce::File (directory).getChildFile ("native-ipad-fit-" + juce::String (viewport.getWidth()) + ".png").createOutputStream();
            REQUIRE (stream != nullptr);
            const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
            REQUIRE (juce::PNGImageFormat().writeImageToStream (image, *stream));
        }
    }
}

TEST_CASE ("Standalone transport swaps Play and Stop in one position with clicks and Space", "[native-ui][render]")
{
    const bool compact = GENERATE (false, true);
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    auto editor = std::make_unique<PluginEditor> (processor, compact);
    if (compact)
        editor->setSize (814, 409);
    auto* view = dynamic_cast<NativeEditor*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    juce::TextButton* play = nullptr;
    juce::TextButton* stop = nullptr;
    for (auto* child : view->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getProperties().contains ("transport"))
        {
            if (button->getTitle() == "Play")
                play = button;
            else if (button->getTitle() == "Stop")
                stop = button;
        }
    REQUIRE (play != nullptr);
    REQUIRE (stop != nullptr);
    const auto checkTransport = [&] (bool playing)
    {
        CHECK (processor.isStandaloneTransportPlaying() == playing);
        CHECK (play->isVisible() == !playing);
        CHECK (stop->isVisible() == playing);
        CHECK (play->getBounds() == stop->getBounds());
    };
    checkTransport (false);
    play->onClick();
    play->onClick();
    checkTransport (true);
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        auto stream = juce::File (directory).getChildFile (compact ? "native-transport-compact.png" : "native-transport.png").createOutputStream();
        REQUIRE (stream != nullptr);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        REQUIRE (juce::PNGImageFormat().writeImageToStream (image, *stream));
    }
    stop->onClick();
    stop->onClick();
    checkTransport (false);
    view->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    checkTransport (true);
    view->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    checkTransport (false);
}

TEST_CASE ("Project details keep header and dates read only and apply metadata explicitly", "[native-ui][project-details][render]")
{
    PluginProcessor processor;
    processor.setProjectMetadata ("Night drive", "A sparse rhythm", "2026-07-08T10:00:00Z", "2026-07-09T11:00:00Z", "ofsound");
    NativeEditor editor (processor);
    const bool compact = GENERATE (false, true);
    if (compact)
        editor.setCompactViewport (true, 814, 409);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    if (compact)
    {
        for (const auto& caption : { juce::String ("Controls"), juce::String ("Project") })
            for (auto* child : editor.getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == caption)
                    button->onClick();
    }
    ProjectHeader* header = nullptr;
    ProjectDetails* details = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<ProjectHeader*> (child)) header = candidate;
        if (auto* candidate = dynamic_cast<ProjectDetails*> (child)) details = candidate;
        CHECK ((dynamic_cast<juce::TextEditor*> (child) == nullptr || child->getTitle() != "Project title"));
    }
    REQUIRE (header != nullptr);
    REQUIRE (details != nullptr);
    CHECK (header->isVisible());
    CHECK_FALSE (header->getWantsKeyboardFocus());
    CHECK_FALSE (details->isVisible());
    auto* info = dynamic_cast<juce::TextButton*> (header->getChildComponent (0));
    REQUIRE (info != nullptr);
    CHECK (info->getTitle() == "Project details");
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    const auto capture = [&] (const juce::String& name)
    {
        if (directory.isEmpty())
            return;
        auto stream = juce::File (directory).getChildFile (name + (compact ? "-compact.png" : "-desktop.png")).createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds()), *stream));
    };
    capture ("project-header");
    info->onClick();
    CHECK (details->isVisible());
    juce::TextEditor* name = nullptr;
    juce::TextEditor* author = nullptr;
    juce::TextEditor* description = nullptr;
    juce::TextButton* apply = nullptr;
    juce::TextButton* saveAs = nullptr;
    juce::TextButton* save = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getButtonText() == "SAVE AS") saveAs = button;
            if (button->getButtonText() == "SAVE") save = button;
        }
    int dates = 0;
    for (auto* child : details->getChildren())
    {
        if (auto* field = dynamic_cast<juce::TextEditor*> (child))
        {
            if (field->getTitle() == "Project name") name = field;
            if (field->getTitle() == "Project author") author = field;
            if (field->getTitle() == "Project description") description = field;
        }
        if (auto* date = dynamic_cast<juce::Label*> (child))
        {
            CHECK_FALSE (date->isEditableOnSingleClick());
            CHECK_FALSE (date->isEditableOnDoubleClick());
            CHECK (date->getText().contains ("2026"));
            ++dates;
        }
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getButtonText() == "Apply") apply = button;
            CHECK (button->getButtonText() != "Save As...");
        }
    }
    REQUIRE (name != nullptr);
    REQUIRE (author != nullptr);
    REQUIRE (description != nullptr);
    REQUIRE (apply != nullptr);
    REQUIRE (saveAs != nullptr);
    REQUIRE (save != nullptr);
    CHECK (saveAs->isVisible());
    CHECK (saveAs->getX() > save->getRight());
    CHECK (saveAs->getY() == save->getY());
    CHECK (dates == 2);
    CHECK (author->getText() == "ofsound");
    capture ("project-details");
    name->setText ("Discard this");
    CHECK (editor.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (details->isVisible());
    CHECK (processor.getProjectName() == "Night drive");
    info->onClick();
    name->setText ("  Morning drive  ");
    author->setText ("  Ben  ");
    description->setText ("Updated notes\nSecond line");
    apply->onClick();
    CHECK (processor.getProjectName() == "Morning drive");
    CHECK (processor.getProjectAuthor() == "Ben");
    CHECK (processor.getProjectDescription() == "Updated notes\nSecond line");
    CHECK (processor.getProjectCreatedAt() == "2026-07-08T10:00:00Z");
    CHECK (processor.getProjectModifiedAt() != "2026-07-09T11:00:00Z");
    CHECK_FALSE (details->isVisible());
    int action = -1;
    editor.projectAction = [&] (int value) { action = value; };
    info->onClick();
    author->setText ("Copy author");
    apply->onClick();
    saveAs->onClick();
    CHECK (action == 5);
    CHECK (processor.getProjectAuthor() == "Copy author");
    info->onClick();
    name->setText ("Stale draft");
    processor.resetProject();
    editor.sync();
    CHECK_FALSE (details->isVisible());
    CHECK (processor.getProjectName() == "Untitled Project");
}

TEST_CASE ("Footer row order toggle reverses presentation without editing the project", "[native-ui][render][row-order]")
{
    PluginProcessor processor;
    for (int ch = 1; ch <= 8; ++ch)
        REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, ch, ch, 1, 100 }));
    NativeEditor editor (processor);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    juce::TextButton* toggle = nullptr;
    juce::TextButton* tips = nullptr;
    juce::TextButton* colors = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getTitle() == "Reverse row order") toggle = button;
            if (button->getButtonText() == "Tips") tips = button;
            if (button->getButtonText() == "DC") colors = button;
        }
    REQUIRE (toggle != nullptr);
    REQUIRE (tips != nullptr);
    REQUIRE (colors != nullptr);
    const bool originalOrder = editor.layout.reverseRows;
    if (originalOrder)
        toggle->onClick();
    const auto before = processor.editorDocument();
    editor.controller.selected.set (0);
    toggle->onClick();
    CHECK (toggle->getToggleState());
    CHECK (editor.layout.cell ({ 0, 0 }).getY() > editor.layout.cell ({ 7, 0 }).getY());
    CHECK (tips->getRight() < toggle->getX());
    CHECK (toggle->getRight() < colors->getX());
    CHECK (processor.editorDocument() == before);
    CHECK (editor.controller.selected.test (0));
    CHECK_FALSE (editor.controller.canUndo());
    {
        NativeEditor reopened (processor);
        CHECK (reopened.layout.reverseRows);
    }
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
    toggle->onClick();
    CHECK_FALSE (toggle->getToggleState());
    if (originalOrder)
        toggle->onClick();
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        auto stream = juce::File (directory).getChildFile ("reverse-rows.png").createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (image, *stream));
    }
}

TEST_CASE ("Footer help preserves every control tooltip and resolves nested children", "[native-ui][render]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    FooterHelp* help = nullptr;
    juce::TextButton* select = nullptr;
    juce::TextButton* rate = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<FooterHelp*> (child)) help = candidate;
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getProperties().contains ("selectMode")) select = button;
            if (button->getTitle() == "Pattern rate") rate = button;
        }
    }
    REQUIRE (help != nullptr);
    REQUIRE (select != nullptr);
    REQUIRE (rate != nullptr);
    help->setHelpEnabled (true);
    int checked = 0;
    std::function<void (juce::Component&)> audit = [&] (juce::Component& component)
    {
        if (auto* client = dynamic_cast<juce::TooltipClient*> (&component); client && client->getTooltip().isNotEmpty())
        {
            INFO (component.getTitle().toStdString());
            help->showHelpFor (&component);
            CHECK (help->getText() == client->getTooltip());
            juce::TextLayout textLayout;
            juce::AttributedString text (client->getTooltip());
            text.setFont (help->getFont());
            textLayout.createLayout (text, static_cast<float> (help->getWidth()));
            CHECK (textLayout.getHeight() <= static_cast<float> (help->getHeight()));
            ++checked;
        }
        for (auto* child : component.getChildren()) audit (*child);
    };
    audit (editor);
    CHECK (checked > 100);
    juce::Component nested;
    rate->addAndMakeVisible (nested);
    const auto now = juce::Time::getCurrentTime();
    help->mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 2, 2 }, {}, 1, 0, 0, 0, 0,
        &nested, &nested, now, { 2, 2 }, now, 0, false));
    CHECK (help->getText() == rate->getTooltip());
    rate->setEnabled (false);
    help->showHelpFor (&nested);
    CHECK (help->getText() == rate->getTooltip());
    help->setHelpEnabled (false);
    help->showHelpFor (select);
    CHECK (help->getText().isEmpty());
    help->setHelpEnabled (true);
    help->showHelpFor (select);
    CHECK (help->getText() == select->getTooltip());
    help->showHelpFor (nullptr);
    CHECK (help->getText().isEmpty());
    help->showHelpFor (&editor);
    CHECK (help->getText().isEmpty());
    juce::TextButton unrelated;
    unrelated.setTooltip ("Another editor's help");
    help->showHelpFor (&unrelated);
    CHECK (help->getText().isEmpty());
    help->showHelpFor (select);
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        auto stream = juce::File (directory).getChildFile ("footer-help.png").createOutputStream();
        REQUIRE (stream != nullptr);
        stream->setPosition (0);
        REQUIRE (stream->truncate().wasOk());
        const auto footer = editor.getLocalBounds().removeFromBottom (Layout::footerHeight);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (footer, true, 2), *stream));
    }
}

TEST_CASE ("Layer segments keep one active highlight across Select and Option transitions", "[native-ui][render]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    const bool compact = GENERATE (false, true);
    if (compact)
    {
        editor.setCompactViewport (true, 814, 409);
        editor.setSize (814, 409);
    }
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    juce::TextButton* velocity = nullptr;
    juce::TextButton* chance = nullptr;
    juce::TextButton* select = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getProperties().contains ("layerSegment"))
                (static_cast<int> (button->getProperties()["layerSegment"]) == 0 ? velocity : chance) = button;
            if (button->getProperties().contains ("selectMode")) select = button;
        }
    REQUIRE (velocity != nullptr);
    REQUIRE (chance != nullptr);
    REQUIRE (select != nullptr);
    const auto checkActive = [&] (juce::TextButton* active)
    {
        for (auto* button : { velocity, chance, select })
            CHECK (button->getToggleState() == (button == active));
    };
    checkActive (velocity);
    chance->onClick();
    checkActive (chance);
    select->onClick();
    checkActive (select);
    select->onClick();
    checkActive (chance);
    editor.modifierKeysChanged (juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
    checkActive (select);
    editor.modifierKeysChanged ({});
    checkActive (chance);
    select->onClick();
    velocity->onClick();
    CHECK_FALSE (editor.controller.selectLatched);
    checkActive (velocity);
    editor.modifierKeysChanged (juce::ModifierKeys (juce::ModifierKeys::altModifier));
    checkActive (chance);
    editor.modifierKeysChanged ({});
    checkActive (velocity);

    // Reproduce the screenshot: layer control keeps focus while Select is active.
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    editor.setVisible (true);
    velocity->grabKeyboardFocus();
    select->onClick();
    checkActive (select);
    const auto image = velocity->createComponentSnapshot (velocity->getLocalBounds());
    CHECK (image.getPixelAt (velocity->getWidth() / 2, 0) != Palette::accent);
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        const auto bounds = velocity->getBounds().getUnion (chance->getBounds()).getUnion (select->getBounds()).expanded (2);
        for (auto* active : { select, velocity, chance })
        {
            if (active != select)
                active->onClick();
            checkActive (active);
            auto stream = juce::File (directory).getChildFile ((compact ? "compact-layer-" : "desktop-layer-") + active->getButtonText().toLowerCase() + ".png").createOutputStream();
            REQUIRE (stream != nullptr);
            stream->setPosition (0);
            REQUIRE (stream->truncate().wasOk());
            REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (bounds, true, 3), *stream));
        }
    }
}

TEST_CASE ("Select modifier supports tap latch momentary use and physical Shift feedback", "[native-ui]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    GestureButton* select = nullptr;
    PatternGrid* grid = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* button = dynamic_cast<GestureButton*> (child); button && button->getProperties().contains ("selectMode"))
            select = button;
        if (auto* candidate = dynamic_cast<PatternGrid*> (child))
            grid = candidate;
    }
    REQUIRE (select != nullptr);
    REQUIRE (grid != nullptr);
    auto event = [] (juce::Component& component, juce::Point<float> point, int duration = 0)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), point, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                                &component, &component, now, point, now - juce::RelativeTime::milliseconds (duration), 1, false);
    };
    auto press = [&] { select->mouseDown (event (*select, { 10, 10 })); };
    auto release = [&] (int duration = 0) { select->mouseUp (event (*select, { 10, 10 }, duration)); };

    SECTION ("Tap latches and toggles off without changing the editing layer")
    {
        editor.controller.layer = Layer::chance;
        press();
        CHECK (select->getToggleState());
        release();
        CHECK (editor.controller.selectLatched);
        CHECK (editor.controller.layer == Layer::chance);
        press();
        release();
        CHECK_FALSE (editor.controller.shiftDown ({}));
    }
    SECTION ("Holding without another action is momentary")
    {
        press();
        CHECK (editor.controller.shiftDown ({}));
        release (400);
        CHECK_FALSE (editor.controller.shiftDown ({}));
        CHECK_FALSE (select->getToggleState());
    }
    SECTION ("Using a second touch makes even a short press momentary")
    {
        press();
        const auto point = grid->layout.cell ({ 0, 0 }).getCentre();
        grid->mouseDown (event (*grid, point));
        grid->mouseUp (event (*grid, point));
        CHECK (editor.controller.selected.test (0));
        CHECK_FALSE (editor.controller.cell ({ 0, 0 }).enabled);
        release (100);
        CHECK_FALSE (editor.controller.shiftDown ({}));
    }
    SECTION ("Physical Shift lights Select and release preserves a latch")
    {
        editor.modifierKeysChanged (juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
        CHECK (select->getToggleState());
        editor.modifierKeysChanged ({});
        CHECK_FALSE (select->getToggleState());
        press();
        release();
        editor.modifierKeysChanged (juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
        editor.modifierKeysChanged ({});
        CHECK (select->getToggleState());
        editor.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        CHECK_FALSE (editor.controller.shiftDown ({}));
    }
    SECTION ("Releasing outside cancels a tap")
    {
        press();
        select->mouseUp (event (*select, { -10, -10 }));
        CHECK_FALSE (editor.controller.shiftDown ({}));
    }
    SECTION ("Latched Select toggles cells and drags a marquee without painting")
    {
        press();
        release();
        const auto first = grid->layout.cell ({ 0, 0 }).getCentre();
        const auto last = grid->layout.cell ({ 0, 2 }).getCentre().translated (0, 10);
        grid->mouseDown (event (*grid, first));
        grid->mouseDrag (event (*grid, last));
        grid->mouseUp (event (*grid, last));
        CHECK (editor.controller.selected.count() == 3);
        grid->mouseDown (event (*grid, first));
        grid->mouseUp (event (*grid, first));
        CHECK_FALSE (editor.controller.selected.test (0));
        CHECK (editor.controller.selected.count() == 2);
        CHECK_FALSE (editor.controller.cell ({ 0, 0 }).enabled);
        CHECK_FALSE (editor.controller.cell ({ 0, 2 }).enabled);
    }
    SECTION ("Momentary Select chooses a pattern copy source")
    {
        press();
        for (auto* child : editor.getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "2")
                button->onClick();
        CHECK (editor.controller.copySource == 1);
        release (100);
        CHECK_FALSE (editor.controller.shiftDown ({}));
    }
    SECTION ("Momentary Select solos a channel")
    {
        press();
        GestureButton* mute = nullptr;
        for (auto* child : grid->getChildren())
            if (auto* button = dynamic_cast<GestureButton*> (child); button && button->getTitle() == "power channel 1")
                mute = button;
        REQUIRE (mute != nullptr);
        mute->mouseDown (event (*mute, { 10, 10 }));
        mute->mouseUp (event (*mute, { 10, 10 }));
        const auto& mutes = editor.controller.state.playback.sourceChannelMutes[0];
        CHECK_FALSE (mutes[0]);
        CHECK (mutes[1]);
        release (100);
        CHECK_FALSE (editor.controller.shiftDown ({}));
    }
}

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

TEST_CASE ("Undo redo shortcuts use keyboard Shift independently of Select mode", "[native-ui]")
{
    const bool standalone = GENERATE (false, true);
    const bool selectLatched = GENERATE (false, true);
    const bool selectHeld = GENERATE (false, true);
    // macOS sends Command shortcuts with a key code but no text character.
    const bool textCharacterPresent = GENERATE (false, true);
    juce::AudioProcessor::setTypeOfNextNewPlugin (standalone ? juce::AudioProcessor::wrapperType_Standalone : juce::AudioProcessor::wrapperType_Undefined);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    NativeEditor editor (processor);
    auto& controller = editor.controller;
    auto cell = controller.cell ({ 0, 0 });
    const bool original = cell.enabled;
    cell.enabled = !original;
    controller.begin ("Toggle cell");
    controller.editCell ({ 0, 0 }, cell);
    controller.commit();
    REQUIRE (controller.canUndo());
    controller.selectLatched = selectLatched;
    controller.selectHeld = selectHeld;

    CHECK (editor.keyPressed (juce::KeyPress ('Z', juce::ModifierKeys::commandModifier, textCharacterPresent ? 'z' : 0)));
    CHECK (controller.cell ({ 0, 0 }).enabled == original);
    CHECK (controller.canRedo());
    CHECK_FALSE (controller.canUndo());

    CHECK (editor.keyPressed (juce::KeyPress ('Z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, textCharacterPresent ? 'Z' : 0)));
    CHECK (controller.cell ({ 0, 0 }).enabled == !original);
    CHECK (controller.canUndo());
    CHECK_FALSE (controller.canRedo());
    CHECK (controller.selectLatched == selectLatched);
    CHECK (controller.selectHeld == selectHeld);
    CHECK_FALSE (controller.selectUsed);
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
    {
        Layout layout;
        layout.steps = steps;
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

TEST_CASE ("Select mode preserves cell data in both layers", "[native-ui][render]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    controller.layer = GENERATE (Layer::velocity, Layer::chance);
    const int mode = GENERATE (0, 1, 2);
    controller.selectLatched = mode == 0;
    controller.selectHeld = mode == 1;
    PatternGrid grid (controller);
    grid.layout.compact = GENERATE (false, true);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    grid.modifierKeysChanged (juce::ModifierKeys (mode == 2 ? juce::ModifierKeys::shiftModifier : 0));
    const auto render = [&] (bool alternate)
    {
        REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1,
            alternate ? 127 : 30, alternate ? 100 : 25, alternate ? 8 : 2, alternate ? 85 : 1, alternate ? 4 : 1 }));
        controller.refresh();
        grid.sync();
        return grid.createComponentSnapshot (grid.layout.cell ({ 0, 0 }).getSmallestIntegerContainer());
    };
    const auto equalPixels = [] (const juce::Image& a, const juce::Image& b)
    {
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    return false;
        return true;
    };
    const auto first = render (false);
    CHECK_FALSE (equalPixels (first, render (true)));
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (mode == 0 && directory.isNotEmpty())
    {
        auto stream = juce::File (directory).getChildFile ("select-cell-" + juce::String (grid.layout.compact ? "compact-" : "desktop-") + juce::String (static_cast<int> (controller.layer)) + ".png").createOutputStream();
        REQUIRE (stream != nullptr);
        stream->setPosition (0);
        REQUIRE (stream->truncate().wasOk());
        REQUIRE (juce::PNGImageFormat().writeImageToStream (grid.createComponentSnapshot (grid.layout.cell ({ 0, 0 }).getSmallestIntegerContainer(), true, 4), *stream));
    }
    controller.selectLatched = controller.selectHeld = false;
    grid.modifierKeysChanged ({});
    const auto detailed = render (false);
    CHECK (equalPixels (first, detailed));
    CHECK_FALSE (equalPixels (detailed, render (true)));
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
    juce::MouseEvent gridEvent (juce::Component& grid, juce::Point<float> position, juce::Point<float> origin, int modifiers, bool dragged = false)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &grid, &grid, now, origin, now, 1, dragged };
    }
}

TEST_CASE ("Reversed row geometry keeps clicks drags and loop handles on their logical channel", "[native-ui][row-order]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.layout.compact = GENERATE (false, true);
    grid.layout.reverseRows = true;
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();
    for (int ch = 0; ch < grid.layout.channels; ++ch)
    {
        const Location location { ch, 3 };
        CHECK (grid.layout.at (grid.layout.cell (location).getCentre()) == location);
    }
    CHECK (grid.layout.channelAtY (grid.layout.cellsY() - 1) == -1);
    CHECK (grid.layout.channelAtY (grid.layout.cellsY() + grid.layout.rowHeight() * static_cast<float> (grid.layout.channels)) == -1);
    const auto start = grid.layout.cell ({ 0, 0 }).getCentre();
    const int left = juce::ModifierKeys::leftButtonModifier;
    controller.layer = Layer::chance;
    grid.mouseDown (gridEvent (grid, start, start, left));
    grid.mouseUp (gridEvent (grid, start, start, 0));
    CHECK (controller.cell ({ 0, 0 }).enabled);
    CHECK_FALSE (controller.cell ({ 7, 0 }).enabled);
    controller.layer = Layer::velocity;
    controller.selected.set (0);
    const auto destination = grid.layout.cell ({ 1, 0 }).getCentre();
    REQUIRE (destination.y < start.y);
    grid.mouseDown (gridEvent (grid, start, start, left));
    grid.mouseDrag (gridEvent (grid, destination, start, left, true));
    grid.mouseUp (gridEvent (grid, destination, start, 0, true));
    CHECK_FALSE (controller.cell ({ 0, 0 }).enabled);
    CHECK (controller.cell ({ 1, 0 }).enabled);
    controller.undo();
    CHECK (controller.cell ({ 0, 0 }).enabled);
    if (!grid.layout.compact)
    {
        controller.selected.reset();
        const auto handle = grid.layout.cell ({ 0, 0 }).getTopLeft().translated (-8, grid.layout.cellHeight() * 0.5f);
        const auto moved = handle.translated (grid.layout.cellWidth(), 0);
        grid.mouseDown (gridEvent (grid, handle, handle, left));
        grid.mouseDrag (gridEvent (grid, moved, handle, left, true));
        grid.mouseUp (gridEvent (grid, moved, handle, 0, true));
        CHECK (controller.loop (0).loopStart == 1);
        CHECK (controller.loop (7).loopStart == 0);
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
    const auto beforeRightClick = controller.cell ({ 0, 0 });
    const auto rightClick = gridEvent (grid, start, start, juce::ModifierKeys::rightButtonModifier);
    grid.mouseDown (rightClick);
    grid.mouseUp (rightClick);
    CHECK (controller.cell ({ 0, 0 }) == beforeRightClick);
    CHECK_FALSE (controller.canUndo());
}

TEST_CASE ("Velocity edge hints require an active neighbor inside the loop and stay stable across the edge", "[native-ui]")
{
    PluginProcessor processor;
    const bool leftActive = GENERATE (false, true);
    const bool rightActive = GENERATE (false, true);
    const bool loopStartsHere = GENERATE (false, true);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, leftActive ? 1 : 0, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 2, rightActive ? 1 : 0, 90 }));
    if (loopStartsHere)
        REQUIRE (processor.dispatchUiEngineCommand ("channel_loop_length", { 1, 15, 2 }));
    EditorController controller (processor);
    REQUIRE (controller.cell ({ 0, 0 }).enabled == leftActive);
    REQUIRE (controller.cell ({ 0, 1 }).enabled == rightActive);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const auto bounds = grid.layout.cell ({ 0, 1 });
    const auto edge = juce::Point<float> (bounds.getX(), bounds.getCentreY());
    const auto hidden = grid.createComponentSnapshot (grid.getLocalBounds());
    grid.mouseMove (gridEvent (grid, edge.translated (-5, 0), edge, 0));
    // Drain callbacks while waiting so JUCE's timer thread can keep advancing.
    for (int i = 0; i < 20; ++i)
    {
        juce::Thread::sleep (10);
        juce::Timer::callPendingTimersSynchronously();
    }
    const auto visible = grid.createComponentSnapshot (grid.getLocalBounds());
    const auto region = juce::Rectangle<float> (edge.x - 7, edge.y - 7, 14, 14).getSmallestIntegerContainer();
    bool changed = false;
    for (int y = region.getY(); y < region.getBottom(); ++y)
        for (int x = region.getX(); x < region.getRight(); ++x)
            changed = changed || hidden.getPixelAt (x, y) != visible.getPixelAt (x, y);
    REQUIRE (changed == ((leftActive && !loopStartsHere) || rightActive));
    for (const float dx : { 5.0f, -5.0f, 2.0f })
    {
        grid.mouseMove (gridEvent (grid, edge.translated (dx, 0), edge, 0));
        grid.sync();
        const auto moved = grid.createComponentSnapshot (grid.getLocalBounds());
        for (int y = region.getY(); y < region.getBottom(); ++y)
            for (int x = region.getX(); x < region.getRight(); ++x)
                REQUIRE (moved.getPixelAt (x, y) == visible.getPixelAt (x, y));
    }
    grid.mouseExit (gridEvent (grid, edge, edge, 0));
    const auto exited = grid.createComponentSnapshot (grid.getLocalBounds());
    for (int y = region.getY(); y < region.getBottom(); ++y)
        for (int x = region.getX(); x < region.getRight(); ++x)
            REQUIRE (exited.getPixelAt (x, y) == hidden.getPixelAt (x, y));
}

TEST_CASE ("Empty row edges keep normal cell clicking and painting", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 2, 1, 1, 90 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const int step = GENERATE (0, 3, 15);
    const bool rightEdge = GENERATE (false, true);
    const auto bounds = grid.layout.cell ({ 0, step });
    const juce::Point<float> start { rightEdge ? bounds.getRight() - 1 : bounds.getX() + 1, bounds.getCentreY() };
    const int left = juce::ModifierKeys::leftButtonModifier;
    const auto down = gridEvent (grid, start, start, left);
    int endStep = step;
    grid.mouseMove (gridEvent (grid, start, start, 0));
    grid.mouseDown (down);

    SECTION ("Click toggles the cell under the pointer")
    {
        grid.mouseUp (down);
    }
    SECTION ("Drag paints the empty cells")
    {
        endStep = step == 15 ? 12 : step + 3;
        const auto end = grid.layout.cell ({ 0, endStep }).getCentre();
        grid.mouseDrag (gridEvent (grid, end, start, left, true));
        grid.mouseUp (gridEvent (grid, end, start, left, true));
    }
    for (int s = 0; s < grid.layout.steps; ++s)
        CHECK (controller.cell ({ 0, s }).enabled == (s >= juce::jmin (step, endStep) && s <= juce::jmax (step, endStep)));
    CHECK (controller.cell ({ 1, 0 }).enabled);
    REQUIRE (controller.canUndo());
    controller.undo();
    for (int s = 0; s < grid.layout.steps; ++s)
        CHECK_FALSE (controller.cell ({ 0, s }).enabled);
    CHECK_FALSE (controller.canUndo());
}

TEST_CASE ("Velocity edge lines still interpolate beside active cells", "[native-ui]")
{
    PluginProcessor processor;
    for (const int step : { 1, 3, 5 })
        REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, step, 1, 90 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    std::array<Cell, 16> before;
    for (int s = 0; s < grid.layout.steps; ++s)
        before[static_cast<size_t> (s)] = controller.cell ({ 0, s });
    const auto row = grid.layout.cell ({ 0, 1 });
    const juce::Point<float> start { row.getX(), row.getBottom() - 99.0f / 126.0f * row.getHeight() };
    const juce::Point<float> end { grid.layout.cell ({ 0, 5 }).getX(), row.getBottom() - 19.0f / 126.0f * row.getHeight() };
    const int left = juce::ModifierKeys::leftButtonModifier;
    grid.mouseDown (gridEvent (grid, start, start, left));
    grid.mouseDrag (gridEvent (grid, end, start, left, true));
    grid.mouseUp (gridEvent (grid, end, start, left, true));
    CHECK (controller.cell ({ 0, 2 }).velocity == 73);
    CHECK (controller.cell ({ 0, 4 }).velocity == 20);
    for (int s = 0; s < grid.layout.steps; ++s)
        CHECK (controller.cell ({ 0, s }).enabled == before[static_cast<size_t> (s)].enabled);
    controller.undo();
    for (int s = 0; s < grid.layout.steps; ++s)
        CHECK (controller.cell ({ 0, s }) == before[static_cast<size_t> (s)]);
}

TEST_CASE ("Cell velocity adjustments follow drag distance from the original value", "[native-ui]")
{
    PluginProcessor processor;
    const int initialVelocity = GENERATE (30, 90, 125);
    const float clickHeight = GENERATE (0.2f, 0.8f);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, initialVelocity }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const auto bounds = grid.layout.cell ({ 0, 0 });
    const juce::Point<float> start { bounds.getCentreX(), bounds.getY() + bounds.getHeight() * clickHeight };
    const int left = juce::ModifierKeys::leftButtonModifier;
    const auto down = gridEvent (grid, start, start, left);
    grid.mouseDown (down);
    grid.mouseDrag (gridEvent (grid, start.translated (0, -20), start, left, true));
    CHECK (controller.cell ({ 0, 0 }).velocity == juce::jmin (127, initialVelocity + 8));
    grid.mouseDrag (gridEvent (grid, start.translated (0, 20), start, left, true));
    CHECK (controller.cell ({ 0, 0 }).velocity == initialVelocity - 8);
    grid.mouseDrag (gridEvent (grid, start, start, left, true));
    CHECK (controller.cell ({ 0, 0 }).velocity == initialVelocity);
    grid.cancelGesture();
    CHECK (controller.cell ({ 0, 0 }).velocity == initialVelocity);
}

TEST_CASE ("Selection mutes only unselected active cell colors and leaves empty slots unchanged", "[native-ui][render]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 70, 80, 3, 2, 4 }));
    EditorController controller (processor);
    controller.layer = GENERATE (Layer::velocity, Layer::chance);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const auto before = grid.createComponentSnapshot (grid.getLocalBounds());
    controller.selectLatched = true;
    controller.selected.set (0);
    grid.sync();
    const auto after = grid.createComponentSnapshot (grid.getLocalBounds());
    for (int step : { 0, 2, 3 })
    {
        INFO ("Step " << step);
        const auto bounds = grid.cellBounds ({ 0, step }).reduced (8).getSmallestIntegerContainer();
        bool identical = true;
        for (int y = bounds.getY(); y < bounds.getBottom(); ++y)
            for (int x = bounds.getX(); x < bounds.getRight(); ++x)
                identical = identical && before.getPixelAt (x, y) == after.getPixelAt (x, y);
        CHECK (identical == (step != 2));
    }
}

TEST_CASE ("Single velocity drags draw across the row in one gesture", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 70, 80, 3, 2, 4 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    std::array<Cell, 5> before;
    for (int s = 0; s < 5; ++s)
        before[static_cast<size_t> (s)] = controller.cell ({ 0, s });
    const auto start = grid.layout.cell ({ 0, 0 }).getCentre();
    const int left = juce::ModifierKeys::leftButtonModifier;
    const auto down = gridEvent (grid, start, start, left);
    auto dragTo = [&] (int step, float dy)
    {
        grid.mouseDrag (gridEvent (grid, { grid.layout.cell ({ 0, step }).getCentreX(), start.y + dy }, start, left, true));
    };
    grid.mouseDown (down);
    dragTo (0, -20);
    REQUIRE (controller.cell ({ 0, 0 }).velocity == 108);
    dragTo (4, -40);
    for (int s = 0; s <= 4; ++s)
    {
        auto expected = before[static_cast<size_t> (s)];
        expected.enabled = true;
        expected.velocity = jsRound (108.0 + s * 2.5);
        CHECK (controller.cell ({ 0, s }) == expected);
    }
    dragTo (1, -40);
    for (int s = 1; s <= 4; ++s)
        CHECK (controller.cell ({ 0, s }).velocity == 118);
    CHECK (controller.cell ({ 0, 0 }).velocity == 108);
    CHECK_FALSE (controller.cell ({ 1, 1 }).enabled);

    SECTION ("Release and undo restores every touched cell")
    {
        grid.mouseUp (down);
        controller.undo();
    }
    SECTION ("Cancellation restores every touched cell")
    {
        grid.cancelGesture();
    }
    for (int s = 0; s < 5; ++s)
        CHECK (controller.cell ({ 0, s }) == before[static_cast<size_t> (s)]);
    CHECK_FALSE (controller.canUndo());
}

TEST_CASE ("Velocity sweeps stay inside the starting row loop", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("channel_loop_length", { 1, 3, 2 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const auto start = grid.layout.cell ({ 0, 2 }).getCentre();
    const int left = juce::ModifierKeys::leftButtonModifier;
    const auto down = gridEvent (grid, start, start, left);
    grid.mouseDown (down);
    grid.mouseDrag (gridEvent (grid, start.translated (0, -20), start, left, true));
    grid.mouseDrag (gridEvent (grid, { grid.layout.cell ({ 0, 8 }).getCentreX(), start.y - 20 }, start, left, true));
    grid.mouseDrag (gridEvent (grid, { grid.layout.cell ({ 0, 0 }).getCentreX(), start.y - 20 }, start, left, true));
    grid.mouseUp (down);
    for (int s = 0; s < grid.layout.steps; ++s)
    {
        CHECK (controller.cell ({ 0, s }).enabled == (s >= 1 && s <= 3));
        if (s >= 1 && s <= 3)
            CHECK (controller.cell ({ 0, s }).velocity == 108);
    }
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

TEST_CASE ("Logo double click reveals about and random controls without resizing the editor", "[native-ui][render]")
{
    PluginProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* view = dynamic_cast<NativeEditor*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    PatternGrid* grid = nullptr;
    juce::Viewport* panel = nullptr;
    PreviewPanel* inspector = nullptr;
    for (auto* child : view->getChildren())
    {
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
            CHECK (button->getButtonText() != "Random");
        if (auto* candidate = dynamic_cast<PatternGrid*> (child))
            grid = candidate;
        if (auto* candidate = dynamic_cast<juce::Viewport*> (child))
            panel = candidate;
        if (auto* candidate = dynamic_cast<PreviewPanel*> (child))
        {
            inspector = candidate;
        }
    }
    REQUIRE (grid != nullptr);
    REQUIRE (panel != nullptr);
    REQUIRE (inspector != nullptr);
    REQUIRE (dynamic_cast<PreviewPanel*> (panel->getViewedComponent()) != nullptr);
    const auto editorBounds = editor->getBounds();
    const auto gridBounds = grid->getBounds();
    const auto event = [inspector] (juce::Point<float> position, int clicks, int modifiers = juce::ModifierKeys::leftButtonModifier)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, inspector, inspector, now, position, now, clicks, false);
    };
    inspector->mouseDown (event ({ 90, 80 }, 1));
    CHECK_FALSE (panel->isVisible());
    inspector->mouseDoubleClick (event ({ 280, 80 }, 2));
    CHECK_FALSE (panel->isVisible());
    inspector->mouseDoubleClick (event ({ 90, 80 }, 2, juce::ModifierKeys::rightButtonModifier));
    CHECK_FALSE (panel->isVisible());
    inspector->mouseDoubleClick (event ({ 90, 80 }, 2));
    CHECK (panel->isVisible());
    CHECK (view->layout.randomVisible);
    CHECK (panel->getBounds().intersects (gridBounds));
    CHECK (panel->getX() == 0);
    CHECK (panel->getRight() == view->getWidth());
    CHECK (panel->getBottom() == inspector->getY());
    CHECK_FALSE (panel->getBounds().intersects (inspector->getBounds()));
    CHECK (view->getIndexOfChildComponent (panel) > view->getIndexOfChildComponent (grid));
    CHECK (grid->getBounds() == gridBounds);
    CHECK (editor->getBounds() == editorBounds);
    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    REQUIRE (image.isValid());
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        juce::File root (directory);
        REQUIRE (root.createDirectory().wasOk());
        auto stream = root.getChildFile ("native-about.png").createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (image, *stream));
    }
    inspector->mouseDoubleClick (event ({ 90, 80 }, 2));
    CHECK_FALSE (panel->isVisible());
    CHECK (grid->getBounds() == gridBounds);
    inspector->mouseDoubleClick (event ({ 90, 80 }, 2));
    REQUIRE (panel->isVisible());
    CHECK (view->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (panel->isVisible());
    CHECK_FALSE (view->layout.randomVisible);
    // Small channel counts keep all about/random controls reachable by scrolling.
    view->layout.channels = 1;
    view->resized();
    CHECK (panel->getViewedComponent()->getHeight() >= 540);
    CHECK (panel->getViewedComponent()->getHeight() > panel->getHeight());
    CHECK (panel->getBottom() == inspector->getY());
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

TEST_CASE ("Option temporarily shows Chance while the button preserves its latch", "[native-ui]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    PatternGrid* grid = nullptr;
    juce::TextButton* toggle = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<PatternGrid*> (child))
            grid = candidate;
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getProperties().contains ("layerSegment") && static_cast<int> (button->getProperties()["layerSegment"]) == 1)
            toggle = button;
    }
    REQUIRE (grid != nullptr);
    REQUIRE (toggle != nullptr);
    CHECK_FALSE (toggle->getToggleState());
    grid->modifierKeysChanged (juce::ModifierKeys (juce::ModifierKeys::altModifier));
    CHECK (grid->displayLayer() == Layer::chance);
    CHECK (toggle->getToggleState());
    CHECK (toggle->getTitle() == "Chance editing layer");
    CHECK (editor.controller.layer == Layer::velocity);
    editor.sync();
    CHECK (grid->displayLayer() == Layer::chance);
    CHECK (toggle->getToggleState());
    grid->modifierKeysChanged ({});
    CHECK (grid->displayLayer() == Layer::velocity);
    CHECK_FALSE (toggle->getToggleState());
    const auto position = grid->layout.cell ({ 0, 0 }).getCentre();
    grid->mouseMove (gridEvent (*grid, position, position, juce::ModifierKeys::altModifier));
    CHECK (toggle->getToggleState());
    grid->mouseMove (gridEvent (*grid, position, position, 0));
    CHECK_FALSE (toggle->getToggleState());
    CHECK (editor.controller.layer == Layer::velocity);
    toggle->onClick();
    grid->modifierKeysChanged (juce::ModifierKeys (juce::ModifierKeys::altModifier));
    CHECK (grid->displayLayer() == Layer::chance);
    grid->modifierKeysChanged ({});
    CHECK (grid->displayLayer() == Layer::chance);
    CHECK (toggle->getToggleState());
}

TEST_CASE ("Option edits unselected Chance values and keeps the gesture layer until release", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 60 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 2, 1, 70, 80 }));
    NativeEditor editor (processor);
    PatternGrid* grid = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* candidate = dynamic_cast<PatternGrid*> (child))
            grid = candidate;
    REQUIRE (grid != nullptr);
    const auto before = editor.controller.cell ({ 0, 0 });
    const auto second = editor.controller.cell ({ 0, 1 });
    const auto start = grid->layout.cell ({ 0, 0 }).getCentre();
    const int left = juce::ModifierKeys::leftButtonModifier;
    const int option = juce::ModifierKeys::altModifier;
    editor.modifierKeysChanged (juce::ModifierKeys (option));
    grid->mouseDown (gridEvent (*grid, start, start, left | option));
    grid->mouseDrag (gridEvent (*grid, start.translated (0, -20), start, left | option, true));
    auto expected = before;
    expected.probability += 8;
    CHECK (editor.controller.cell ({ 0, 0 }) == expected);
    auto expectedSecond = second;
    CHECK (editor.controller.cell ({ 0, 1 }) == expectedSecond);
    editor.modifierKeysChanged (juce::ModifierKeys (left));
    CHECK (grid->displayLayer() == Layer::chance);
    grid->mouseUp (gridEvent (*grid, start.translated (0, -20), start, 0, true));
    CHECK (grid->displayLayer() == Layer::velocity);
    CHECK (editor.controller.layer == Layer::velocity);
    editor.controller.undo();
    CHECK (editor.controller.cell ({ 0, 0 }) == before);
    CHECK (editor.controller.cell ({ 0, 1 }) == second);
}

TEST_CASE ("Option arms selection duplication without switching layers or editing values", "[native-ui]")
{
    const auto layer = GENERATE (Layer::velocity, Layer::chance);
    const bool compact = GENERATE (false, true);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 60, 4, 5, 3 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 2, 1, 70, 80, 3, 2, 2 }));
    NativeEditor editor (processor);
    if (compact)
        editor.setCompactViewport (true, 814, 409);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    PatternGrid* grid = nullptr;
    juce::TextButton* heading = nullptr;
    juce::TextButton* copy = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<PatternGrid*> (child))
            grid = candidate;
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getProperties().contains ("layerSegment") && static_cast<int> (button->getProperties()["layerSegment"]) == 1)
                heading = button;
            if (button->getTitle() == "Copy selected cells when dragging")
                copy = button;
        }
    }
    REQUIRE (grid != nullptr);
    REQUIRE (heading != nullptr);
    REQUIRE (copy != nullptr);
    auto& controller = editor.controller;
    controller.layer = layer;
    controller.selected.set (0);
    controller.selected.set (1);
    const auto first = controller.cell ({ 0, 0 });
    const auto second = controller.cell ({ 0, 1 });
    editor.sync();
    const int option = juce::ModifierKeys::altModifier;
    const int left = juce::ModifierKeys::leftButtonModifier;
    editor.modifierKeysChanged (juce::ModifierKeys (option));
    CHECK (grid->displayLayer() == layer);
    CHECK (heading->getToggleState() == (layer == Layer::chance));
    CHECK (grid->isCopyMode());
    CHECK (copy->getToggleState());
    CHECK_FALSE (grid->copySelection);
    CHECK (grid->getMouseCursor() == juce::MouseCursor (juce::MouseCursor::CopyingCursor));
    const auto start = grid->layout.cell ({ 0, 0 }).getCentre();
    const auto end = grid->layout.cell ({ 1, 4 }).getCentre();
    grid->mouseDown (gridEvent (*grid, start, start, left | option));

    SECTION ("Dragging copies the entire selection and supports undo redo")
    {
        grid->mouseDrag (gridEvent (*grid, end, start, left | option, true));
        CHECK (grid->displayLayer() == layer);
        grid->mouseUp (gridEvent (*grid, end, start, option, true));
        CHECK (controller.cell ({ 0, 0 }) == first);
        CHECK (controller.cell ({ 0, 1 }) == second);
        CHECK (controller.cell ({ 1, 4 }) == first);
        CHECK (controller.cell ({ 1, 5 }) == second);
        CHECK (controller.selected.count() == 2);
        CHECK (controller.selected.test (36));
        CHECK (controller.selected.test (37));
        controller.undo();
        CHECK (controller.cell ({ 0, 0 }) == first);
        CHECK (controller.cell ({ 0, 1 }) == second);
        CHECK_FALSE (controller.cell ({ 1, 4 }).enabled);
        CHECK_FALSE (controller.cell ({ 1, 5 }).enabled);
        controller.redo();
        CHECK (controller.cell ({ 1, 4 }) == first);
        CHECK (controller.cell ({ 1, 5 }) == second);
    }
    SECTION ("Clicking without dragging leaves selected notes intact")
    {
        grid->mouseUp (gridEvent (*grid, start, start, option));
        CHECK (controller.cell ({ 0, 0 }) == first);
        CHECK (controller.cell ({ 0, 1 }) == second);
        CHECK_FALSE (controller.canUndo());
    }
    SECTION ("Cancelling a duplicate drag leaves the pattern intact")
    {
        grid->mouseDrag (gridEvent (*grid, end, start, left | option, true));
        grid->cancelGesture();
        CHECK (controller.cell ({ 0, 0 }) == first);
        CHECK (controller.cell ({ 0, 1 }) == second);
        CHECK_FALSE (controller.cell ({ 1, 4 }).enabled);
        CHECK_FALSE (controller.cell ({ 1, 5 }).enabled);
        CHECK_FALSE (controller.canUndo());
    }
    editor.modifierKeysChanged ({});
    CHECK (controller.layer == layer);
    CHECK (grid->displayLayer() == layer);
    CHECK_FALSE (grid->isCopyMode());
    CHECK_FALSE (copy->getToggleState());
    CHECK_FALSE (grid->copySelection);
}

TEST_CASE ("Option can switch a selection drag between moving and copying", "[native-ui]")
{
    const bool optionInitially = GENERATE (false, true);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 60, 4, 5, 3 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    controller.selected.set (0);
    grid.sync();
    const auto original = controller.cell ({ 0, 0 });
    const auto start = grid.layout.cell ({ 0, 0 }).getCentre();
    const auto end = grid.layout.cell ({ 1, 4 }).getCentre();
    const int left = juce::ModifierKeys::leftButtonModifier;
    const int option = juce::ModifierKeys::altModifier;
    const int initial = optionInitially ? option : 0;
    const int final = optionInitially ? 0 : option;
    grid.mouseDown (gridEvent (grid, start, start, left | initial));
    grid.mouseDrag (gridEvent (grid, end, start, left | initial, true));
    grid.modifierKeysChanged (juce::ModifierKeys (left | final));
    CHECK (grid.isCopyMode() == !optionInitially);
    CHECK (grid.displayLayer() == Layer::velocity);
    grid.mouseUp (gridEvent (grid, end, start, final, true));
    CHECK (controller.cell ({ 1, 4 }) == original);
    CHECK (controller.cell ({ 0, 0 }).enabled == !optionInitially);
    controller.undo();
    CHECK (controller.cell ({ 0, 0 }) == original);
    CHECK_FALSE (controller.cell ({ 1, 4 }).enabled);
}

TEST_CASE ("Roll is edited in the inspector while grid layers remain velocity and chance", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 80, 4, 5, 1 }));
    NativeEditor editor (processor);
    for (char key : { '1', '2', '3', '4' })
        CHECK_FALSE (editor.keyPressed (juce::KeyPress (key, {}, static_cast<juce::juce_wchar> (key))));
    CHECK (editor.controller.layer == Layer::velocity);
    juce::TextButton* velocity = nullptr;
    juce::TextButton* chance = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getProperties().contains ("layerSegment"))
        {
            if (button->getButtonText() == "Velocity") velocity = button;
            if (button->getButtonText() == "Chance") chance = button;
        }
    REQUIRE (velocity != nullptr);
    REQUIRE (chance != nullptr);
    CHECK (velocity->getToggleState());
    CHECK_FALSE (chance->getToggleState());
    chance->onClick();
    chance->onClick();
    CHECK (editor.controller.layer == Layer::chance);
    CHECK (chance->getToggleState());
    CHECK_FALSE (velocity->getToggleState());
    velocity->onClick();
    velocity->onClick();
    CHECK (editor.controller.layer == Layer::velocity);
    CHECK (velocity->getToggleState());
    CHECK_FALSE (chance->getToggleState());

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

TEST_CASE ("Chance grid clicks fill empty cells and preserve their values", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 0, 90, 60, 4, 5, 3 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.layout.compact = GENERATE (false, true);
    grid.setSize (1338, 554);
    grid.sync();
    const bool temporary = GENERATE (false, true);
    controller.layer = temporary ? Layer::velocity : Layer::chance;
    const auto before = controller.cell ({ 0, 0 });
    REQUIRE_FALSE (before.enabled);
    const auto position = grid.layout.cell ({ 0, 0 }).getCentre();
    const int modifiers = temporary ? juce::ModifierKeys::altModifier : 0;
    const auto click = [&]
    {
        grid.mouseDown (gridEvent (grid, position, position, modifiers | juce::ModifierKeys::leftButtonModifier));
        grid.mouseUp (gridEvent (grid, position, position, modifiers));
    };
    click();
    auto expected = before;
    expected.enabled = true;
    CHECK (controller.cell ({ 0, 0 }) == expected);
    // Clicking an existing Chance cell still leaves its probability and cycle intact.
    click();
    CHECK (controller.cell ({ 0, 0 }) == expected);
    controller.undo();
    CHECK (controller.cell ({ 0, 0 }) == before);
    controller.redo();
    CHECK (controller.cell ({ 0, 0 }) == expected);
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
    const auto bounds = grid.layout.cell ({ 0, 0 });
    const auto start = juce::Point<float> (bounds.getX() + GENERATE (2.0f, 27.0f, 52.0f), bounds.getY() + GENERATE (2.0f, 27.0f, 52.0f));
    const int modifiers = juce::ModifierKeys::leftButtonModifier;
    const float horizontalDrag = GENERATE (0.0f, 30.0f);
    const auto down = gridEvent (grid, start, start, modifiers);
    grid.mouseDown (down);
    grid.mouseDrag (gridEvent (grid, start.translated (horizontalDrag, -20), start, modifiers, true));
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

TEST_CASE ("Inspector hover previews cell layers without changing the chosen layer", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 60, 4, 5, 3 }));
    EditorController controller (processor);
    controller.selected.set (0);
    PatternGrid grid (controller);
    PreviewPanel inspector (controller);
    inspector.setSize (1200, 180);
    inspector.setInspectorBounds ({ 300, 10, 880, 160 });
    const bool compact = GENERATE (false, true);
    inspector.setCompact (compact);
    inspector.sync();
    controller.layer = GENERATE (Layer::velocity, Layer::chance);
    const auto chosen = controller.layer;
    const auto event = [&] (juce::Component& component, juce::Point<float> position)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position,
            {}, 1, 0, 0, 0, 0, &component, &component, now, position, now, 0, false);
    };
    CycleEditor* cycle = nullptr;
    for (auto* child : inspector.getChildren())
    {
        auto expected = Layer::chance;
        if (auto* candidate = dynamic_cast<CycleEditor*> (child))
            cycle = candidate;
        else if (auto* control = dynamic_cast<ValueControl*> (child))
        {
            if (control->caption == "Velocity")
                expected = Layer::velocity;
            else if (control->caption != "Probability" && control->caption != "Length")
                continue;
        }
        else
            continue;
        if (!child->isVisible())
            continue;
        inspector.mouseEnter (event (*child, child->getLocalBounds().toFloat().getCentre()));
        CHECK (grid.displayLayer() == expected);
        CHECK (controller.layer == chosen);
        CHECK_FALSE (controller.editing());
        inspector.mouseExit (event (inspector, { -1, -1 }));
        CHECK (grid.displayLayer() == chosen);
    }
    REQUIRE (cycle != nullptr);
    for (auto* child : cycle->getChildren())
    {
        if (!child->isVisible())
            continue;
        inspector.mouseEnter (event (*child, child->getLocalBounds().toFloat().getCentre()));
        CHECK (grid.displayLayer() == Layer::chance);
    }
    auto* bit = dynamic_cast<juce::TextButton*> (cycle->getChildComponent (1));
    REQUIRE (bit != nullptr);
    bit->onClick();
    CHECK (grid.displayLayer() == Layer::chance);
    CHECK (controller.layer == chosen);
    inspector.mouseExit (event (inspector, { -1, -1 }));
    CHECK (grid.displayLayer() == chosen);
    inspector.mouseEnter (event (*cycle, cycle->getLocalBounds().toFloat().getCentre()));
    controller.selected.reset();
    inspector.sync();
    CHECK (grid.displayLayer() == chosen);
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

TEST_CASE ("Disabled cycle handles reject input and cancel active drags", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 60, 4, 5, 3 }));
    EditorController controller (processor);
    CycleEditor cycle (controller);
    cycle.locations = [&] { return controller.selection (true); };
    cycle.setSize (400, 120);
    cycle.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    cycle.setVisible (true);
    juce::Component* handle = nullptr;
    for (auto* child : cycle.getChildren())
        if (child->getTitle() == "Cycle length")
            handle = child;
    REQUIRE (handle != nullptr);
    const auto event = [&] (float x, bool dragged = false)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { x, 10.0f },
            juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            handle, handle, now, { 4.0f, 10.0f }, now, 1, dragged);
    };

    SECTION ("No selection")
    {
        cycle.sync();
    }
    SECTION ("Disabled with a selection")
    {
        controller.selected.set (0);
        cycle.sync();
        cycle.setEnabled (false);
    }
    SECTION ("Selection cleared during a drag")
    {
        controller.selected.set (0);
        cycle.sync();
        handle->mouseDown (event (4));
        REQUIRE (controller.editing());
        handle->mouseDrag (event (55, true));
        REQUIRE (controller.cell ({ 0, 0 }).cycle > 4);
        controller.selected.reset();
        cycle.sync();
    }

    CHECK_FALSE (handle->isEnabled());
    CHECK_FALSE (handle->getWantsKeyboardFocus());
    CHECK (handle->getMouseCursor() == juce::MouseCursor::NormalCursor);
    handle->mouseDown (event (4));
    handle->mouseDrag (event (100, true));
    handle->mouseUp (event (100, true));
    CHECK_FALSE (handle->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK_FALSE (controller.editing());
    CHECK_FALSE (controller.canUndo());
    CHECK (controller.layer == Layer::velocity);
    CHECK (controller.cell ({ 0, 0 }).cycle == 4);

    controller.selected.set (0);
    cycle.sync();
    CHECK (handle->isEnabled());
    CHECK (handle->getWantsKeyboardFocus());
    CHECK (handle->getMouseCursor() == juce::MouseCursor::LeftRightResizeCursor);
    CHECK (handle->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (controller.cell ({ 0, 0 }).cycle == 5);
}

TEST_CASE ("Standalone restores the saved UI scale before creating its editor", "[native-ui]")
{
    juce::TemporaryFile temporary (".settings");
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    const int percent = GENERATE (50, 75, 100, 120);
    {
        PluginProcessor previousSession;
        previousSession.setProjectUiScalePercent (percent);
        juce::PropertiesFile settings (temporary.getFile(), options);
        ksh::saveStandaloneUiScale (previousSession, settings);
        REQUIRE (settings.saveIfNeeded());
    }
    PluginProcessor nextSession;
    juce::PropertiesFile settings (temporary.getFile(), options);
    ksh::restoreStandaloneUiScale (nextSession, settings);
    REQUIRE (nextSession.getProjectUiScalePercent() == percent);
    std::unique_ptr<juce::AudioProcessorEditor> editor (nextSession.createEditor());
    const auto* view = dynamic_cast<NativeEditor*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    CHECK (editor->getWidth() == juce::roundToInt (view->layout.width() * static_cast<float> (percent) / 100.0f));
    CHECK (nextSession.getProjectUiScalePercent() == percent);
}

TEST_CASE ("Standalone UI scale defaults and limits remain valid", "[native-ui]")
{
    PluginProcessor processor;
    juce::PropertySet settings;
    ksh::restoreStandaloneUiScale (processor, settings);
    CHECK (processor.getProjectUiScalePercent() == 100);
    settings.setValue ("uiScalePercent", -10);
    ksh::restoreStandaloneUiScale (processor, settings);
    CHECK (processor.getProjectUiScalePercent() == 50);
    settings.setValue ("uiScalePercent", 500);
    ksh::restoreStandaloneUiScale (processor, settings);
    CHECK (processor.getProjectUiScalePercent() == 120);
}

namespace
{
    struct GestureCounter final : juce::AudioProcessorListener
    {
        int parameterIndex = -1, begins = 0, ends = 0, depth = 0, maxDepth = 0;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override
        {
            if (index != parameterIndex)
                return;
            ++begins;
            maxDepth = juce::jmax (maxDepth, ++depth);
        }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override
        {
            if (index != parameterIndex)
                return;
            ++ends;
            --depth;
        }
    };

    ValueControl* findValueControl (juce::Component& parent, const juce::String& caption)
    {
        for (auto* child : parent.getChildren())
            if (auto* control = dynamic_cast<ValueControl*> (child); control && control->caption == caption)
                return control;
        return nullptr;
    }

    void scroll (ValueControl& control, int ticks)
    {
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), { 4, 4 }, {}, 1, 0, 0, 0, 0, &control, &control, now, { 4, 4 }, now, 0, false);
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = ticks > 0 ? 0.1f : -0.1f;
        for (int i = 0; i < std::abs (ticks); ++i)
            control.mouseWheelMove (event, wheel);
    }
}

TEST_CASE ("Grid swing and nudge positions match MIDI timing for each roll hit", "[native-ui][swing-display][render]")
{
    const int swing = GENERATE (0, 37, 100);
    const int subdivision = GENERATE (0, 1, 2);
    const int nudge = GENERATE (-25, 25);
    const bool compact = GENERATE (false, true);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 6, 1, 90, 100, 1, 1, 4 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell_nudge", { 1, 1, 6, nudge }));
    REQUIRE (processor.dispatchUiEngineCommand ("swing", { swing }));
    REQUIRE (processor.dispatchUiEngineCommand ("swing_subdivision", { subdivision }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.layout.compact = compact;
    grid.layout.reverseRows = true;
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();
    MidiPlaybackRunner runner;
    runner.prepare (48000.0);
    juce::MidiBuffer midi;
    runner.processBlock (controller.state.playback, 0.0, 120.0, true, 48000, midi);
    int part = 0;
    for (const auto event : midi)
    {
        if (!event.getMessage().isNoteOn())
            continue;
        REQUIRE (part < 4);
        const float visualStep = (grid.hitPosition ({ 0, 5 }, part).x - grid.layout.gridLeft()) / grid.layout.cellWidth();
        CHECK (visualStep == Catch::Approx (static_cast<double> (event.samplePosition) / 6000.0).margin (0.0001));
        ++part;
    }
    CHECK (part == 4);
    CHECK (grid.cellBounds ({ 0, 5 }).getX() == Catch::Approx (grid.hitPosition ({ 0, 5 }).x));
    // Live swing edits must move accessible bounds as well as the paint position.
    REQUIRE (processor.dispatchUiEngineCommand ("swing", { swing == 0 ? 100 : 0 }));
    controller.refresh();
    grid.sync();
    auto* accessible = grid.getChildComponent (5);
    REQUIRE (accessible != nullptr);
    CHECK (accessible->getX() == grid.cellBounds ({ 0, 5 }).toNearestInt().getX());
    const auto position = grid.cellBounds ({ 0, 5 }).getCentre();
    grid.mouseDown (gridEvent (grid, position, position, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    grid.mouseUp (gridEvent (grid, position, position, juce::ModifierKeys::shiftModifier));
    CHECK (controller.selected.test (5));
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty() && swing == 0 && nudge == 25)
    {
        auto stream = juce::File (directory).getChildFile ("swing-grid-" + juce::String (compact ? "compact-" : "desktop-") + juce::String (subdivision) + ".png").createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (grid.createComponentSnapshot (grid.getLocalBounds()), *stream));
    }
}

TEST_CASE ("Swing display follows the next visit through channel loops and directions", "[native-ui][swing-display]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("channel_loop_length", { 1, 4, 2 }));
    REQUIRE (processor.dispatchUiEngineCommand ("swing", { 73 }));
    REQUIRE (processor.dispatchUiEngineCommand ("swing_subdivision", { 2 }));
    const int mode = GENERATE (0, 1, 2, 3);
    const std::array<const char*, 4> modes { "normal", "reverse", "ping_pong", "reverse_ping_pong" };
    REQUIRE (processor.dispatchUiEngineCommand ("channel_playback_mode", { 1, modes[static_cast<size_t> (mode)] }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.sync();
    const std::array<int, 3> reference { 0, 5, 8 };
    const std::array<std::array<int, 3>, 4> expected {{ { 1, 5, 9 }, { 2, 6, 10 }, { 1, 6, 9 }, { 2, 5, 10 } }};
    for (size_t visit = 0; visit < reference.size(); ++visit)
    {
        controller.playingStep = reference[visit] + 1;
        controller.playingGlobalStep = reference[visit];
        grid.sync();
        const double offset = swingDelayStepsForPosition (expected[static_cast<size_t> (mode)][visit], 73, 2);
        CHECK (grid.hitPosition ({ 0, 2 }).x == Catch::Approx (grid.layout.cell ({ 0, 2 }).getX() + offset * grid.layout.cellWidth()));
    }
}

TEST_CASE ("Nudge adjusts a selection together, preserves differences, and supports undo and cancel", "[native-ui][nudge]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 90, 60, 4, 5, 3 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 6, 1, 70, 80, 3, 2, 4 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell_nudge", { 1, 1, 3, -20 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell_nudge", { 1, 1, 6, 30 }));
    NativeEditor editor (processor);
    const bool compact = GENERATE (false, true);
    if (compact)
        editor.setCompactViewport (true, 814, 409);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    PreviewPanel* inspector = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* panel = dynamic_cast<PreviewPanel*> (child))
            inspector = panel;
    REQUIRE (inspector != nullptr);
    auto* nudge = findValueControl (*inspector, "Nudge");
    REQUIRE (nudge != nullptr);
    editor.controller.selected.set (2);
    editor.controller.selected.set (5);
    editor.sync();
    CHECK (nudge->isVisible());
    CHECK (nudge->isEnabled());
    CHECK (nudge->mixed);
    const auto first = editor.controller.cell ({ 0, 2 });
    const auto second = editor.controller.cell ({ 0, 5 });
    REQUIRE (nudge->horizontalAdjustment);
    const auto start = nudge->getLocalBounds().toFloat().getCentre();
    const int left = juce::ModifierKeys::leftButtonModifier;
    const auto down = gridEvent (*nudge, start, start, left);
    nudge->mouseDown (down);
    nudge->mouseDrag (gridEvent (*nudge, start.translated (64, 0), start, left, true));
    CHECK (editor.controller.cell ({ 0, 2 }).nudge == 10);
    CHECK (editor.controller.cell ({ 0, 5 }).nudge == 60);
    nudge->mouseDrag (gridEvent (*nudge, start.translated (-24, 0), start, left, true));
    CHECK (editor.controller.cell ({ 0, 2 }).nudge == -30);
    CHECK (editor.controller.cell ({ 0, 5 }).nudge == 20);
    nudge->mouseDrag (gridEvent (*nudge, start.translated (0, -40), start, left, true));
    CHECK (editor.controller.cell ({ 0, 2 }).nudge == first.nudge);
    CHECK (editor.controller.cell ({ 0, 5 }).nudge == second.nudge);
    nudge->cancelGesture();
    editor.sync();
    nudge->onBegin();
    nudge->onChange (10);
    nudge->onEnd();
    auto expectedFirst = first, expectedSecond = second;
    expectedFirst.nudge = 10;
    expectedSecond.nudge = 60;
    CHECK (editor.controller.cell ({ 0, 2 }) == expectedFirst);
    CHECK (editor.controller.cell ({ 0, 5 }) == expectedSecond);
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        auto stream = juce::File (directory).getChildFile (compact ? "nudge-editor-compact.png" : "nudge-editor-desktop.png").createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds()), *stream));
    }
    editor.controller.undo();
    CHECK (editor.controller.cell ({ 0, 2 }) == first);
    CHECK (editor.controller.cell ({ 0, 5 }) == second);
    editor.controller.redo();
    CHECK (editor.controller.cell ({ 0, 2 }) == expectedFirst);
    CHECK (editor.controller.cell ({ 0, 5 }) == expectedSecond);
    nudge->onBegin();
    nudge->onChange (-40);
    nudge->onCancel();
    CHECK (editor.controller.cell ({ 0, 2 }) == expectedFirst);
    CHECK (editor.controller.cell ({ 0, 5 }) == expectedSecond);
    nudge->onBegin();
    nudge->onChange (100);
    nudge->onEnd();
    CHECK (editor.controller.cell ({ 0, 2 }).nudge == 50);
    CHECK (editor.controller.cell ({ 0, 5 }).nudge == 100);
    // Undo only the nudge field; a later recording/command edit must survive.
    REQUIRE (processor.dispatchUiEngineCommand ("cell_velocity", { 1, 1, 3, 55 }));
    editor.controller.undo();
    CHECK (editor.controller.cell ({ 0, 2 }).nudge == 10);
    CHECK (editor.controller.cell ({ 0, 2 }).velocity == 55);
    editor.controller.selected.reset();
    editor.sync();
    CHECK_FALSE (nudge->isEnabled());
}

TEST_CASE ("Nudged cell shapes and hit tests move with their timing", "[native-ui][render][nudge]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 90, 60, 4, 5, 3 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    const bool compact = GENERATE (false, true);
    grid.layout.compact = compact;
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    controller.layer = GENERATE (Layer::velocity, Layer::chance);
    const int nudge = GENERATE (-50, 50);
    REQUIRE (processor.dispatchUiEngineCommand ("cell_nudge", { 1, 1, 3, nudge }));
    controller.refresh();
    grid.sync();
    const auto original = grid.layout.cell ({ 0, 2 });
    const auto shifted = grid.cellBounds ({ 0, 2 });
    CHECK (shifted.getX() == Catch::Approx (original.getX() + static_cast<float> (nudge) * grid.layout.cellWidth() / 100.0f));
    const auto point = shifted.getCentre().translated (nudge < 0 ? -5.0f : 5.0f, 0.0f);
    const int modifiers = juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier;
    grid.mouseDown (gridEvent (grid, point, point, modifiers));
    grid.mouseUp (gridEvent (grid, point, point, juce::ModifierKeys::shiftModifier));
    CHECK (controller.selected.test (2));
    CHECK (controller.selected.count() == 1);
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        auto stream = juce::File (directory).getChildFile ("nudge-grid-" + juce::String (compact ? "compact-" : "desktop-") + juce::String (nudge) + "-" + juce::String (static_cast<int> (controller.layer)) + ".png").createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (grid.createComponentSnapshot (grid.getLocalBounds()), *stream));
    }
}

TEST_CASE ("Scroll wheel ticks on a value control make one undo step and one host gesture", "[native-ui]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    GestureCounter gestures;
    gestures.parameterIndex = processor.getValueTreeState().getParameter ("swing")->getParameterIndex();
    processor.addListener (&gestures);
    auto* swing = findValueControl (editor, "Amount");
    REQUIRE (swing != nullptr);

    scroll (*swing, 5);
    CHECK (swing->isWheelGestureActive());
    CHECK (processor.engineStateSnapshot().swing == 5);
    CHECK (gestures.begins == 1);
    CHECK (gestures.ends == 0);

    juce::MessageManager::getInstance()->runDispatchLoopUntil (700);
    CHECK_FALSE (swing->isWheelGestureActive());
    CHECK (gestures.begins == 1);
    CHECK (gestures.ends == 1);
    CHECK (gestures.maxDepth == 1);
    CHECK (processor.getValueTreeState().getRawParameterValue ("swing")->load() == 5.0f);

    REQUIRE (editor.controller.canUndo());
    editor.controller.undo();
    CHECK (processor.engineStateSnapshot().swing == 0);
    CHECK_FALSE (editor.controller.canUndo());
    processor.removeListener (&gestures);
}

TEST_CASE ("A new edit closes an idle scroll gesture instead of joining it", "[native-ui]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    auto& controller = editor.controller;
    auto* swing = findValueControl (editor, "Amount");
    REQUIRE (swing != nullptr);

    scroll (*swing, 2);
    REQUIRE (swing->isWheelGestureActive());
    controller.begin ("Toggle cell");
    CHECK_FALSE (swing->isWheelGestureActive());
    auto cell = controller.cell ({ 0, 0 });
    cell.enabled = true;
    controller.editCell ({ 0, 0 }, cell);
    controller.commit();

    controller.undo();
    CHECK_FALSE (controller.cell ({ 0, 0 }).enabled);
    CHECK (processor.engineStateSnapshot().swing == 2);
    controller.undo();
    CHECK (processor.engineStateSnapshot().swing == 0);

    scroll (*swing, 3);
    controller.cancel();
    CHECK_FALSE (swing->isWheelGestureActive());
    CHECK (processor.engineStateSnapshot().swing == 3);
}

TEST_CASE ("Macro gestures reach the host once and settings undo stays conditional", "[native-ui]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    GestureCounter gestures;
    gestures.parameterIndex = processor.getValueTreeState().getParameter ("swing")->getParameterIndex();
    processor.addListener (&gestures);

    controller.begin ("Change swing", "swing");
    for (const int value : { 10, 20, 30 })
        controller.command ("swing", { value });
    controller.commit();
    CHECK (gestures.begins == 1);
    CHECK (gestures.ends == 1);
    CHECK (gestures.maxDepth == 1);

    REQUIRE (processor.dispatchUiEngineCommand ("channel_note", { 1, 50 }));
    controller.undo();
    CHECK (processor.engineStateSnapshot().swing == 0);
    CHECK (controller.state.playback.channels[0].note == 50);
    controller.redo();
    CHECK (processor.engineStateSnapshot().swing == 30);

    REQUIRE (processor.dispatchUiEngineCommand ("swing", { 45 }));
    controller.undo();
    CHECK (processor.engineStateSnapshot().swing == 45);
    processor.removeListener (&gestures);
}
