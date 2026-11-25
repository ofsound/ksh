#include <PluginEditor.h>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <ui/KshEditorController.h>
#include <ui/KshLayout.h>
#include <ui/KshUiScale.h>
#include <engine/KshMath.h>
#include "../standalone/StandalonePreferences.h"

using namespace ksh;
using namespace ksh::ui;

TEST_CASE ("Escape passes through unless the editor has something to cancel", "[native-ui][escape]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    editor.layout.compact = GENERATE (false, true);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    const juce::KeyPress escape (juce::KeyPress::escapeKey);
    CHECK_FALSE (editor.keyPressed (escape));

    SECTION ("Select mode")
    {
        editor.controller.selectLatched = true;
        CHECK (editor.keyPressed (escape));
        CHECK_FALSE (editor.controller.selectLatched);
    }
    SECTION ("Pattern copying")
    {
        editor.controller.copySource = 0;
        CHECK (editor.keyPressed (escape));
        CHECK (editor.controller.copySource == -1);
    }
    SECTION ("An edit in progress")
    {
        editor.controller.begin ("Edit cell");
        CHECK (editor.keyPressed (escape));
        CHECK_FALSE (editor.controller.editing());
    }
    CHECK_FALSE (editor.keyPressed (escape));
}

TEST_CASE ("Graphical cell values preserve editing data and distinguish empty and full probability", "[native-ui][render][cell-display]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 25, 2, 4, 1 }));
    EditorController controller (processor);
    REQUIRE (controller.graphicalCellValues);
    controller.layer = Layer::chance;
    controller.graphicalCellValues = false;
    PatternGrid grid (controller);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();
    processor.markDocumentClean();
    const auto original = controller.cell ({ 0, 0 });
    const auto capture = [&] { return grid.createComponentSnapshot (grid.cellBounds ({ 0, 0 }).toNearestInt()); };
    const auto numeric = capture();
    controller.graphicalCellValues = true;
    grid.sync();
    const auto graphical = capture();
    bool changed = false;
    for (int y = 0; y < numeric.getHeight(); ++y)
        for (int x = 0; x < numeric.getWidth(); ++x)
            changed = changed || numeric.getPixelAt (x, y) != graphical.getPixelAt (x, y);
    CHECK (changed);
    CHECK (controller.cell ({ 0, 0 }) == original);
    CHECK_FALSE (processor.isDocumentModified());
    REQUIRE (processor.dispatchUiEngineCommand ("cell_probability", { 1, 1, 1, 0 }));
    controller.refresh();
    grid.sync();
    const auto empty = capture();
    REQUIRE (processor.dispatchUiEngineCommand ("cell_probability", { 1, 1, 1, 100 }));
    controller.refresh();
    grid.sync();
    const auto full = capture();
    CHECK (empty.getPixelAt (empty.getWidth() / 2, empty.getHeight() / 2 - 3)
           != full.getPixelAt (full.getWidth() / 2, full.getHeight() / 2 - 3));
    CHECK (empty.getPixelAt (empty.getWidth() / 2, empty.getHeight() / 2 - 3).getPerceivedBrightness()
           < full.getPixelAt (full.getWidth() / 2, full.getHeight() / 2 - 3).getPerceivedBrightness());
    CHECK (full.getPixelAt (full.getWidth() / 2, full.getHeight() / 2 - 3).getPerceivedBrightness() > 0.2f);
    CHECK (grid.getChildComponent (0)->getDescription().contains ("probability 100"));
    controller.graphicalCellValues = false;
    grid.sync();
    CHECK (controller.cell ({ 0, 0 }).probability == 100);
}

TEST_CASE ("Row power ink reflects mute after a tap and remains stable when hovered", "[native-ui][mute-indicator]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    PatternGrid* grid = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* candidate = dynamic_cast<PatternGrid*> (child)) grid = candidate;
    REQUIRE (grid != nullptr);
    RowPlaybackButton* power = nullptr;
    for (auto* child : grid->getChildren())
        if (child->getTitle() == "power channel 1") power = dynamic_cast<RowPlaybackButton*> (child);
    REQUIRE (power != nullptr);
    const auto ink = [&] (bool hover, bool down)
    {
        power->setSize (30, 30);
        constexpr int scale = 3;
        juce::Image image (juce::Image::ARGB, 30 * scale, 30 * scale, true);
        juce::Graphics graphics (image);
        graphics.addTransform (juce::AffineTransform::scale (static_cast<float> (scale)));
        power->paintButton (graphics, hover, down);
        return image.getPixelAt (15 * scale, 12 * scale);
    };
    const auto active = ink (false, false);
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent press (juce::Desktop::getInstance().getMainMouseSource(), { 15, 15 },
        juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
        power, power, now, { 15, 15 }, now, 1, false);
    power->mouseDown (press);
    CHECK_FALSE (editor.controller.state.playback.sourceChannelMutes[0][0]);
    power->mouseUp (press);
    REQUIRE (editor.controller.state.playback.sourceChannelMutes[0][0]);
    REQUIRE_FALSE (power->getToggleState());
    CHECK (active == Palette::accent);
    CHECK (ink (false, false) == Palette::text);
    CHECK (ink (true, false) == Palette::text);
    CHECK (ink (true, true) == Palette::text);
    editor.controller.undo();
    CHECK (power->getToggleState());
}

TEST_CASE ("Draggable numeric inputs keep suffix pixels fixed across value changes", "[native-ui][numeric-layout][render]")
{
    ValueControl control;
    control.setBounds (0, 0, 240, 48);
    control.valueFontSize = 18;
    const auto suffix = GENERATE (juce::String (" BPM"), juce::String (" steps"), juce::String ("%"));
    control.suffix = suffix;
    control.minimum = suffix == " BPM" ? 20 : 0;
    control.maximum = suffix == " BPM" ? 300 : suffix == " steps" ? 32 : 100;
    const auto capture = [&] (int value)
    {
        control.setValue (value);
        return control.createComponentSnapshot (control.getLocalBounds(), true, 2);
    };
    const auto reference = capture (22);
    for (const int value : { 9, 11, 18, 28, 88, 99, 100, 111, 288 })
    {
        if (value < control.minimum || value > control.maximum)
            continue;
        INFO ("suffix=" << suffix.toStdString() << ", value=" << value);
        const auto image = capture (value);
        // The suffix is to the right of the centre. Compare the rendered
        // pixels there, rather than mirroring the layout calculation.
        for (int y = 4; y < image.getHeight() - 4; ++y)
            for (int x = image.getWidth() / 2 + 32; x < image.getWidth() - 4; ++x)
                REQUIRE (image.getPixelAt (x, y) == reference.getPixelAt (x, y));
    }
}

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

TEST_CASE ("Zen keeps the standalone viewport fixed while scaling its centered interface", "[native-ui][zen][render]")
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    PluginEditor editor (processor, true);
    const juce::Rectangle<int> viewport (1920, 1080);
    editor.setBounds (viewport);
    auto* view = dynamic_cast<NativeEditor*> (editor.getChildComponent (0));
    REQUIRE (view != nullptr);
    ValueControl* scaleControl = nullptr;
    for (auto* child : view->getChildren())
    {
        if (auto* value = dynamic_cast<ValueControl*> (child); value && value->getTitle().startsWith ("UI scale"))
            scaleControl = value;
    }
    REQUIRE (scaleControl != nullptr);
    CHECK_FALSE (scaleControl->isVisible());
    editor.setZenMode (true);
    CHECK (editor.isZenMode());
    CHECK (scaleControl->isVisible());
    CHECK (scaleControl->isEnabled());
    auto safeViewport = editor.getLocalBounds().toFloat();
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (editor.getScreenBounds()))
    {
        const auto safeScreen = display->safeAreaInsets.subtractedFrom (display->userBounds.toNearestInt());
        safeViewport = editor.getLocalArea (nullptr, safeScreen).getIntersection (editor.getLocalBounds()).toFloat();
    }
    for (int percent : { 50, 75, 120, 200, 400 })
    {
        processor.setProjectUiScalePercent (percent);
        view->sync();
        CHECK (editor.getBounds() == viewport);
        CHECK (processor.getProjectUiScalePercent() == percent);
        const auto content = view->getLocalBounds().toFloat().transformedBy (view->getTransform());
        CHECK (content.getCentreX() == Catch::Approx (safeViewport.getCentreX()));
        CHECK (content.getCentreY() == Catch::Approx (safeViewport.getCentreY()));
        CHECK (content.getX() >= -0.01f);
        CHECK (content.getY() >= -0.01f);
        CHECK (content.getRight() <= 1920.01f);
        CHECK (content.getBottom() <= 1080.01f);
        if (percent == 50)
        {
            const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
            CHECK (image.getPixelAt (0, 0) == juce::Colours::black);
        }
    }
    CHECK (view->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (editor.isZenMode());
    CHECK_FALSE (scaleControl->isVisible());
    CHECK (editor.getBounds() == viewport);
}

TEST_CASE ("Hosted plugins do not expose or enter Zen", "[native-ui][zen]")
{
    PluginProcessor processor;
    PluginEditor editor (processor);
    editor.setZenMode (true);
    CHECK_FALSE (editor.isZenMode());
    auto* view = dynamic_cast<NativeEditor*> (editor.getChildComponent (0));
    REQUIRE (view != nullptr);
    for (auto* child : view->getChildren())
        if (child->getTitle() == "Zen mode" || child->getTitle() == "UI scale percent")
            CHECK_FALSE (child->isVisible());
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
        CHECK (view->getWidth() * 3 == view->getHeight() * 4);
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

TEST_CASE ("iPad AUv3 keeps the first host viewport through later editor updates", "[native-ui][ipad][auv3][render]")
{
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AudioUnitv3);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    processor.setProjectUiScalePercent (50);
    PluginEditor editor (processor);
    auto* view = dynamic_cast<NativeEditor*> (editor.getChildComponent (0));
    REQUIRE (view != nullptr);
    view->layout.touchRows = true; // Exercise the tablet viewport policy in the desktop test runner.

    for (const auto viewport : { juce::Rectangle<int> (1024, 336), juce::Rectangle<int> (1024, 620), juce::Rectangle<int> (814, 409) })
    {
        editor.setBounds (viewport);
        CHECK (view->layout.compact == (viewport.getHeight() <= 500));
        const auto checkFit = [&]
        {
            CHECK (editor.getBounds() == viewport);
            const auto content = view->getLocalBounds().toFloat().transformedBy (view->getTransform());
            CHECK (content.getX() >= -0.01f);
            CHECK (content.getY() >= -0.01f);
            CHECK (content.getRight() <= static_cast<float> (viewport.getWidth()) + 0.01f);
            CHECK (content.getBottom() <= static_cast<float> (viewport.getHeight()) + 0.01f);
            CHECK (juce::jmax (content.getWidth() / static_cast<float> (viewport.getWidth()),
                               content.getHeight() / static_cast<float> (viewport.getHeight())) == Catch::Approx (1.0));
        };
        checkFit();

        for (const int savedScale : { 50, 120 })
        {
            processor.setProjectUiScalePercent (savedScale);
            view->sync();
            view->preferredSizeChanged();
            checkFit();
            CHECK (processor.getProjectUiScalePercent() == savedScale);
        }
        for (const int channels : { 4, 8 })
        {
            view->controller.command ("channels", { channels });
            CHECK (view->layout.channels == channels);
            checkFit();
        }
    }
    editor.setSize (0, 0);
    view->preferredSizeChanged();
    CHECK (editor.getLocalBounds().isEmpty());
}

TEST_CASE ("The full editor uses a shared 4:3 canvas with header transport only in standalone", "[native-ui][header-layout]")
{
    const bool standalone = GENERATE (false, true);
    juce::AudioProcessor::setTypeOfNextNewPlugin (standalone ? juce::AudioProcessor::wrapperType_Standalone : juce::AudioProcessor::wrapperType_VST3);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    PluginEditor editor (processor);
    auto* view = dynamic_cast<NativeEditor*> (editor.getChildComponent (0));
    REQUIRE (view != nullptr);
    REQUIRE (editor.getConstrainer() != nullptr);
    CHECK (editor.getConstrainer()->getFixedAspectRatio() == Catch::Approx (4.0 / 3.0));
    CHECK (view->getWidth() * 3 == view->getHeight() * 4);

    auto layoutAt16 = view->layout;
    layoutAt16.steps = 16;
    const auto standardCell = layoutAt16.cell ({ 0, 0 }).reduced (layoutAt16.cellInsetX(), Layout::cellInsetY);
    for (const int steps : { 16, 24, 32, 48, 64 })
    {
        auto layout = view->layout;
        layout.steps = steps;
        const Location location { 0, steps - 1 };
        const auto body = layout.cell (location).reduced (layout.cellInsetX(), Layout::cellInsetY);
        CHECK (body.getWidth() == Catch::Approx (layout.gridWidth() / static_cast<float> (steps) - 2.0f * layout.cellInsetX()));
        CHECK (body.getHeight() == Catch::Approx (standardCell.getHeight()));
        CHECK (layout.visibleSteps() == steps);
        CHECK (body.getCentreY() == Catch::Approx (view->layout.cell ({ 0, 0 }).getCentreY()));
        CHECK (layout.at (body.getCentre()) == location);
    }

    const juce::Component* play = nullptr;
    const juce::Component* tempo = nullptr;
    const juce::Component* metronome = nullptr;
    const juce::Component* mute = nullptr;
    const juce::Component* timing = nullptr;
    juce::Rectangle<int> bank;
    for (auto* child : view->getChildren())
    {
        if (child->getTitle() == "Play")
            play = child;
        else if (child->getTitle() == "Set tempo")
            tempo = child;
        else if (child->getTitle() == "Metronome")
            metronome = child;
        else if (child->getTitle() == "Timing humanization")
            timing = child;
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getButtonText() == "M")
                mute = button;
            else if (button->getProperties().contains ("patternNumber"))
                bank = bank.getUnion (button->getBounds());
        }
    }
    REQUIRE (play != nullptr);
    REQUIRE (tempo != nullptr);
    REQUIRE (metronome != nullptr);
    REQUIRE (mute != nullptr);
    REQUIRE (timing != nullptr);
    REQUIRE_FALSE (bank.isEmpty());
    CHECK (timing->getRight() < bank.getX());
    CHECK (bank.getRight() < mute->getX());
    CHECK (play->isVisible() == standalone);
    CHECK (tempo->isVisible() == standalone);
    CHECK (metronome->isVisible() == standalone);
    const int rightEdge = view->getWidth() - Layout::outerGutter;
    if (standalone)
    {
        CHECK (tempo->getBottom() < view->layout.toolbarY());
        CHECK (mute->getRight() < play->getX());
        CHECK (tempo->getY() == play->getY());
        CHECK (mute->getX() == tempo->getX());
        CHECK (play->getBottom() < metronome->getY());
        CHECK (mute->getY() == metronome->getY());
        CHECK (play->getRight() == metronome->getRight());
        CHECK (metronome->getRight() == rightEdge);
    }
    else
    {
        CHECK (mute->getRight() == rightEdge);
        CHECK (bank.getCentreY() == mute->getBounds().getCentreY());
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

TEST_CASE ("Discard confirmation uses the native card and only proceeds on explicit discard", "[native-ui][discard][render]")
{
    PluginProcessor processor;
    processor.setProjectMetadata ("Night drive", "", "", "");
    NativeEditor editor (processor);
    const bool compact = GENERATE (false, true);
    if (compact)
        editor.setCompactViewport (true, 814, 409);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    DiscardChanges* dialog = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* candidate = dynamic_cast<DiscardChanges*> (child))
            dialog = candidate;
    REQUIRE (dialog != nullptr);
    CHECK_FALSE (dialog->isVisible());
    int discarded = 0;
    const auto open = [&] { editor.confirmDiscardingChanges ([&] { ++discarded; }); };
    open();
    editor.sync();
    REQUIRE (dialog->isVisible());
    CHECK (dialog->isCurrentlyModal());
    CHECK (dialog->getBounds() == editor.getLocalBounds());
    juce::TextButton* cancel = nullptr;
    juce::TextButton* discard = nullptr;
    for (auto* child : dialog->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getButtonText() == "Cancel") cancel = button;
            if (button->getButtonText() == "Discard Changes") discard = button;
        }
    REQUIRE (cancel != nullptr);
    REQUIRE (discard != nullptr);
    CHECK (discard->findColour (juce::TextButton::buttonColourId) == cancel->findColour (juce::TextButton::buttonColourId));
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        REQUIRE (juce::File (directory).createDirectory().wasOk());
        auto stream = juce::File (directory).getChildFile (compact ? "discard-compact.png" : "discard-desktop.png").createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds()), *stream));
    }
    cancel->onClick();
    CHECK_FALSE (dialog->isVisible());
    CHECK_FALSE (dialog->isCurrentlyModal());
    CHECK (discarded == 0);
    open();
    CHECK (editor.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (dialog->isVisible());
    CHECK (discarded == 0);
    open();
    discard->onClick();
    CHECK_FALSE (dialog->isVisible());
    CHECK_FALSE (dialog->isCurrentlyModal());
    CHECK (discarded == 1);
    open();
    processor.resetProject();
    editor.controller.refresh();
    editor.sync();
    CHECK_FALSE (dialog->isVisible());
    CHECK (discarded == 1);
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
        for (const auto& caption : { juce::String ("Settings"), juce::String ("Project") })
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
    CHECK (header->getNumChildComponents() == 0);
    if (!compact)
        for (auto* child : editor.getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "SAVE AS")
                CHECK (header->getRight() + 6 + Layout::touchTarget == button->getRight());
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
    header->onClick();
    CHECK (details->isVisible());
    details->dismiss();
    CHECK_FALSE (details->isVisible());
    header->onClick();
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
            if (button->getButtonText() == (compact ? "Save as" : "SAVE AS")) saveAs = button;
            if (button->getButtonText() == (compact ? "Save" : "SAVE")) save = button;
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
    const auto pressDetails = [&] (juce::Point<float> position)
    {
        const auto now = juce::Time::getCurrentTime();
        details->mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), position,
                             juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0, details, details,
                             now, position, now, 1, false });
    };
    name->setText ("Discard this");
    pressDetails (details->getLocalBounds().getCentre().toFloat());
    CHECK (details->isVisible());
    pressDetails ({ 0, 0 });
    CHECK_FALSE (details->isVisible());
    CHECK (processor.getProjectName() == "Night drive");
    header->onClick();
    CHECK (editor.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (details->isVisible());
    CHECK (processor.getProjectName() == "Night drive");
    header->onClick();
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
    header->onClick();
    author->setText ("Copy author");
    apply->onClick();
    saveAs->onClick();
    CHECK (action == 5);
    CHECK (processor.getProjectAuthor() == "Copy author");
    header->onClick();
    name->setText ("Stale draft");
    processor.resetProject();
    editor.sync();
    CHECK_FALSE (details->isVisible());
    CHECK (processor.getProjectName() == "Untitled Project");
}

TEST_CASE ("Row order preference reverses presentation without editing the project", "[native-ui][render][row-order]")
{
    PluginProcessor processor;
    for (int ch = 1; ch <= 8; ++ch)
        REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, ch, ch, 1, 100 }));
    NativeEditor editor (processor);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    juce::TextButton* toggle = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getTitle() == "Reverse row order") toggle = button;
        }
    REQUIRE (toggle != nullptr);
    CHECK_FALSE (toggle->isVisible());
    const bool originalOrder = editor.layout.reverseRows;
    if (originalOrder)
        toggle->onClick();
    const auto before = processor.editorDocument();
    editor.controller.selected.set (0);
    toggle->onClick();
    CHECK (toggle->getToggleState());
    CHECK (editor.layout.cell ({ 0, 0 }).getY() > editor.layout.cell ({ 7, 0 }).getY());
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

TEST_CASE ("Footer hints fit, skip obvious controls and resolve nested children", "[native-ui][render]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    FooterHelp* help = nullptr;
    juce::TextButton* select = nullptr;
    juce::TextButton* rate = nullptr;
    PreviewPanel* inspector = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<FooterHelp*> (child)) help = candidate;
        if (auto* panel = dynamic_cast<PreviewPanel*> (child); panel != nullptr && panel->isVisible()) inspector = panel;
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getProperties().contains ("selectMode")) select = button;
            if (button->getTitle() == "Pattern rate") rate = button;
        }
    }
    REQUIRE (help != nullptr);
    REQUIRE (select != nullptr);
    REQUIRE (rate != nullptr);
    REQUIRE (inspector != nullptr);
    CHECK (help->getFont().getHeight() > font (14).getHeight());
    help->setHelpEnabled (true);
    std::function<void (juce::Component&)> audit = [&] (juce::Component& component)
    {
        if (auto* client = dynamic_cast<juce::TooltipClient*> (&component); client && client->getTooltip().isNotEmpty())
        {
            INFO (component.getTitle().toStdString());
            help->showHelpFor (&component);
            CHECK (help->getText() == (inspector->isParentOf (&component) && !component.isEnabled() ? juce::String() : client->getTooltip()));
            juce::TextLayout textLayout;
            juce::AttributedString text (client->getTooltip());
            text.setFont (help->getFont());
            textLayout.createLayout (text, static_cast<float> (help->getWidth()));
            CHECK (textLayout.getHeight() <= static_cast<float> (help->getHeight()));
        }
        for (auto* child : component.getChildren()) audit (*child);
    };
    audit (editor);
    ValueControl* velocity = nullptr;
    for (auto* child : inspector->getChildren())
        if (auto* control = dynamic_cast<ValueControl*> (child); control != nullptr && control->caption == "Velocity")
            velocity = control;
    REQUIRE (velocity != nullptr);
    REQUIRE_FALSE (velocity->isEnabled());
    help->showHelpFor (select);
    REQUIRE (help->getText().isNotEmpty());
    help->showHelpFor (velocity);
    CHECK (help->getText().isEmpty());
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90 }));
    editor.controller.refresh();
    editor.controller.selected.set (0);
    editor.controller.notify();
    REQUIRE (velocity->isEnabled());
    help->showHelpFor (velocity);
    CHECK (help->getText() == velocity->getTooltip());
    help->showHelpFor (rate);
    CHECK (help->getText().isEmpty());
    REQUIRE (select->getTooltip().isNotEmpty());
    juce::Component nested;
    select->addAndMakeVisible (nested);
    const auto now = juce::Time::getCurrentTime();
    help->mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 2, 2 }, {}, 1, 0, 0, 0, 0,
        &nested, &nested, now, { 2, 2 }, now, 0, false));
    CHECK (help->getText() == select->getTooltip());
    select->setEnabled (false);
    help->showHelpFor (&nested);
    CHECK (help->getText() == select->getTooltip());
    select->setEnabled (true);
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

TEST_CASE ("Keyboard modifier toggle gates Shift and Option without disabling explicit modes", "[native-ui]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    PatternGrid grid (controller);
    const juce::ModifierKeys shift (juce::ModifierKeys::shiftModifier);
    const juce::ModifierKeys option (juce::ModifierKeys::altModifier);
    CHECK (controller.useSelectionModifier (shift));
    grid.modifierKeysChanged (option);
    CHECK (grid.displayLayer() == Layer::chance);
    controller.selected.set (0);
    CHECK (grid.isCopyMode());

    controller.keyboardModifiersEnabled = false;
    CHECK_FALSE (controller.useSelectionModifier (shift));
    CHECK_FALSE (grid.isCopyMode());
    controller.selected.reset();
    CHECK (grid.displayLayer() == Layer::velocity);
    controller.selectLatched = true;
    CHECK (controller.useSelectionModifier ({}));
    controller.selectLatched = false;
    controller.beginSelect();
    CHECK (controller.useSelectionModifier ({}));
    controller.cancelSelect();
    controller.selected.set (0);
    grid.copySelection = true;
    CHECK (grid.isCopyMode());
    grid.copySelection = false;

    controller.keyboardModifiersEnabled = true;
    CHECK (controller.useSelectionModifier (shift));
    CHECK (grid.isCopyMode());
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
    editor.controller.selected.set (0);
    editor.controller.selected.set (3);
    const auto selectedCell = editor.controller.cell ({ 0, 0 });
    velocity->onClick();
    CHECK_FALSE (editor.controller.selectLatched);
    CHECK (editor.controller.selected.none());
    CHECK (editor.controller.cell ({ 0, 0 }) == selectedCell);
    checkActive (velocity);
    // Selection alone also highlights Select; either editing layer must exit it.
    editor.controller.selected.set (0);
    editor.controller.notify();
    checkActive (select);
    chance->onClick();
    CHECK (editor.controller.selected.none());
    CHECK (editor.controller.cell ({ 0, 0 }) == selectedCell);
    checkActive (chance);
    velocity->onClick();
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
        editor.controller.selected.set (1);
        press();
        const auto point = grid->layout.cell ({ 0, 0 }).getCentre();
        grid->mouseDown (event (*grid, point));
        grid->mouseUp (event (*grid, point));
        CHECK (editor.controller.selected.none());
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
        CHECK (editor.controller.selected.none());
        CHECK_FALSE (editor.controller.cell ({ 0, 0 }).enabled);
        CHECK_FALSE (editor.controller.cell ({ 0, 2 }).enabled);
    }
    SECTION ("A straight first marquee movement keeps its starting step selected")
    {
        const bool horizontal = GENERATE (false, true);
        const Location start { 2, 3 };
        const Location end = horizontal ? Location { 2, 5 } : Location { 4, 3 };
        REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, start.channel + 1, start.step + 1, 1, 90 }));
        editor.controller.refresh();
        press();
        release();

        const auto first = grid->layout.cell (start).getCentre();
        const auto last = grid->layout.cell (end).getCentre();
        grid->mouseDown (event (*grid, first));
        REQUIRE (editor.controller.selected.test (static_cast<size_t> (start.key())));
        grid->mouseDrag (event (*grid, last));
        CHECK (editor.controller.selected.test (static_cast<size_t> (start.key())));
        CHECK (editor.controller.selected.test (static_cast<size_t> (end.key())));
        CHECK (editor.controller.selected.count() == 3);
        grid->mouseUp (event (*grid, last));
        CHECK (editor.controller.selected.count() == 3);
    }
    SECTION ("Pattern switching does not borrow Shift from Select mode")
    {
        const bool latched = GENERATE (false, true);
        press();
        if (latched)
            release();
        for (auto* child : editor.getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "2")
                button->onClick();
        CHECK (editor.controller.source() == 1);
        CHECK (editor.controller.copySource == -1);
        if (!latched)
            release (100);
        CHECK (editor.controller.shiftDown ({}) == latched);
    }
    SECTION ("Only holding power solos a channel, and tapping power restores mutes")
    {
        press();
        GestureButton* mute = nullptr;
        for (auto* child : grid->getChildren())
            if (auto* button = dynamic_cast<GestureButton*> (child); button && button->getTitle() == "power channel 1")
                mute = button;
        REQUIRE (mute != nullptr);
        const auto point = mute->getLocalBounds().toFloat().getCentre();
        mute->mouseDown (event (*mute, point));
        mute->mouseUp (event (*mute, point));
        CHECK (editor.controller.soloedChannels().none());
        const auto originalMutes = editor.controller.state.playback.sourceChannelMutes[0];
        mute->mouseDown (event (*mute, point));
        mute->mouseUp (event (*mute, point, 500));
        CHECK (editor.controller.soloedChannels().count() == 1);
        CHECK (editor.controller.soloedChannels().test (0));
        const auto& mutes = editor.controller.state.playback.sourceChannelMutes[0];
        CHECK_FALSE (mutes[0]);
        CHECK (mutes[1]);
        mute->mouseDown (event (*mute, point));
        mute->mouseUp (event (*mute, point));
        CHECK (editor.controller.soloedChannels().none());
        CHECK (mutes == originalMutes);
        release (500);
        CHECK_FALSE (editor.controller.shiftDown ({}));
    }
    SECTION ("Row power follows a held swipe across skipped rows and reversals")
    {
        constexpr int leftButton = juce::ModifierKeys::leftButtonModifier;
        const auto swipeEvent = [] (juce::Component& component, juce::Point<float> position, juce::Point<float> origin, int mods)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys (mods), 1, 0, 0, 0, 0,
                                    &component, &component, now, origin, now, 1, position != origin);
        };
        const auto muteFor = [&] (int channel) -> GestureButton&
        {
            for (auto* child : grid->getChildren())
                if (auto* button = dynamic_cast<GestureButton*> (child); button && button->getTitle() == "power channel " + juce::String (channel + 1))
                    return *button;
            FAIL ("Missing row power button");
            std::terminate();
        };
        auto& startButton = muteFor (2);
        const auto centre = startButton.getLocalBounds().toFloat().getCentre();
        const auto bottom = startButton.getLocalPoint (&muteFor (5), centre);
        const auto top = startButton.getLocalPoint (&muteFor (0), centre);
        const auto initialMutes = editor.controller.state.playback.sourceChannelMutes[0];
        startButton.mouseDown (swipeEvent (startButton, centre, centre, leftButton));
        startButton.mouseDrag (swipeEvent (startButton, bottom, centre, leftButton));
        const auto& mutes = editor.controller.state.playback.sourceChannelMutes[0];
        CHECK (mutes[2]);
        CHECK (mutes[3]);
        CHECK (mutes[4]);
        CHECK (mutes[5]);
        CHECK_FALSE (mutes[0]);
        startButton.mouseDrag (swipeEvent (startButton, top, centre, leftButton));
        startButton.mouseUp (swipeEvent (startButton, top, centre, 0));
        CHECK (mutes[0]);
        CHECK (mutes[1]);
        editor.controller.undo();
        CHECK (mutes == initialMutes);

        constexpr int shiftClick = leftButton | juce::ModifierKeys::shiftModifier;
        for (int row : { 2, 5 })
        {
            auto& button = muteFor (row);
            button.mouseDown (swipeEvent (button, centre, centre, shiftClick));
            button.mouseUp (swipeEvent (button, centre, centre, 0));
        }
        const auto solo = editor.controller.soloedChannels();
        CHECK (solo.count() == 2);
        CHECK (solo.test (2));
        CHECK (solo.test (5));
        CHECK (editor.controller.exitSolo());
        CHECK (mutes == initialMutes);
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
        const auto headerLeft = image.getPixelAt (2, 2);
        const auto headerRight = image.getPixelAt (image.getWidth() - 3, 2);
        CHECK (headerLeft.getPerceivedBrightness() > headerRight.getPerceivedBrightness());
        CHECK (image.getPixelAt (2, image.getHeight() - 2) == Palette::app);
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
    juce::MouseEvent gridEvent (juce::Component& grid, juce::Point<float> position, juce::Point<float> origin, int modifiers, bool dragged = false, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &grid, &grid, now, origin, now, clicks, dragged };
    }
}

TEST_CASE ("Grid cell artwork has a small inset and keeps full hit zones", "[native-ui][render]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 80, 60, 4, 5, 3 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.layout.compact = GENERATE (false, true);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    const Location hit { 0, 2 };
    const auto hitBounds = grid.cellBounds (hit);
    const auto fullFace = hitBounds.reduced (grid.layout.cellInsetX(), Layout::cellInsetY);
    const auto face = grid.layout.cellArtwork (hitBounds);
    CHECK (face.getWidth() == Catch::Approx (fullFace.getWidth() * 0.95f));
    CHECK (face.getHeight() == Catch::Approx (fullFace.getHeight() * 0.95f));

    controller.layer = Layer::velocity;
    grid.sync();
    const Location empty { 0, 6 };
    const auto slot = grid.layout.cell (empty);
    const auto paintedSlot = grid.layout.cellArtwork (slot);
    const auto gap = slot.getCentre().withX ((slot.getX() + grid.layout.cellInsetX() + paintedSlot.getX()) * 0.5f);
    REQUIRE_FALSE (grid.layout.cellArtwork (slot).contains (gap));
    grid.mouseDown (gridEvent (grid, gap, gap, juce::ModifierKeys::leftButtonModifier));
    grid.mouseUp (gridEvent (grid, gap, gap, 0));
    CHECK (controller.cell (empty).enabled);
}

TEST_CASE ("Editor cursors follow the control and ignore disabled actions", "[native-ui][cursor]")
{
    using Cursor = juce::MouseCursor;
    LookAndFeel look;
    juce::Component panel;
    juce::TextButton button;
    juce::ComboBox menu;
    juce::TextEditor text;
    juce::Slider fader (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    ValueControl value;
    for (auto* component : std::array<juce::Component*, 5> { &button, &menu, &text, &fader, &value })
        panel.addChildComponent (component);
    CHECK (look.getMouseCursorFor (panel) == Cursor::NormalCursor);
    CHECK (look.getMouseCursorFor (button) == Cursor::PointingHandCursor);
    CHECK (look.getMouseCursorFor (menu) == Cursor::PointingHandCursor);
    CHECK (look.getMouseCursorFor (text) == Cursor::IBeamCursor);
    CHECK (look.getMouseCursorFor (fader) == Cursor::LeftRightResizeCursor);
    CHECK (look.getMouseCursorFor (value) == Cursor::UpDownResizeCursor);
    value.horizontalAdjustment = true;
    CHECK (look.getMouseCursorFor (value) == Cursor::LeftRightResizeCursor);
    button.setMouseCursor (Cursor::UpDownLeftRightResizeCursor);
    CHECK (look.getMouseCursorFor (button) == Cursor::UpDownLeftRightResizeCursor);
    panel.setEnabled (false);
    CHECK (look.getMouseCursorFor (button) == Cursor::NormalCursor);
    CHECK (look.getMouseCursorFor (value) == Cursor::NormalCursor);
}

TEST_CASE ("Pattern copy highlights only the hovered destination", "[native-ui][pattern-copy]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    const auto findPattern = [&] (const juce::String& label)
    {
        for (auto* child : editor.getChildren())
            if (auto* button = dynamic_cast<PatternButton*> (child); button != nullptr && button->getButtonText() == label)
                return button;
        return static_cast<PatternButton*> (nullptr);
    };
    auto* source = findPattern ("1");
    auto* destination = findPattern ("2");
    REQUIRE (source != nullptr);
    REQUIRE (destination != nullptr);

    destination->setState (juce::Button::buttonOver);
    CHECK_FALSE (destination->showsCopyDestination());
    editor.controller.selectSource (0, true);
    CHECK (destination->showsCopyDestination());
    destination->setState (juce::Button::buttonNormal);
    CHECK_FALSE (destination->showsCopyDestination());
    source->setState (juce::Button::buttonOver);
    CHECK_FALSE (source->showsCopyDestination());
    destination->setState (juce::Button::buttonOver);
    CHECK (editor.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (destination->showsCopyDestination());
}

TEST_CASE ("Grid cursor signals copying on the selected cells", "[native-ui][cursor]")
{
    using Cursor = juce::MouseCursor;
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 4, 1, 90 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    const Location selected { 0, 3 };
    controller.selected.set (static_cast<size_t> (selected.key()));
    grid.copySelection = true;
    grid.sync();
    const auto centre = grid.cellBounds (selected).getCentre();
    const auto other = grid.layout.cell ({ 0, 4 }).getCentre();
    grid.mouseMove (gridEvent (grid, centre, centre, 0));
    CHECK (grid.getMouseCursor() == Cursor::CopyingCursor);
    grid.mouseMove (gridEvent (grid, other, other, 0));
    CHECK (grid.getMouseCursor() == Cursor::NormalCursor);
}

TEST_CASE ("Step presets apply and close on release after a right drag", "[native-ui][step-presets]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 4, 3, 1, 90, 100, 4, 7 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 90 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const Location step { 3, 2 };
    const auto active = grid.cellBounds (step).getCentre();
    const auto empty = grid.cellBounds ({ 3, 3 }).getCentre();
    constexpr int right = juce::ModifierKeys::rightButtonModifier;
    grid.mouseDown (gridEvent (grid, empty, empty, right));
    CHECK (grid.hasActiveGesture());
    grid.mouseUp (gridEvent (grid, empty, empty, 0));
    CHECK_FALSE (controller.cell ({ 3, 3 }).enabled);
    grid.mouseDown (gridEvent (grid, active, active, right));
    juce::Component* menu = nullptr;
    for (auto* child : grid.getChildren())
        if (child->getTitle() == "Step presets")
            menu = child;
    REQUIRE (menu != nullptr);
    CHECK (menu->isVisible());
    CHECK_FALSE (menu->getBounds().intersects (grid.cellBounds (step).getSmallestIntegerContainer()));
    const auto findPreset = [menu] (const juce::String& label) -> juce::TextButton*
    {
        for (auto* child : menu->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == label)
                return button;
        return nullptr;
    };
    CHECK (findPreset ("First of 2") == nullptr);
    auto* roll = findPreset ("3-hit roll");
    REQUIRE (roll != nullptr);
    const auto release = grid.getLocalPoint (roll, roll->getLocalBounds().getCentre()).toFloat();
    grid.mouseDrag (gridEvent (grid, release, active, right, true));
    grid.mouseUp (gridEvent (grid, release, active, 0, true));
    CHECK_FALSE (menu->isVisible());
    CHECK (controller.cell (step).roll == 3);
    controller.undo();
    grid.mouseDown (gridEvent (grid, active, active, right));
    grid.mouseUp (gridEvent (grid, active, active, 0));
    CHECK (menu->isVisible());
    CHECK (controller.cell (step).roll == 1);
    auto* triplet = findPreset ("8th triplet");
    REQUIRE (triplet != nullptr);
    triplet->onClick();
    CHECK_FALSE (menu->isVisible());
    CHECK (controller.cell (step).cycle == 4);
    CHECK (controller.cell (step).cycleMask == 7);
    CHECK (controller.cell (step).roll == 3);
    CHECK (controller.cell (step).span == 4);
    CHECK_FALSE (grid.hasActiveGesture());
    CHECK (controller.cell (step).enabled);
    CHECK (controller.selected.none());
    controller.undo();
    CHECK_FALSE (controller.canUndo());
    const auto nearTop = grid.cellBounds ({ 0, 2 }).getCentre();
    grid.mouseDown (gridEvent (grid, nearTop, nearTop, right));
    CHECK (menu->isVisible());
    CHECK_FALSE (menu->getBounds().intersects (grid.cellBounds ({ 0, 2 }).getSmallestIntegerContainer()));
    CHECK (menu->getY() == grid.cellBounds ({ 0, 2 }).getSmallestIntegerContainer().getBottom());
    CHECK (std::abs (menu->getBounds().getCentreX() - juce::roundToInt (nearTop.x)) <= 1);
    CHECK (grid.getLocalBounds().contains (menu->getBounds()));

    grid.mouseDown (gridEvent (grid, empty, empty, right));
    grid.mouseUp (gridEvent (grid, empty, empty, 0));
    REQUIRE (menu->isVisible());
    triplet->onClick();
    CHECK_FALSE (menu->isVisible());
    CHECK (controller.cell ({ 3, 3 }).enabled);
    CHECK (controller.cell ({ 3, 3 }).span == 4);
    CHECK (controller.cell ({ 3, 3 }).roll == 3);
    controller.undo();
    CHECK_FALSE (controller.cell ({ 3, 3 }).enabled);
    CHECK_FALSE (controller.canUndo());
}

TEST_CASE ("Chance cycle strip targets a step or split without editing the cycle", "[native-ui][cycle-selection]")
{
    PluginProcessor processor;
    const int selectionMode = GENERATE (0, 1, 2, 3);
    const bool split = GENERATE (false, true);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 75, 48, 4, 5, split ? 3 : 1 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 4, 1, 55, 42, 3, 2, 2 }));
    EditorController controller (processor);
    controller.layer = Layer::chance;
    PatternGrid grid (controller);
    grid.layout.phoneRowControls = grid.layout.compact = GENERATE (false, true);
    const Location step { 0, 2 }, other { 0, 3 };
    if (selectionMode > 0)
        controller.selected.set (static_cast<size_t> (other.key()));
    if (selectionMode > 1)
        controller.selected.set (static_cast<size_t> (step.key()));
    controller.selectLatched = selectionMode == 3;
    const auto original = controller.cell (step);
    const auto otherOriginal = controller.cell (other);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();
    const auto bounds = grid.cellBounds (step);
    const auto point = bounds.getCentre().withY (bounds.getY() + bounds.getHeight() * 0.90f);
    const auto expected = !split && selectionMode > 1 ? std::vector { step, other } : std::vector { step };
    for (int clicks : { 1, 2 })
    {
        grid.mouseDown (gridEvent (grid, point, point, juce::ModifierKeys::leftButtonModifier, false, clicks));
        grid.mouseUp (gridEvent (grid, point, point, 0, false, clicks));
        CHECK (controller.selection() == expected);
        const auto splitCycle = controller.selectedSplitCycle();
        REQUIRE (splitCycle.has_value() == split);
        if (splitCycle)
            CHECK (splitCycle->index == 1);
        CHECK_FALSE (grid.hasActiveGesture());
        CHECK_FALSE (controller.canUndo());
        CHECK (controller.cell (step) == original);
        CHECK (controller.cell (other) == otherOriginal);
    }
}


TEST_CASE ("Velocity and Probability step clicks remove immediately and recreate with defaults", "[native-ui][cycle-reset]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 75, 48, 4, 5, 3 }));
    EditorController controller (processor);
    controller.layer = GENERATE (Layer::velocity, Layer::chance);
    PatternGrid grid (controller);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();
    const Location step { 0, 2 };
    const auto original = controller.cell (step);
    const auto bounds = grid.cellBounds (step);
    const juce::Point<float> point { bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.25f };
    const auto enabledDefault = Cell { .enabled = true };
    for (int clicks : { 1, 2 })
    {
        grid.mouseDown (gridEvent (grid, point, point, juce::ModifierKeys::leftButtonModifier, false, clicks));
        grid.mouseUp (gridEvent (grid, point, point, 0, false, clicks));
        CHECK (controller.cell (step) == (clicks == 1 ? Cell {} : enabledDefault));
    }
    controller.undo();
    CHECK (controller.cell (step) == Cell {});
    controller.undo();
    CHECK (controller.cell (step) == original);
}

TEST_CASE ("Triplet presets set span and roll while both remain independently editable", "[native-ui][step-presets][triplets]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 67, 4, 5, 2 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 8, 1, 80 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 16, 1, 95 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const Location anchor { 0, 0 };
    const auto openMenu = [&] (Location location)
    {
        const auto point = grid.layout.cell (location).getCentre();
        grid.mouseDown (gridEvent (grid, point, point, juce::ModifierKeys::rightButtonModifier));
        grid.mouseUp (gridEvent (grid, point, point, 0));
    };
    openMenu (anchor);
    const auto preset = [&] (const juce::String& label) -> juce::TextButton&
    {
        for (auto* child : grid.getChildren())
            if (child->getTitle() == "Step presets" && child->isVisible())
                for (auto* entry : child->getChildren())
                    if (auto* button = dynamic_cast<juce::TextButton*> (entry); button && button->getButtonText() == label)
                        return *button;
        FAIL ("Missing preset " + label.toStdString());
        std::terminate();
    };
    PreviewPanel inspector (controller);
    inspector.setSize (1200, 174);
    inspector.setInspectorBounds ({ 288, 0, 890, 174 });
    const auto input = [&] (const juce::String& title) -> ValueControl&
    {
        for (auto* child : inspector.getChildren())
            if (auto* field = dynamic_cast<ValueControl*> (child); field && field->getTitle() == title)
                return *field;
        FAIL ("Missing inspector control " + title.toStdString());
        std::terminate();
    };
    auto& span = input ("Selected span");
    auto& split = input ("Selected split");
    CHECK (grid.openPresetMenu()->getNumChildComponents() == 6);
    CHECK_FALSE (preset ("Quarter triplet").isEnabled());
    REQUIRE (preset ("8th triplet").isEnabled());
    preset ("8th triplet").onClick();
    CHECK (grid.openPresetMenu() == nullptr);
    const auto triplet = controller.cell (anchor);
    CHECK (triplet.span == 4);
    CHECK (triplet.roll == 3);
    CHECK (triplet.velocity == 90);
    CHECK (triplet.probability == 67);
    CHECK (triplet.cycleMask == 5);
    CHECK (grid.cellBounds (anchor).getWidth() == Catch::Approx (4 * grid.layout.cellWidth()));
    CHECK (grid.hitPosition (anchor, 2).x - grid.hitPosition (anchor, 0).x == Catch::Approx (8 * grid.layout.cellWidth() / 3));
    controller.selected.set (static_cast<size_t> (anchor.key()));
    inspector.sync();
    CHECK (span.value == 4);
    CHECK (split.value == 3);
    CHECK (span.getRight() < split.getX());
    CHECK (span.valueFaceBounds().getWidth() == Catch::Approx (span.valueFaceBounds().getHeight()));
    CHECK (split.valueFaceBounds().getWidth() == Catch::Approx (split.valueFaceBounds().getHeight()));
    auto splitAccessibility = split.createAccessibilityHandler();
    splitAccessibility->getValueInterface()->setValue (5.0);
    CHECK (controller.cell (anchor).roll == 5);
    CHECK (controller.cell (anchor).span == 4);
    CHECK (span.keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (controller.cell (anchor).roll == 5);
    CHECK (controller.cell (anchor).span == 3);
    controller.undo();
    controller.undo();
    CHECK (controller.cell (anchor) == triplet);
    openMenu ({ 0, 2 });
    CHECK (preset ("8th triplet").getToggleState());
    CHECK (preset ("8th triplet").getHeight() == 52);
    for (int hits : { 2, 3, 4 })
    {
        auto& roll = preset (juce::String (hits) + "-hit roll");
        REQUIRE (roll.isEnabled());
        roll.onClick();
        auto expected = triplet;
        expected.span = 1;
        expected.roll = hits;
        CHECK (controller.cell (anchor) == expected);
        CHECK (grid.openPresetMenu() == nullptr);
        controller.undo();
        CHECK (controller.cell (anchor) == triplet);
        openMenu ({ 0, 2 });
    }
    openMenu ({ 0, 15 });
    CHECK_FALSE (preset ("16th triplet").isEnabled());
    controller.selected.reset();
    controller.selected.set (static_cast<size_t> (Location { 0, 15 }.key()));
    inspector.sync();
    CHECK (span.value == 1);
    CHECK (span.maximum == 1);
    CHECK_FALSE (span.isEnabled());
    CHECK (split.isEnabled());
    CHECK (preset ("2-hit roll").isEnabled());
    CHECK (controller.cell ({ 0, 15 }).roll == 1);
}

TEST_CASE ("Rhythm menu edits apply to all selected hits in a single undo", "[native-ui][step-presets]")
{
    const bool adjacent = GENERATE (false, true);
    const Location anchor { 0, 3 }, other = adjacent ? Location { 0, 4 } : Location { 1, 5 };
    const Location unrelated { 2, 5 }, empty { 4, 6 };
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, anchor.channel + 1, anchor.step + 1, 1, 90, 78, 4, 5, 2 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, other.channel + 1, other.step + 1, 1, 56, 62 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, unrelated.channel + 1, unrelated.step + 1, 1, 82 }));
    EditorController controller (processor);
    for (const auto location : { anchor, other, empty })
        controller.selected.set (static_cast<size_t> (location.key()));
    const auto selection = controller.selected;
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();

    const auto openMenu = [&] (Location location)
    {
        const auto point = grid.cellBounds (location).getCentre();
        grid.mouseDown (gridEvent (grid, point, point, juce::ModifierKeys::rightButtonModifier));
        grid.mouseUp (gridEvent (grid, point, point, 0));
        REQUIRE (grid.openPresetMenu() != nullptr);
    };
    openMenu (anchor);
    const auto preset = [&] (const juce::String& label) -> juce::TextButton&
    {
        for (auto* entry : grid.openPresetMenu()->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (entry); button && button->getButtonText() == label)
                return *button;
        FAIL ("Missing preset " + label.toStdString());
        std::terminate();
    };
    PreviewPanel inspector (controller);
    inspector.sync();
    const auto input = [&] (const juce::String& title) -> ValueControl&
    {
        for (auto* child : inspector.getChildren())
            if (auto* field = dynamic_cast<ValueControl*> (child); field && field->getTitle() == title)
                return *field;
        FAIL ("Missing inspector control " + title.toStdString());
        std::terminate();
    };

    const auto originalAnchor = controller.cell (anchor);
    const auto originalOther = controller.cell (other);
    const auto originalUnrelated = controller.cell (unrelated);
    const auto checkBatch = [&] (int span, int roll)
    {
        auto expectedAnchor = originalAnchor;
        expectedAnchor.span = span;
        expectedAnchor.roll = roll;
        auto expectedOther = originalOther;
        expectedOther.span = span;
        expectedOther.roll = roll;
        CHECK (controller.cell (anchor) == expectedAnchor);
        CHECK (controller.cell (other) == expectedOther);
        CHECK (controller.cell (unrelated) == originalUnrelated);
        CHECK_FALSE (controller.cell (empty).enabled);
        CHECK (controller.selected == selection);
        REQUIRE (controller.canUndo());
        controller.undo();
        CHECK (controller.cell (anchor) == originalAnchor);
        CHECK (controller.cell (other) == originalOther);
        CHECK_FALSE (controller.canUndo());
        grid.sync();
        inspector.sync();
    };

    auto& split = input ("Selected split");
    CHECK (split.mixed);
    CHECK_FALSE (preset ("2-hit roll").getToggleState());
    split.createAccessibilityHandler()->getValueInterface()->setValue (2.0);
    CHECK_FALSE (split.mixed);
    checkBatch (1, 2);
    auto& roll = preset ("4-hit roll");
    REQUIRE (roll.isEnabled());
    roll.onClick();
    CHECK (grid.openPresetMenu() == nullptr);
    checkBatch (1, 4);

    openMenu (anchor);
    auto& triplet = preset ("16th triplet");
    CHECK (triplet.isEnabled() == !adjacent);
    CHECK (input ("Selected span").isEnabled() == !adjacent);
    triplet.onClick();
    if (adjacent)
    {
        CHECK (controller.cell (anchor) == originalAnchor);
        CHECK (controller.cell (other) == originalOther);
        CHECK_FALSE (controller.canUndo());
    }
    else
    {
        checkBatch (2, 3);
        CHECK (input ("Selected span").keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
        CHECK (controller.cell (anchor).span == 2);
        CHECK (controller.cell (other).span == 2);
        CHECK (controller.cell (anchor).roll == originalAnchor.roll);
        CHECK (controller.cell (other).roll == originalOther.roll);
        controller.undo();
        CHECK (controller.cell (anchor) == originalAnchor);
        CHECK (controller.cell (other) == originalOther);
        CHECK_FALSE (controller.canUndo());
    }

    openMenu (unrelated);
    preset ("3-hit roll").onClick();
    CHECK (controller.cell (unrelated).roll == 3);
    CHECK (controller.cell (anchor) == originalAnchor);
    CHECK (controller.cell (other) == originalOther);
    CHECK (controller.selected == selection);
}

TEST_CASE ("Step presets dismiss on clicks elsewhere in the editor", "[native-ui][step-presets]")
{
    PluginProcessor processor;
    const int channels = GENERATE (1, 8);
    REQUIRE (processor.dispatchUiEngineCommand ("channels", { channels }));
    NativeEditor editor (processor);
    editor.setSize (Layout::fullWidth, Layout::fullHeight);
    PatternGrid* grid = nullptr;
    juce::Component* inspectorControl = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* patternGrid = dynamic_cast<PatternGrid*> (child))
            grid = patternGrid;
        if (auto* inspector = dynamic_cast<PreviewPanel*> (child))
            for (auto* control : inspector->getChildren())
                if (inspector->isVisible() && control->getTitle() == "Selected velocity offset")
                    inspectorControl = control;
    }
    REQUIRE (grid != nullptr);
    REQUIRE (inspectorControl != nullptr);
    REQUIRE_FALSE (inspectorControl->isEnabled());
    const Location edited { grid->layout.channelForRow (channels == 1 ? 0 : 6), 2 };
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, edited.channel + 1, edited.step + 1, 1, 90 }));
    editor.controller.refresh();
    const auto step = grid->cellBounds (edited).getCentre();
    const auto open = [&]
    {
        const auto press = gridEvent (*grid, step, step, juce::ModifierKeys::rightButtonModifier);
        grid->mouseDown (press);
        editor.mouseDown (press);
        grid->mouseUp (gridEvent (*grid, step, step, 0));
        REQUIRE (grid->hasActiveGesture());
    };
    open();
    auto* menu = grid->openPresetMenu();
    REQUIRE (menu != nullptr);
    REQUIRE (menu->getParentComponent() == &editor);
    CHECK (editor.getLocalBounds().contains (menu->getBounds()));
    CHECK (menu->getY() > editor.getLocalPoint (grid, step).y);
    REQUIRE (menu->getNumChildComponents() > 0);
    for (auto* item : menu->getChildren())
        CHECK (menu->getLocalBounds().contains (item->getBounds()));
    const auto selected = static_cast<size_t> (edited.key());
    editor.controller.selected.set (selected);
    editor.mouseDown (gridEvent (*menu, { 1, 1 }, { 1, 1 }, juce::ModifierKeys::leftButtonModifier));
    CHECK (editor.controller.selected.test (selected));
    editor.mouseDown (gridEvent (*menu->getChildComponent (0), { 1, 1 }, { 1, 1 }, juce::ModifierKeys::leftButtonModifier));
    CHECK (grid->hasActiveGesture());
    editor.mouseDown (gridEvent (editor, { 1, 1 }, { 1, 1 }, juce::ModifierKeys::leftButtonModifier));
    CHECK_FALSE (grid->hasActiveGesture());
    open();
    editor.mouseDown (gridEvent (*inspectorControl, { 1, 1 }, { 1, 1 }, juce::ModifierKeys::leftButtonModifier));
    CHECK_FALSE (grid->hasActiveGesture());
    CHECK (editor.controller.cell (edited).enabled);
    CHECK_FALSE (editor.controller.canUndo());
}

TEST_CASE ("Chance strip click opens the row-coloured Cycle inspector for the selection", "[native-ui][cycle-selection]")
{
    PluginProcessor processor;
    const int row = GENERATE (0, 7);
    const int otherRow = row == 0 ? 7 : 0;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, row + 1, 3, 1, 85, 60, 4, 9, 1 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, row + 1, 4, 1, 90, 50, 4, 5, 1 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, otherRow + 1, 3, 1, 90, 50, 4, 5, 1 }));
    NativeEditor editor (processor);
    editor.setSize (Layout::fullWidth, Layout::fullHeight);
    auto& controller = editor.controller;
    controller.layer = Layer::chance;
    controller.drumColors = true;
    const Location first { row, 2 }, clicked { row, 3 }, mixed { otherRow, 2 };
    controller.selected.set (static_cast<size_t> (first.key()));
    controller.selected.set (static_cast<size_t> (clicked.key()));
    editor.sync();

    PatternGrid* grid = nullptr;
    CycleEditor* cycle = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<PatternGrid*> (child))
            grid = candidate;
        if (auto* inspector = dynamic_cast<PreviewPanel*> (child); inspector != nullptr && inspector->isVisible())
            for (auto* control : inspector->getChildren())
                if (auto* candidate = dynamic_cast<CycleEditor*> (control); candidate != nullptr && candidate->isVisible())
                    cycle = candidate;
    }
    REQUIRE (grid != nullptr);
    REQUIRE (cycle != nullptr);
    const auto bounds = grid->cellBounds (clicked);
    const auto point = bounds.getCentre().withY (bounds.getY() + bounds.getHeight() * 0.9f);
    grid->mouseDown (gridEvent (*grid, point, point, juce::ModifierKeys::leftButtonModifier));
    grid->mouseUp (gridEvent (*grid, point, point, 0));
    CHECK (controller.selection() == std::vector { first, clicked });
    CHECK_FALSE (grid->hasActiveGesture());
    CHECK (cycle->isEnabled());
    CHECK_FALSE (controller.canUndo());

    juce::TextButton* firstBeat = nullptr;
    juce::TextButton* secondBeat = nullptr;
    for (auto* child : cycle->getChildren())
        if (auto* beat = dynamic_cast<juce::TextButton*> (child))
        {
            if (beat->getButtonText() == "1") firstBeat = beat;
            if (beat->getButtonText() == "2") secondBeat = beat;
        }
    REQUIRE (firstBeat != nullptr);
    REQUIRE (secondBeat != nullptr);
    const auto beatFill = [firstBeat]
    {
        const auto image = firstBeat->createComponentSnapshot (firstBeat->getLocalBounds());
        return image.getPixelAt (5, image.getHeight() / 2);
    };
    CHECK (beatFill() == Palette::channel (row));
    secondBeat->onClick();
    CHECK (controller.cell (first).cycleMask == 11);
    CHECK (controller.cell (clicked).cycleMask == 11);
    CHECK (controller.cell (mixed).cycleMask == 5);
    controller.undo();
    CHECK (controller.cell (first).cycleMask == 9);
    CHECK (controller.cell (clicked).cycleMask == 5);
    controller.selected.set (static_cast<size_t> (mixed.key()));
    editor.sync();
    CHECK (beatFill() == Palette::accent);
    controller.selected.reset (static_cast<size_t> (mixed.key()));
    controller.drumColors = false;
    editor.sync();
    CHECK (beatFill() == Palette::accent);
}


TEST_CASE ("Select stays highlighted while cells remain selected after releasing modifiers", "[native-ui][selection-heading]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100 }));
    NativeEditor editor (processor);
    if (GENERATE (false, true))
        editor.setCompactViewport (true, 814, 409);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    editor.controller.keyboardModifiersEnabled = true;
    editor.controller.layer = GENERATE (Layer::velocity, Layer::chance);
    PatternGrid* grid = nullptr;
    juce::TextButton* velocity = nullptr;
    juce::TextButton* chance = nullptr;
    juce::TextButton* select = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<PatternGrid*> (child))
            grid = candidate;
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getProperties().contains ("layerSegment"))
                (static_cast<int> (button->getProperties()["layerSegment"]) == 0 ? velocity : chance) = button;
            if (button->getProperties().contains ("selectMode")) select = button;
        }
    }
    REQUIRE (grid != nullptr);
    REQUIRE (velocity != nullptr);
    REQUIRE (chance != nullptr);
    REQUIRE (select != nullptr);
    const auto checkSelect = [&]
    {
        CHECK (select->getToggleState());
        CHECK_FALSE (velocity->getToggleState());
        CHECK_FALSE (chance->getToggleState());
    };
    const auto point = grid->layout.cell ({ 0, 0 }).getCentre();
    const int shift = juce::ModifierKeys::shiftModifier;
    editor.modifierKeysChanged (juce::ModifierKeys (shift));
    grid->mouseDown (gridEvent (*grid, point, point, shift | juce::ModifierKeys::leftButtonModifier));
    grid->mouseUp (gridEvent (*grid, point, point, shift));
    REQUIRE (editor.controller.selected.test (0));
    editor.modifierKeysChanged ({});
    CHECK_FALSE (grid->isSelectMode());
    checkSelect();
    editor.controller.cancelSelect();
    checkSelect();
    editor.controller.inspectorHoverLayer = Layer::chance;
    editor.controller.notify();
    checkSelect();
    editor.controller.inspectorHoverLayer.reset();
    velocity->onClick();
    CHECK (editor.controller.selected.none());
    CHECK (velocity->getToggleState());
    CHECK_FALSE (select->getToggleState());
    editor.controller.selected.set (0);
    editor.controller.notify();
    checkSelect();
    chance->onClick();
    CHECK (editor.controller.selected.none());
    CHECK (chance->getToggleState());
    CHECK_FALSE (select->getToggleState());
    editor.mouseDown (gridEvent (editor, {}, {}, juce::ModifierKeys::leftButtonModifier));
    CHECK (editor.controller.selected.none());
    CHECK (chance->getToggleState());
    CHECK_FALSE (select->getToggleState());
    CHECK_FALSE (velocity->getToggleState());
    CHECK_FALSE (editor.controller.canUndo());
}

TEST_CASE ("Background clicks deselect while grid and nested controls keep the selection", "[native-ui][background-deselect]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90 }));
    NativeEditor editor (processor);
    if (GENERATE (false, true))
        editor.setCompactViewport (true, 814, 409);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    const auto before = processor.editorDocument();
    const auto select = [&]
    {
        editor.controller.selected.set (0);
        editor.controller.selected.set (1);
        editor.controller.notify();
    };
    const auto click = [&] (juce::Component& target)
    {
        const auto point = target.getLocalBounds().toFloat().getCentre();
        editor.mouseDown (gridEvent (target, point, point, juce::ModifierKeys::leftButtonModifier).getEventRelativeTo (&editor));
    };
    select();
    click (editor);
    CHECK (editor.controller.selected.none());
    for (auto* child : editor.getChildren())
    {
        if (dynamic_cast<PreviewPanel*> (child) != nullptr || dynamic_cast<FooterHelp*> (child) != nullptr)
        {
            select();
            click (*child);
            CHECK (editor.controller.selected.none());
        }
    }
    const auto checkControls = [&] (auto&& self, juce::Component& parent) -> void
    {
        for (auto* child : parent.getChildren())
        {
            if (dynamic_cast<juce::Button*> (child) != nullptr || dynamic_cast<ValueControl*> (child) != nullptr
                || dynamic_cast<juce::TextEditor*> (child) != nullptr || dynamic_cast<PatternGrid*> (child) != nullptr
                || child->getTitle() == "Cycle length")
            {
                select();
                click (*child);
                REQUIRE (editor.controller.selected.count() == 2);
                // Events delivered relative to the editor still retain their
                // original nested target, such as a label inside a button.
                juce::Component nested;
                child->addChildComponent (nested);
                click (nested);
                REQUIRE (editor.controller.selected.count() == 2);
            }
            self (self, *child);
        }
    };
    checkControls (checkControls, editor);
    CHECK (processor.editorDocument() == before);
    CHECK_FALSE (editor.controller.canUndo());
}

TEST_CASE ("Clicks on margins around the scaled editor deselect cells", "[native-ui][background-deselect]")
{
    PluginProcessor processor;
    PluginEditor editor (processor);
    NativeEditor* native = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* view = dynamic_cast<NativeEditor*> (child))
            native = view;
    REQUIRE (native != nullptr);
    native->controller.selected.set (0);
    const juce::Point<float> point { 0, 0 };
    editor.mouseDown (gridEvent (editor, point, point, juce::ModifierKeys::leftButtonModifier));
    CHECK (native->controller.selected.none());
    CHECK_FALSE (native->controller.canUndo());
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
        const auto handle = grid.loopHandleBounds (0, false).getCentre();
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
    CHECK (controller.cell ({ 0, 0 }).velocity == 113);
    grid.cancelGesture();
    CHECK_FALSE (controller.cell ({ 0, 0 }).enabled);
    const auto beforeRightClick = controller.cell ({ 0, 0 });
    const auto rightClick = gridEvent (grid, start, start, juce::ModifierKeys::rightButtonModifier);
    grid.mouseDown (rightClick);
    grid.mouseUp (rightClick);
    CHECK (controller.cell ({ 0, 0 }) == beforeRightClick);
    CHECK_FALSE (controller.canUndo());
}

TEST_CASE ("Velocity edge hints require a neighboring hit and two active steps inside the loop", "[native-ui]")
{
    PluginProcessor processor;
    const bool leftActive = GENERATE (false, true);
    const bool rightActive = GENERATE (false, true);
    const bool loopStartsHere = GENERATE (false, true);
    const bool distantActive = GENERATE (false, true);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, leftActive ? 1 : 0, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 2, rightActive ? 1 : 0, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 6, distantActive ? 1 : 0, 90 }));
    if (loopStartsHere)
        REQUIRE (processor.dispatchUiEngineCommand ("channel_loop_length", { 1, 15, 2 }));
    EditorController controller (processor);
    REQUIRE (controller.cell ({ 0, 0 }).enabled == leftActive);
    REQUIRE (controller.cell ({ 0, 1 }).enabled == rightActive);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const auto bounds = grid.layout.cell ({ 0, 1 });
    const auto edge = juce::Point<float> (loopStartsHere ? grid.loopHandleBounds (0, false).getRight() + 3.0f : bounds.getX(), bounds.getCentreY());
    const auto hidden = grid.createComponentSnapshot (grid.getLocalBounds());
    grid.mouseMove (gridEvent (grid, edge.translated (loopStartsHere ? -2.0f : -8.0f, 0), edge, 0));
    // Drain callbacks while waiting so JUCE's timer thread can keep advancing.
    for (int i = 0; i < 70; ++i)
    {
        juce::Thread::sleep (10);
        juce::Timer::callPendingTimersSynchronously();
    }
    const auto visible = grid.createComponentSnapshot (grid.getLocalBounds());
    const auto marker = juce::Point<float> (bounds.getX(), edge.y);
    const auto region = juce::Rectangle<float> (8.0f, 8.0f).withCentre (marker).getSmallestIntegerContainer();
    bool changed = false;
    for (int y = region.getY(); y < region.getBottom(); ++y)
        for (int x = region.getX(); x < region.getRight(); ++x)
            changed = changed || hidden.getPixelAt (x, y) != visible.getPixelAt (x, y);
    const bool activeLeftInLoop = leftActive && !loopStartsHere;
    const int activeSteps = static_cast<int> (activeLeftInLoop) + static_cast<int> (rightActive) + static_cast<int> (distantActive);
    REQUIRE (changed == (activeSteps >= 2 && (activeLeftInLoop || rightActive)));
    if (changed)
    {
        juce::Thread::sleep (125);
        const auto nextFrame = grid.createComponentSnapshot (grid.getLocalBounds());
        const auto rippleRegion = region.expanded (9);
        bool animated = false;
        for (int y = rippleRegion.getY(); y < rippleRegion.getBottom(); ++y)
            for (int x = rippleRegion.getX(); x < rippleRegion.getRight(); ++x)
                animated = animated || visible.getPixelAt (x, y) != nextFrame.getPixelAt (x, y);
        CHECK (animated);
    }
    for (const float dx : { 8.0f, -8.0f, 2.0f })
    {
        grid.mouseMove (gridEvent (grid, edge.translated (loopStartsHere ? dx * 0.25f : dx, 0), edge, 0));
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
    if (activeSteps == 1 && !loopStartsHere)
    {
        const int leftButton = juce::ModifierKeys::leftButtonModifier;
        grid.mouseDown (gridEvent (grid, edge, edge, leftButton));
        grid.mouseDrag (gridEvent (grid, edge.translated (0, -12), edge, leftButton, true));
        CHECK (grid.getMouseCursor() != juce::MouseCursor::CrosshairCursor);
        grid.cancelGesture();
    }
}

TEST_CASE ("Velocity lines can start beside either loop handle", "[native-ui]")
{
    const bool closing = GENERATE (false, true);
    const bool shortened = GENERATE (false, true);
    const int first = shortened ? 2 : 0;
    const int end = shortened ? 12 : 16;
    const int steps = end - first;
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("channel_loop_length", { 1, steps, first + 1 }));
    const int step = closing ? end - 1 : first;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, step + 1, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, closing ? first + 1 : end, 1, 90 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (Layout::fullWidth, 554);
    grid.sync();
    const auto handle = grid.loopHandleBounds (0, closing);
    const auto row = grid.layout.cell ({ 0, step });
    const float seamX = closing ? row.getRight() : row.getX();
    const float gutter = closing ? handle.getX() - seamX : seamX - handle.getRight();
    CHECK (gutter == Catch::Approx (Layout::rowVelocityGutter));
    const auto edge = juce::Point<float> (seamX + (closing ? 5.0f : -5.0f), row.getCentreY());
    const auto seam = juce::Point<float> (seamX, edge.y).roundToInt();
    const auto marker = juce::Rectangle<int> (closing ? seam.x - 7 : seam.x + 3, seam.y - 6, 4, 12);
    const auto snapshot = [&] { return grid.createComponentSnapshot (grid.getLocalBounds()); };
    const auto changedBrightness = [&] (const juce::Image& before, const juce::Image& after)
    {
        float brightest = 0.0f;
        for (int y = marker.getY(); y < marker.getBottom(); ++y)
            for (int x = marker.getX(); x < marker.getRight(); ++x)
                if (const auto pixel = after.getPixelAt (x, y); pixel != before.getPixelAt (x, y))
                    brightest = juce::jmax (brightest, pixel.getPerceivedBrightness());
        return brightest;
    };
    const auto idle = snapshot();
    grid.mouseMove (gridEvent (grid, edge, edge, 0));
    juce::Thread::sleep (100);
    const auto hovered = snapshot();
    CHECK (changedBrightness (idle, hovered) > 0.75f);
    grid.mouseExit (gridEvent (grid, edge, edge, 0));
    CHECK (changedBrightness (idle, snapshot()) == 0.0f);
    const auto destination = grid.layout.cell ({ 0, closing ? end - 3 : first + 3 }).getTopLeft().translated (0, 2);
    const int left = juce::ModifierKeys::leftButtonModifier;
    const auto drawLine = [&] (juce::Point<float> press)
    {
        grid.mouseDown (gridEvent (grid, press, press, left));
        grid.mouseDrag (gridEvent (grid, destination, press, left, true));
        grid.mouseUp (gridEvent (grid, destination, press, 0, true));
        CHECK (controller.cell ({ 0, step }).velocity == 64);
        CHECK (controller.cell ({ 0, step }).enabled);
        CHECK (controller.loop (0).loopStart == first);
        CHECK (controller.loop (0).loopLength == steps);
        controller.undo();
        CHECK (controller.cell ({ 0, step }).velocity == 90);
    };
    drawLine (edge);
    drawLine (edge.withX (seamX + (closing ? -8.0f : 8.0f)));
    // A drag inside the cell still scrubs velocity.
    const auto body = edge.withX (seamX + (closing ? -12.0f : 12.0f));
    const auto raised = body.translated (0, -20.0f);
    grid.mouseDown (gridEvent (grid, body, body, left));
    grid.mouseDrag (gridEvent (grid, raised, body, left, true));
    grid.mouseUp (gridEvent (grid, raised, body, 0, true));
    CHECK (controller.cell ({ 0, step }).velocity > 90);
    CHECK (controller.loop (0).loopLength == steps);

}

TEST_CASE ("Freeform velocity drawing edits only active steps on the focused row", "[native-ui][velocity-draw]")
{
    PluginProcessor processor;
    const int stepCount = GENERATE (16, 64);
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, stepCount }));
    for (const int step : { 2, 4, 6, 8 })
        REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, step, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("channel_loop_length", { 1, 6, 1 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 2, 4, 1, 90 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();
    const auto startCell = grid.cellBounds ({ 0, 1 });
    const auto edge = juce::Point<float> (startCell.getX(), startCell.getCentreY());
    const auto inactive = controller.cell ({ 0, 2 });
    const auto other = grid.cellBounds ({ 1, 3 }).getCentre();
    const auto snapshot = [&] { return grid.createComponentSnapshot (grid.getLocalBounds()); };
    const auto idle = snapshot();
    const auto firstClick = juce::Time::getCurrentTime();
    const auto tapEdge = [&] (juce::Point<float> point, juce::Time time)
    {
        const auto event = [&] (int modifiers)
        {
            return juce::MouseEvent { juce::Desktop::getInstance().getMainMouseSource(), point, juce::ModifierKeys (modifiers),
                                      1, 0, 0, 0, 0, &grid, &grid, time, point, time, 1, false };
        };
        grid.mouseDown (event (juce::ModifierKeys::leftButtonModifier));
        grid.mouseUp (event (0));
    };

    tapEdge (edge, firstClick);
    CHECK (controller.cell ({ 0, 1 }).enabled);
    CHECK_FALSE (controller.canUndo());
    tapEdge (edge.translated (6.0f, 0.0f), firstClick + juce::RelativeTime::milliseconds (220));
    REQUIRE (controller.editing());
    CHECK (controller.cell ({ 0, 1 }).velocity == 90);
    const auto focused = snapshot();
    const auto pixel = other.roundToInt();
    CHECK (focused.getPixelAt (pixel.x, pixel.y).getPerceivedBrightness()
           < idle.getPixelAt (pixel.x, pixel.y).getPerceivedBrightness());

    const auto high = startCell.getCentre().withY (startCell.getY() + 1.0f);
    const auto endCell = grid.cellBounds ({ 0, 5 });
    const auto low = endCell.getCentre().withY (endCell.getBottom() - 1.0f);
    grid.mouseMove (gridEvent (grid, high, edge, 0));
    CHECK (grid.getMouseCursor() == juce::MouseCursor::CrosshairCursor);
    grid.mouseMove (gridEvent (grid, other, edge, 0));
    CHECK (grid.getMouseCursor() == juce::MouseCursor::NormalCursor);
    const auto rowControls = high.withX (grid.layout.gridLeft() - 8.0f);
    grid.mouseMove (gridEvent (grid, rowControls, edge, 0));
    CHECK (grid.getMouseCursor() == juce::MouseCursor::NormalCursor);
    grid.mouseMove (gridEvent (grid, low, edge, 0));
    CHECK (grid.getMouseCursor() == juce::MouseCursor::CrosshairCursor);
    grid.mouseExit (gridEvent (grid, low, edge, 0));
    CHECK (grid.getMouseCursor() == juce::MouseCursor::NormalCursor);
    CHECK (controller.cell ({ 0, 1 }).velocity == 90);
    CHECK (controller.cell ({ 0, 5 }).velocity == 90);

    const auto press = high.translated (0.0f, 6.0f);
    const int left = juce::ModifierKeys::leftButtonModifier;
    grid.mouseDown (gridEvent (grid, press, press, left));
    grid.mouseDrag (gridEvent (grid, high, press, left, true));
    grid.mouseDrag (gridEvent (grid, low, press, left, true));
    CHECK (controller.cell ({ 0, 1 }).velocity > 120);
    CHECK (controller.cell ({ 0, 3 }).velocity > 60);
    CHECK (controller.cell ({ 0, 3 }).velocity < 70);
    CHECK (controller.cell ({ 0, 5 }).velocity <= 5);
    CHECK (controller.cell ({ 0, 2 }) == inactive);
    grid.mouseDrag (gridEvent (grid, grid.cellBounds ({ 0, 7 }).getCentre(), press, left, true));
    grid.mouseDrag (gridEvent (grid, other, press, left, true));
    grid.mouseUp (gridEvent (grid, other, press, 0, true));
    CHECK (grid.hasActiveGesture());
    CHECK (controller.cell ({ 0, 7 }).velocity == 90);
    CHECK (controller.cell ({ 1, 3 }).velocity == 90);

    grid.mouseDown (gridEvent (grid, other, other, left));
    grid.mouseUp (gridEvent (grid, other, other, 0));
    CHECK_FALSE (grid.hasActiveGesture());
    CHECK_FALSE (controller.editing());
    CHECK (controller.cell ({ 1, 3 }).enabled);
    CHECK (snapshot().getPixelAt (pixel.x, pixel.y) == idle.getPixelAt (pixel.x, pixel.y));
    controller.undo();
    for (const int step : { 1, 3, 5, 7 })
        CHECK (controller.cell ({ 0, step }).velocity == 90);
    CHECK_FALSE (controller.canUndo());
}

TEST_CASE ("Holding a split segment selects the span's step without changing it", "[native-ui][split-edits]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    controller.layer = GENERATE (Layer::velocity, Layer::chance);
    const Location step { 0, 4 };
    const Cell original { .enabled = true, .velocity = 84, .roll = 3, .span = 4 };
    controller.begin ("Set up span");
    controller.editCell (step, original);
    controller.commit();
    controller.clearHistory();
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();

    const bool divider = GENERATE (false, true);
    const auto body = grid.layout.cellArtwork (grid.cellBounds (step));
    const auto point = body.getCentre().withX (body.getX() + body.getWidth() * (divider ? 2.0f / 3.0f : 5.0f / 6.0f));
    grid.mouseMove (gridEvent (grid, point, point, 0));
    grid.mouseDown (gridEvent (grid, point, point, juce::ModifierKeys::leftButtonModifier));
    CHECK (controller.selected.none());
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1600.0;
    while (controller.selected.none() && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        juce::Thread::sleep (10);
        juce::Timer::callPendingTimersSynchronously();
    }
    const std::vector<Location> expected { step };
    CHECK (controller.selection() == expected);
    grid.mouseUp (gridEvent (grid, point, point, 0));
    CHECK (controller.selection() == expected);
    CHECK (controller.cell (step) == original);
    CHECK_FALSE (controller.canUndo());
}

TEST_CASE ("Tightly packed splits target one cycle strip and still scrub individual probabilities", "[native-ui][split-edits]")
{
    PluginProcessor processor;
    const int steps = GENERATE (16, 64);
    const int splits = steps == 16 ? 3 : 8;
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, steps }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 4, 1, 90, 65, 3, 5, splits }));
    EditorController controller (processor);
    controller.layer = Layer::chance;
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();

    const Location step { 0, 3 };
    const auto original = controller.cell (step);
    const auto bounds = grid.layout.cellArtwork (grid.cellBounds (step));
    const auto point = juce::Point<float> (bounds.getRight() - bounds.getWidth() / (2.0f * static_cast<float> (splits)),
                                          bounds.getY() + bounds.getHeight() * 0.9f);
    const int left = juce::ModifierKeys::leftButtonModifier;
    grid.mouseDown (gridEvent (grid, point, point, left));
    grid.mouseUp (gridEvent (grid, point, point, 0));
    CHECK (controller.selection() == std::vector { step });
    REQUIRE (controller.selectedSplitCycle());
    CHECK (controller.selectedSplitCycle()->index == splits - 1);
    CHECK_FALSE (grid.hasActiveGesture());
    CHECK (controller.cell (step) == original);
    CHECK_FALSE (controller.canUndo());
    controller.selected.reset();
    grid.sync();

    const auto fader = point.withY (bounds.getY() + bounds.getHeight() * 0.3f);
    const auto end = fader.translated (0, 20);
    grid.mouseDown (gridEvent (grid, fader, fader, left));
    grid.mouseDrag (gridEvent (grid, end, fader, left, true));
    grid.mouseUp (gridEvent (grid, end, fader, 0, true));
    const auto& edited = controller.cell (step);
    const int probability = edited.splitGates[static_cast<size_t> (splits - 1)].probability();
    CHECK (probability < 80);
    auto expected = original;
    expected.splitGates[static_cast<size_t> (splits - 1)].setProbability (probability);
    CHECK (edited == expected);
    controller.undo();
    CHECK (controller.cell (step) == original);
}

TEST_CASE ("Velocity line and freeform drawing interpolate individual split hits", "[native-ui][split-edits][velocity-draw]")
{
    const bool freeform = GENERATE (false, true);
    PluginProcessor processor;
    EditorController controller (processor);
    const Location step { 0, 1 };
    Cell original { .enabled = true, .velocity = 84, .roll = 3, .span = 3 };
    original.splitGates[1].setProbability (23);
    controller.begin ("Set up steps");
    controller.editCell (step, original);
    controller.editCell ({ 0, 7 }, Cell { .enabled = true, .velocity = 70 });
    controller.commit();
    controller.clearHistory();
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const auto region = grid.cellBounds (step);
    const auto start = region.getTopLeft().translated (0, 2);
    const auto end = region.getBottomRight().translated (-2, -2);
    const int left = juce::ModifierKeys::leftButtonModifier;
    if (freeform)
    {
        for (int click = 1; click <= 2; ++click)
        {
            grid.mouseDown (gridEvent (grid, start, start, left, false, click));
            grid.mouseUp (gridEvent (grid, start, start, 0, false, click));
        }
        REQUIRE (controller.editing());
    }
    const auto drawStart = freeform ? start.translated (2, 0) : start;
    grid.mouseDown (gridEvent (grid, drawStart, drawStart, left));
    grid.mouseDrag (gridEvent (grid, end, drawStart, left, true));
    grid.mouseUp (gridEvent (grid, end, drawStart, 0, true));
    if (freeform)
        grid.finishVelocityDrawing();
    const auto changed = controller.cell (step);
    CHECK (changed.velocity == original.velocity);
    CHECK (changed.splitGates == original.splitGates);
    CHECK (changed.splitVelocity (0) > changed.splitVelocity (1));
    CHECK (changed.splitVelocity (1) > changed.splitVelocity (2));
    CHECK (controller.cell ({ 0, 7 }).velocity == 70);
    REQUIRE (controller.canUndo());
    controller.undo();
    CHECK (controller.cell (step) == original);
}

TEST_CASE ("A lone split step accepts velocity lines and sweeps within its painted divisions", "[native-ui][split-edits][velocity-draw]")
{
    const int span = GENERATE (1, 3);
    const int stepCount = GENERATE (16, 64);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, stepCount }));
    EditorController controller (processor);
    const Location step { 0, 5 };
    Cell original { .enabled = true, .velocity = 84, .roll = 3, .span = span };
    original.splitGates[1].setProbability (23);
    controller.begin ("Set up split step");
    controller.editCell (step, original);
    controller.commit();
    controller.clearHistory();
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();

    const auto body = grid.layout.cellArtwork (grid.cellBounds (step));
    const float width = body.getWidth() / 3.0f;
    const int left = juce::ModifierKeys::leftButtonModifier;
    SECTION ("A line between two internal dividers edits only the segment between them")
    {
        const juce::Point<float> start { body.getX() + width, body.getY() + 3.0f };
        const juce::Point<float> end { body.getX() + 2.0f * width, body.getY() + 12.0f };
        grid.mouseMove (gridEvent (grid, start, start, 0));
        grid.mouseDown (gridEvent (grid, start, start, left));
        grid.mouseDrag (gridEvent (grid, end, start, left, true));
        grid.mouseUp (gridEvent (grid, end, start, 0, true));
        const auto changed = controller.cell (step);
        CHECK (changed.splitVelocity (0) == original.velocity);
        CHECK (changed.splitVelocity (1) > original.velocity);
        CHECK (changed.splitVelocity (2) == original.velocity);
    }
    SECTION ("A manual drag sweeps from one split body into the next")
    {
        const juce::Point<float> start { body.getX() + 0.5f * width, body.getCentreY() };
        const auto raised = start.translated (0, -12);
        const auto end = start.translated (width, -24);
        grid.mouseDown (gridEvent (grid, start, start, left));
        grid.mouseDrag (gridEvent (grid, raised, start, left, true));
        grid.mouseDrag (gridEvent (grid, end, start, left, true));
        grid.mouseUp (gridEvent (grid, end, start, 0, true));
        const auto changed = controller.cell (step);
        CHECK (changed.splitVelocity (0) > original.velocity);
        CHECK (changed.splitVelocity (1) > changed.splitVelocity (0));
        CHECK (changed.splitVelocity (2) == original.velocity);
    }
    SECTION ("A double-click on a divider starts freeform drawing")
    {
        const juce::Point<float> divider { body.getX() + width, body.getCentreY() };
        for (int click = 1; click <= 2; ++click)
        {
            grid.mouseDown (gridEvent (grid, divider, divider, left, false, click));
            grid.mouseUp (gridEvent (grid, divider, divider, 0, false, click));
        }
        REQUIRE (controller.editing());
        const auto start = divider.translated (width * 0.5f, 0);
        const auto end = start.withY (body.getY() + 1.0f);
        grid.mouseDown (gridEvent (grid, start, start, left));
        grid.mouseDrag (gridEvent (grid, end, start, left, true));
        grid.mouseUp (gridEvent (grid, end, start, 0, true));
        grid.finishVelocityDrawing();
        const auto changed = controller.cell (step);
        CHECK (changed.splitVelocity (0) == original.velocity);
        CHECK (changed.splitVelocity (1) > original.velocity);
        CHECK (changed.splitVelocity (2) == original.velocity);
    }
    const auto changed = controller.cell (step);
    CHECK (changed.velocity == original.velocity);
    CHECK (changed.splitGates == original.splitGates);
    CHECK_FALSE (controller.cell ({ 0, step.step + 1 }).enabled);
    REQUIRE (controller.canUndo());
    controller.undo();
    CHECK (controller.cell (step) == original);
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
    const float leftEdge = juce::jmax (bounds.getX(), grid.loopHandleBounds (0, false).getRight());
    const float rightEdgeX = juce::jmin (bounds.getRight(), grid.loopHandleBounds (0, true).getX());
    const juce::Point<float> start { rightEdge ? rightEdgeX - 1 : leftEdge + 1, bounds.getCentreY() };
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

TEST_CASE ("Velocity lines grab and snap at swung cell edges", "[native-ui][swing-display]")
{
    const bool useTrailingEdge = GENERATE (false, true);
    const float grabOffset = GENERATE (-8.0f, 8.0f);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 7, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("swing", { 80 }));
    REQUIRE (processor.dispatchUiEngineCommand ("swing_subdivision", { 1 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const auto bounds = grid.cellBounds ({ 0, 2 });
    const int firstStep = useTrailingEdge ? 3 : 2;
    const float edgeX = useTrailingEdge ? bounds.getRight() : bounds.getX();
    const float straightX = grid.layout.cell ({ 0, firstStep }).getX();
    REQUIRE (edgeX - straightX > grid.layout.cellWidth() * 0.5f);
    const juce::Point<float> start { edgeX + grabOffset, bounds.getBottom() - 99.0f / 126.0f * bounds.getHeight() };
    const juce::Point<float> end { grid.cellBounds ({ 0, 6 }).getX(), bounds.getBottom() - 19.0f / 126.0f * bounds.getHeight() };
    const int left = juce::ModifierKeys::leftButtonModifier;
    const auto before = processor.editorDocument();

    SECTION ("Drawing a line uses the step boundaries at both shifted edges")
    {
        grid.mouseDown (gridEvent (grid, start, start, left));
        grid.mouseDrag (gridEvent (grid, end, start, left, true));
        grid.mouseUp (gridEvent (grid, end, start, 0, true));
        CHECK (controller.cell ({ 0, firstStep }).velocity == 100);
        CHECK (controller.cell ({ 0, 5 }).velocity == 20);
        CHECK (controller.cell ({ 0, 6 }).velocity == 90);
        CHECK_FALSE (controller.cell ({ 0, 5 }).enabled);
        REQUIRE (controller.canUndo());
        controller.undo();
        CHECK (processor.editorDocument() == before);
    }
    SECTION ("The old straight edge is available for ordinary cell clicks")
    {
        const juce::Point<float> point { straightX + 1.0f, start.y };
        grid.mouseDown (gridEvent (grid, point, point, left));
        grid.mouseUp (gridEvent (grid, point, point, 0));
        grid.focusLost (juce::Component::focusChangedByMouseClick);
        CHECK_FALSE (controller.cell ({ 0, 2 }).enabled);
        REQUIRE (controller.canUndo());
        controller.undo();
        CHECK (processor.editorDocument() == before);
    }
    SECTION ("A click inside the wider drag target still toggles the step")
    {
        const auto point = juce::Point<float> (edgeX + (useTrailingEdge ? -8.0f : 8.0f), start.y);
        grid.mouseDown (gridEvent (grid, point, point, left));
        grid.mouseUp (gridEvent (grid, point, point, 0));
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + juce::MouseEvent::getDoubleClickTimeout() + 150.0;
        while (!controller.canUndo() && juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            juce::Thread::sleep (10);
            juce::Timer::callPendingTimersSynchronously();
        }
        CHECK_FALSE (controller.cell ({ 0, 2 }).enabled);
        REQUIRE (controller.canUndo());
        controller.undo();
        CHECK (processor.editorDocument() == before);
    }
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
    CHECK (controller.cell ({ 0, 0 }).velocity == juce::jmin (127, initialVelocity + 13));
    grid.mouseDrag (gridEvent (grid, start.translated (0, 20), start, left, true));
    CHECK (controller.cell ({ 0, 0 }).velocity == initialVelocity - 13);
    grid.mouseDrag (gridEvent (grid, start, start, left, true));
    CHECK (controller.cell ({ 0, 0 }).velocity == initialVelocity);
    grid.cancelGesture();
    CHECK (controller.cell ({ 0, 0 }).velocity == initialVelocity);
}

TEST_CASE ("Selection preserves the cell display preference and mutes unselected active cells", "[native-ui][render][cell-display]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 70, 80, 3, 2, 4 }));
    EditorController controller (processor);
    controller.graphicalCellValues = GENERATE (false, true);
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

TEST_CASE ("Muted steps fade toward the empty grid in every layer", "[native-ui][render]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 100, 80, 3, 2, 3 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    const Location step { 0, 2 };
    const auto crop = grid.layout.cell (step).getSmallestIntegerContainer();
    const auto capture = [&] { grid.sync(); return grid.createComponentSnapshot (crop); };
    const auto distance = [] (const juce::Image& a, const juce::Image& b)
    {
        int sum = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
            {
                const auto ca = a.getPixelAt (x, y), cb = b.getPixelAt (x, y);
                sum += std::abs (ca.getRed() - cb.getRed()) + std::abs (ca.getGreen() - cb.getGreen()) + std::abs (ca.getBlue() - cb.getBlue());
            }
        return sum;
    };
    controller.layer = GENERATE (Layer::velocity, Layer::chance, Layer::nudge, Layer::roll, Layer::cycle);
    const auto on = capture();
    REQUIRE (processor.dispatchUiEngineCommand ("source_channel_mute", { 1, 1, true }));
    controller.refresh();
    const auto muted = capture();
    CHECK (controller.cell (step).enabled);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 0 }));
    controller.refresh();
    const auto empty = capture();
    const int mutedContrast = distance (muted, empty);
    CHECK (mutedContrast > 0);
    CHECK (mutedContrast * 4 < distance (on, empty));
}

TEST_CASE ("Cells outside a channel loop are completely invisible in every layer", "[native-ui][render][loop-visibility]")
{
    PluginProcessor processor;
    for (const int step : { 1, 4, 8 })
        REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, step, 1, 90, 60, 4, 5, 3 }));
    REQUIRE (processor.dispatchUiEngineCommand ("channel_loop_length", { 1, 4, 3 }));
    EditorController controller (processor);
    controller.layer = GENERATE (Layer::velocity, Layer::chance, Layer::nudge, Layer::roll, Layer::cycle);
    controller.selected.set (0);
    controller.selected.set (7);
    PatternGrid grid (controller);
    grid.layout.compact = GENERATE (false, true);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();
    const auto before = processor.editorDocument();
    const auto rendered = grid.createComponentSnapshot (grid.getLocalBounds());
    const auto backdrop = Palette::shell.interpolatedWith (Palette::app, 0.28f);
    for (const int step : { 0, 7, 10 })
    {
        const auto area = grid.cellBounds ({ 0, step }).reduced (3).getSmallestIntegerContainer();
        bool invisible = true;
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                invisible = invisible && rendered.getPixelAt (x, y) == backdrop;
        INFO ("Step " << step + 1);
        CHECK (invisible);
    }
    const auto visible = grid.cellBounds ({ 0, 3 }).getCentre().toInt();
    CHECK (rendered.getPixelAt (visible.x, visible.y) != backdrop);
    CHECK (controller.cell ({ 0, 0 }).enabled);
    CHECK (controller.cell ({ 0, 7 }).enabled);
    CHECK (processor.editorDocument() == before);
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        auto stream = juce::File (directory).getChildFile ("loop-visibility-" + juce::String (grid.layout.compact ? "compact-" : "desktop-") + juce::String (static_cast<int> (controller.layer)) + ".png").createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (rendered, *stream));
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
    REQUIRE (controller.cell ({ 0, 0 }).velocity == 113);
    dragTo (4, -40);
    for (int s = 0; s <= 4; ++s)
    {
        auto expected = before[static_cast<size_t> (s)];
        expected.enabled = true;
        if (expected.roll == 1)
            expected.velocity = jsRound (113.0 + s * 3.5);
        else
        {
            const auto body = grid.layout.cellArtwork (grid.cellBounds ({ 0, s }));
            const float endX = grid.layout.cell ({ 0, 4 }).getCentreX();
            for (int part = 0; part < expected.roll; ++part)
            {
                const float x = body.getX() + body.getWidth() * (static_cast<float> (part) + 0.5f) / static_cast<float> (expected.roll);
                expected.setSplitVelocity (part, jsRound (113.0f + 14.0f * (x - start.x) / (endX - start.x)));
            }
        }
        CHECK (controller.cell ({ 0, s }) == expected);
    }
    dragTo (1, -40);
    for (int s = 1; s <= 4; ++s)
    {
        const auto cell = controller.cell ({ 0, s });
        for (int part = 0; part < cell.roll; ++part)
            CHECK (cell.splitVelocity (part) == 127);
    }
    CHECK (controller.cell ({ 0, 0 }).velocity == 113);
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
            CHECK (controller.cell ({ 0, s }).velocity == 113);
    }
}

TEST_CASE ("Grid clicks switch or clear the selection without editing cells", "[native-ui][grid-selection]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 6, 1, 90 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const Location selected { 0, 0 }, other { 0, 4 }, neighbor { 0, 5 };
    const auto point = grid.layout.cell (other).getCentre();
    const int left = juce::ModifierKeys::leftButtonModifier;
    auto click = [&] (juce::Point<float> position, int modifiers)
    {
        const auto event = gridEvent (grid, position, position, modifiers);
        grid.mouseDown (event);
        grid.mouseUp (event);
    };

    SECTION ("Enabled cells replace the selection and empty cells clear it")
    {
        const bool enabled = GENERATE (false, true);
        controller.layer = GENERATE (Layer::velocity, Layer::chance);
        if (enabled)
        {
            REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 5, 1, 85 }));
            controller.refresh();
        }
        const auto bounds = grid.cellBounds (other);
        const bool nearEdge = GENERATE (false, true);
        const juce::Point<float> position { nearEdge ? bounds.getRight() - 1.0f : bounds.getCentreX(), bounds.getCentreY() };
        const auto before = processor.editorDocument();
        controller.selected.set (static_cast<size_t> (selected.key()));
        controller.selected.set (static_cast<size_t> (neighbor.key()));
        click (position, left);
        if (enabled)
            CHECK (controller.selection() == std::vector<Location> { other });
        else
            CHECK (controller.selected.none());
        CHECK (controller.cell (other).enabled == enabled);
        CHECK (processor.editorDocument() == before);
        CHECK_FALSE (controller.canUndo());
    }
    SECTION ("Clearing the selection on an empty cell does not begin a paint or value drag")
    {
        const auto before = processor.editorDocument();
        controller.selected.set (static_cast<size_t> (selected.key()));
        grid.mouseDown (gridEvent (grid, point, point, left));
        grid.mouseDrag (gridEvent (grid, grid.layout.cell ({ 0, 6 }).getCentre(), point, left, true));
        grid.mouseUp (gridEvent (grid, point, point, 0, true));
        CHECK (controller.selected.none());
        CHECK (processor.editorDocument() == before);
        CHECK_FALSE (controller.canUndo());
    }
    SECTION ("Grid background clears the selection")
    {
        const auto before = processor.editorDocument();
        controller.selected.set (static_cast<size_t> (selected.key()));
        click ({ point.x, grid.layout.cellsY() - 1.0f }, left);
        CHECK (controller.selected.none());
        CHECK (processor.editorDocument() == before);
        CHECK_FALSE (controller.canUndo());
    }
    SECTION ("Shift-click toggles active cells and clears selection on empty cells")
    {
        const int shiftClick = left | juce::ModifierKeys::shiftModifier;
        controller.selected.set (static_cast<size_t> (selected.key()));
        click (grid.layout.cell (neighbor).getCentre(), shiftClick);
        CHECK (controller.selected.count() == 2);
        click (grid.layout.cell (neighbor).getCentre(), shiftClick);
        CHECK (controller.selection() == std::vector<Location> { selected });
        click (point, shiftClick);
        CHECK (controller.selected.none());
        CHECK_FALSE (controller.cell (other).enabled);
        CHECK_FALSE (controller.canUndo());
    }
}

TEST_CASE ("Dragging from a dense grid row gutter requires Shift or Select", "[native-ui][grid-selection]")
{
    PluginProcessor processor;
    const int steps = GENERATE (32, 64);
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, steps }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();
    const auto first = grid.layout.cell ({ 0, 2 });
    const juce::Point<float> start { first.getCentreX(), first.getY() - 4.0f };
    const auto end = grid.layout.cell ({ 2, 7 }).getCentre();
    REQUIRE_FALSE (grid.layout.at (start).valid());
    const auto before = processor.editorDocument();
    const auto drag = [&] (int modifiers)
    {
        const auto held = modifiers | juce::ModifierKeys::leftButtonModifier;
        grid.mouseDown (gridEvent (grid, start, start, held));
        grid.mouseDrag (gridEvent (grid, end, start, held, true));
        grid.mouseUp (gridEvent (grid, end, start, modifiers, true));
    };
    drag (0);
    CHECK (controller.selected.none());
    controller.selectLatched = GENERATE (false, true);
    drag (controller.selectLatched ? 0 : juce::ModifierKeys::shiftModifier);
    CHECK (controller.selected.test (static_cast<size_t> (Location { 1, 4 }.key())));
    CHECK (processor.editorDocument() == before);
    CHECK_FALSE (controller.canUndo());
}

TEST_CASE ("Clicking selected cells fills blanks then clears the selection in single undo steps", "[native-ui][selection-fill]")
{
    PluginProcessor processor;
    const bool mixed = GENERATE (false, true);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 5, mixed ? 1 : 0, 90, 60, 4, 5, 3 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 2, 5, 0, 70, 80, 3, 2, 4 }));
    EditorController controller (processor);
    controller.layer = GENERATE (Layer::velocity, Layer::chance);
    PatternGrid grid (controller);
    grid.layout.compact = GENERATE (false, true);
    grid.setSize (1338, 554);
    grid.sync();
    const std::array<Location, 3> cells { Location { 0, 4 }, Location { 1, 4 }, Location { 2, 4 } };
    std::array<Cell, 3> before;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        before[i] = controller.cell (cells[i]);
        controller.selected.set (static_cast<size_t> (cells[i].key()));
    }
    const auto selection = controller.selected;
    const auto untouched = controller.cell ({ 3, 4 });
    const auto anchor = cells[GENERATE (size_t { 0 }, size_t { 1 })];
    const auto position = grid.layout.cell (anchor).getCentre();
    const auto click = [&]
    {
        grid.mouseDown (gridEvent (grid, position, position, juce::ModifierKeys::leftButtonModifier));
        grid.mouseUp (gridEvent (grid, position, position, 0));
    };
    const auto check = [&] (bool enabled, bool original)
    {
        for (size_t i = 0; i < cells.size(); ++i)
        {
            const auto expected = original || (enabled && before[i].enabled) ? before[i] : Cell { .enabled = enabled };
            CHECK (controller.cell (cells[i]) == expected);
        }
        CHECK (controller.selected == selection);
        CHECK (controller.cell ({ 3, 4 }) == untouched);
    };
    click();
    check (true, false);
    click();
    check (false, false);
    controller.undo();
    check (true, false);
    controller.undo();
    check (false, true);
    CHECK_FALSE (controller.canUndo());
    controller.redo();
    check (true, false);
    controller.redo();
    check (false, false);
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
    const int steps = GENERATE (32, 64);
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, steps }));
    REQUIRE (processor.dispatchUiEngineCommand ("channel_playback_mode", { 1, "reverse_ping_pong" }));
    REQUIRE (processor.dispatchUiEngineCommand ("source_channel_mute", { 1, 2, true }));
    for (int ch = 0; ch < 8; ++ch)
        for (int step = 0; step < steps; ++step)
            REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, ch + 1, step + 1, step % 3 != 1 ? 1 : 0, 50 + step, 60 + step % 32, 4, 5, 1 << (step % 4) }));
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    for (int scale : { 50, 100, 120 })
    for (auto layer : { Layer::velocity, Layer::chance })
    {
        processor.setProjectUiScalePercent (scale);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        auto* view = dynamic_cast<NativeEditor*> (editor->getChildComponent (0));
        REQUIRE (view != nullptr);
        const auto cell = view->layout.cell ({ 0, steps - 1 });
        CHECK (cell.getHeight() == Catch::Approx (view->layout.rowHeight()));
        CHECK (view->layout.at (cell.getTopLeft().translated (2.0f, 2.0f)) == Location { 0, steps - 1 });
        view->controller.selected.set (0);
        view->controller.selected.set (2);
        view->controller.layer = layer;
        view->sync();
        auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        REQUIRE (image.isValid());
        CHECK (editor->getWidth() == juce::roundToInt (view->layout.width() * UiScale::factor (scale)));
        if (directory.isNotEmpty())
        {
            auto stream = juce::File (directory).getChildFile ("native-" + juce::String (steps) + "-" + juce::String (scale) + "-"
                                                              + juce::String (static_cast<int> (layer)) + ".png").createOutputStream();
            REQUIRE (stream != nullptr);
            REQUIRE (juce::PNGImageFormat().writeImageToStream (image, *stream));
        }
    }
}

TEST_CASE ("Dragging S adjusts and persists text brightness without opening Generate", "[native-ui][text-brightness]")
{
    struct RestoreBrightness
    {
        int previous = Palette::textBrightness();
        ~RestoreBrightness() { Palette::setTextBrightness (previous); }
    } restore;
    juce::TemporaryFile settings (".settings");
    juce::PropertiesFile::Options options;
    options.millisecondsBeforeSaving = -1;
    const auto preferences = std::make_shared<juce::PropertiesFile> (settings.getFile(), options);
    PluginProcessor processor;
    NativeEditor editor (processor, preferences);
    editor.setSize (Layout::fullWidth, Layout::fullHeight);

    PreviewPanel* inspector = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* candidate = dynamic_cast<PreviewPanel*> (child))
            inspector = candidate;
    REQUIRE (inspector != nullptr);
    auto* brightness = inspector->getComponentAt (125, 80);
    REQUIRE (brightness != nullptr);
    REQUIRE (brightness->getTitle() == "Text brightness");
    CHECK (Palette::textBrightness() == 0);
    CHECK (Palette::white == juce::Colour (0xffc7c7c7));
    CHECK (Palette::text.isOpaque());
    CHECK (Palette::muted.isOpaque());
    const auto renderS = []
    {
        juce::Image image (juce::Image::ARGB, 300, 140, true);
        juce::Graphics graphics (image);
        AccentWordmark::paintBranding (graphics, image.getBounds().toFloat());
        return image.getPixelAt (145, 110);
    };
    const auto defaultS = renderS();
    REQUIRE (defaultS.isOpaque());

    const auto origin = brightness->getLocalBounds().toFloat().getCentre();
    const auto gesture = [brightness, origin] (float y, int modifiers)
    {
        return gridEvent (*brightness, origin.translated (0, y), origin, modifiers, y != 0.0f);
    };
    constexpr int down = juce::ModifierKeys::leftButtonModifier;
    brightness->mouseDown (gesture (0, down));
    brightness->mouseDrag (gesture (120, down));
    CHECK (Palette::textBrightness() == -40);
    CHECK (Palette::white.getRed() < 199);
    CHECK (renderS().getPerceivedBrightness() < defaultS.getPerceivedBrightness());
    brightness->mouseDrag (gesture (-120, down));
    CHECK (Palette::textBrightness() == 100);
    CHECK (Palette::white == juce::Colours::white);
    CHECK (renderS().getPerceivedBrightness() > defaultS.getPerceivedBrightness());
    brightness->mouseUp (gesture (-120, 0));
    CHECK_FALSE (editor.layout.randomVisible);
    CHECK (juce::PropertiesFile (settings.getFile(), options).getIntValue ("textBrightness") == 100);
    editor.controller.undo();
    CHECK (Palette::textBrightness() == 0);
    editor.controller.redo();
    CHECK (Palette::textBrightness() == 100);
    brightness->mouseDoubleClick (gesture (0, down));
    CHECK (Palette::textBrightness() == 0);
    CHECK_FALSE (processor.isDocumentModified());
}

TEST_CASE ("Selected value controls temporarily switch the editing layer", "[native-ui]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    PreviewPanel panel (controller);
    for (auto* child : panel.getChildren())
    {
        auto* control = dynamic_cast<ValueControl*> (child);
        if (control == nullptr || (control->caption != "Velocity" && control->caption != "Prob" && control->caption != "Split"))
            continue;

        for (auto original : { Layer::velocity, Layer::chance })
        {
            controller.layer = original;
            control->onBegin();
            const auto expected = control->caption == "Velocity" ? Layer::velocity : control->caption == "Prob" ? Layer::chance : Layer::roll;
            CHECK (controller.layer == expected);
            control->onEnd();
            CHECK (controller.layer == original);
        }
    }
}

TEST_CASE ("Inspector offsets follow vertical mouse drags in both layouts", "[native-ui][inspector-drag]")
{
    const bool compact = GENERATE (false, true);
    const auto layer = GENERATE (Layer::velocity, Layer::chance);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 70, 60 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 2, 1, 90, 80 }));
    EditorController controller (processor);
    const Location splitStep { 0, 1 };
    auto split = controller.cell (splitStep);
    split.roll = 3;
    split.setSplitVelocity (0, 120);
    split.splitGates[0].setProbability (5);
    split.splitGates[1].setProbability (96);
    controller.begin ("Set up split hits");
    controller.editCell (splitStep, split);
    controller.commit();
    controller.clearHistory();
    controller.selected.set (0);
    controller.selected.set (1);
    PreviewPanel panel (controller);
    panel.setSize (1100, 100);
    panel.setInspectorBounds (panel.getLocalBounds());
    panel.setCompact (compact);
    panel.sync();
    const auto title = layer == Layer::velocity ? "Selected velocity offset" : "Selected probability offset";
    ValueControl* adjustment = nullptr;
    for (auto* child : panel.getChildren())
        if (child->getTitle() == title)
            adjustment = dynamic_cast<ValueControl*> (child);
    REQUIRE (adjustment != nullptr);
    REQUIRE (adjustment->isEnabled());
    const auto first = controller.cell ({ 0, 0 });
    const auto second = controller.cell ({ 0, 1 });
    const auto start = adjustment->getLocalBounds().toFloat().getCentre();
    const int left = juce::ModifierKeys::leftButtonModifier;
    adjustment->mouseDown (gridEvent (*adjustment, start, start, left));
    const auto dragTo = [&] (juce::Point<float> point)
    {
        adjustment->mouseDrag (gridEvent (*adjustment, point, start, left, true));
    };
    const auto checkOffset = [&] (int offset)
    {
        CHECK (controller.cell ({ 0, 0 }) == withValue (first, layer, cellValue (first, layer) + offset));
        auto expected = withValue (second, layer, cellValue (second, layer) + offset);
        if (layer == Layer::velocity)
            expected.setSplitVelocity (0, std::clamp (120 + offset, 1, 127));
        else
        {
            expected.splitGates[0].setProbability (std::clamp (5 + offset, 0, 100));
            expected.splitGates[1].setProbability (std::clamp (96 + offset, 0, 100));
            expected.splitGates[2].setProbability (std::clamp (100 + offset, 0, 100));
        }
        CHECK (controller.cell (splitStep) == expected);
    };
    dragTo (start.translated (44, 0));
    checkOffset (0);
    dragTo (start.translated (0, -44));
    checkOffset (10);
    dragTo (start.translated (44, 0));
    checkOffset (0);
    const auto end = start.translated (0, 44);
    dragTo (end);
    checkOffset (-10);
    adjustment->mouseUp (gridEvent (*adjustment, end, start, 0, true));
    REQUIRE (controller.canUndo());
    controller.undo();
    checkOffset (0);
}

TEST_CASE ("Option temporarily shows Chance while the button preserves its latch", "[native-ui]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    PatternGrid* grid = nullptr;
    GestureButton* toggle = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<PatternGrid*> (child))
            grid = candidate;
        if (auto* button = dynamic_cast<GestureButton*> (child); button && button->getProperties().contains ("layerSegment") && static_cast<int> (button->getProperties()["layerSegment"]) == 1)
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
    const auto buttonPosition = toggle->getLocalBounds().toFloat().getCentre();
    const auto press = gridEvent (*toggle, buttonPosition, buttonPosition, juce::ModifierKeys::leftButtonModifier);
    toggle->mouseDown (press);
    CHECK (grid->displayLayer() == Layer::chance);
    const juce::MouseEvent release { press.source, buttonPosition, {}, 1, 0, 0, 0, 0,
        toggle, toggle, press.eventTime + juce::RelativeTime::milliseconds (400), buttonPosition, press.mouseDownTime, 1, false };
    toggle->mouseUp (release);
    CHECK (grid->displayLayer() == Layer::velocity);
    toggle->mouseDown (press);
    grid->mouseDown (gridEvent (*grid, position, position, juce::ModifierKeys::leftButtonModifier));
    grid->mouseUp (gridEvent (*grid, position, position, 0));
    toggle->mouseUp (gridEvent (*toggle, buttonPosition, buttonPosition, 0));
    CHECK (grid->displayLayer() == Layer::velocity);
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
    juce::TextButton* selectHeading = nullptr;
    juce::TextButton* copy = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<PatternGrid*> (child))
            grid = candidate;
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getProperties().contains ("layerSegment") && static_cast<int> (button->getProperties()["layerSegment"]) == 1)
                heading = button;
            if (button->getProperties().contains ("selectMode"))
                selectHeading = button;
            if (button->getTitle() == "Copy selected cells when dragging")
                copy = button;
        }
    }
    REQUIRE (grid != nullptr);
    REQUIRE (heading != nullptr);
    REQUIRE (selectHeading != nullptr);
    REQUIRE (copy != nullptr);
    auto& controller = editor.controller;
    controller.layer = layer;
    controller.selected.set (0);
    controller.selected.set (1);
    controller.selected.set (2);
    const auto first = controller.cell ({ 0, 0 });
    const auto second = controller.cell ({ 0, 1 });
    editor.sync();
    const int option = juce::ModifierKeys::altModifier;
    const int left = juce::ModifierKeys::leftButtonModifier;
    editor.modifierKeysChanged (juce::ModifierKeys (option));
    CHECK (grid->displayLayer() == layer);
    CHECK_FALSE (heading->getToggleState());
    CHECK (selectHeading->getToggleState());
    CHECK (grid->isCopyMode());
    CHECK (copy->getToggleState());
    CHECK_FALSE (grid->copySelection);
    const auto start = grid->layout.cell ({ 0, 0 }).getCentre();
    const auto end = grid->layout.cell ({ 1, 4 }).getCentre();
    grid->mouseMove (gridEvent (*grid, start, start, option));
    CHECK (grid->getMouseCursor() == juce::MouseCursor (juce::MouseCursor::CopyingCursor));
    grid->mouseDown (gridEvent (*grid, start, start, left | option));

    SECTION ("Dragging copies the entire selection and supports undo redo")
    {
        grid->mouseDrag (gridEvent (*grid, end, start, left | option, true));
        CHECK (grid->displayLayer() == layer);
        const auto preview = grid->createComponentSnapshot (grid->getLocalBounds());
        const auto blank = grid->layout.cell ({ 1, 6 }).getCentre();
        CHECK (preview.getPixelAt (juce::roundToInt (end.x), juce::roundToInt (end.y)).getPerceivedBrightness()
               > preview.getPixelAt (juce::roundToInt (blank.x), juce::roundToInt (blank.y)).getPerceivedBrightness() + 0.10f);
        if (layer == Layer::velocity)
        {
            const auto cell = grid->layout.cell ({ 1, 4 });
            const int x = juce::roundToInt (cell.getX() + cell.getWidth() * 0.22f);
            const auto top = preview.getPixelAt (x, juce::roundToInt (cell.getY() + cell.getHeight() * 0.15f));
            const auto bottom = preview.getPixelAt (x, juce::roundToInt (cell.getY() + cell.getHeight() * 0.85f));
            CHECK (bottom.getPerceivedBrightness() > top.getPerceivedBrightness() + 0.12f);
        }
        grid->mouseUp (gridEvent (*grid, end, start, option, true));
        CHECK (controller.cell ({ 0, 0 }) == first);
        CHECK (controller.cell ({ 0, 1 }) == second);
        CHECK (controller.cell ({ 1, 4 }) == first);
        CHECK (controller.cell ({ 1, 5 }) == second);
        CHECK (controller.selected.count() == 3);
        CHECK (controller.selected.test (static_cast<size_t> (Location { 1, 4 }.key())));
        CHECK (controller.selected.test (static_cast<size_t> (Location { 1, 5 }.key())));
        CHECK (controller.selected.test (static_cast<size_t> (Location { 1, 6 }.key())));
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

TEST_CASE ("Move drags fade the original clips and selection outlines", "[native-ui][grid-selection][render]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    controller.keyboardModifiersEnabled = true;
    controller.graphicalCellValues = true;
    PatternGrid grid (controller);
    grid.layout.compact = GENERATE (false, true);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();

    const Location source { 0, 2 }, empty { 0, 4 }, destination { 2, 10 };
    const auto bodyPoint = [&] (Location cell)
    {
        const auto bounds = grid.layout.cell (cell);
        return juce::Point<int> { juce::roundToInt (bounds.getCentreX()), juce::roundToInt (bounds.getY() + 0.75f * bounds.getHeight()) };
    };
    const auto readPixel = [&] (juce::Point<int> point)
    {
        return grid.createComponentSnapshot ({ point.x, point.y, 1, 1 }).getPixelAt (0, 0);
    };
    const auto contrast = [] (juce::Colour a, juce::Colour b)
    {
        return std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen()) + std::abs (a.getBlue() - b.getBlue());
    };
    const auto sourcePoint = bodyPoint (source);
    const auto destinationPoint = bodyPoint (destination);
    const auto emptyBody = grid.layout.cellArtwork (grid.layout.cell (empty));
    const juce::Point<int> outlinePoint { juce::roundToInt (emptyBody.getX() - 1.0f), juce::roundToInt (emptyBody.getCentreY()) };
    const auto sourceBackground = readPixel (sourcePoint);
    const auto destinationBackground = readPixel (destinationPoint);
    const auto outlineBackground = readPixel (outlinePoint);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 110 }));
    controller.refresh();
    controller.selected.set (static_cast<size_t> (source.key()));
    controller.selected.set (static_cast<size_t> (empty.key()));
    grid.sync();
    const auto sourceColour = readPixel (sourcePoint);
    const auto outlineColour = readPixel (outlinePoint);
    const int fullHit = contrast (sourceBackground, sourceColour);
    const int fullOutline = contrast (outlineBackground, outlineColour);
    REQUIRE (fullHit > 0);
    REQUIRE (fullOutline > 0);

    const auto start = grid.cellBounds (source).getCentre();
    const auto end = grid.layout.cell (destination).getCentre();
    constexpr int left = juce::ModifierKeys::leftButtonModifier;
    grid.mouseDown (gridEvent (grid, start, start, left));
    grid.mouseDrag (gridEvent (grid, end, start, left, true));
    const int ghostHit = contrast (sourceBackground, readPixel (sourcePoint));
    const int ghostOutline = contrast (outlineBackground, readPixel (outlinePoint));
    CHECK (ghostHit > 0);
    CHECK (ghostHit * 5 <= fullHit);
    CHECK (ghostOutline > 0);
    CHECK (ghostOutline * 5 <= fullOutline);
    const auto preview = readPixel (destinationPoint);
    CHECK (contrast (destinationBackground, preview) > ghostHit);

    grid.modifierKeysChanged (juce::ModifierKeys (left | juce::ModifierKeys::altModifier));
    CHECK (readPixel (sourcePoint) == sourceColour);
    CHECK (readPixel (outlinePoint) == outlineColour);
    CHECK (readPixel (destinationPoint) == preview);
    grid.modifierKeysChanged (juce::ModifierKeys (left));
    grid.cancelGesture();
    CHECK (readPixel (sourcePoint) == sourceColour);
    CHECK (readPixel (destinationPoint) == destinationBackground);
}

TEST_CASE ("Releasing Option preserves the selected steps and their grid layer", "[native-ui][grid-selection]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 4, 1, 70 }));
    EditorController controller (processor);
    controller.keyboardModifiersEnabled = true;
    controller.layer = GENERATE (Layer::velocity, Layer::chance);
    controller.selected.set (2);
    controller.selected.set (3);
    const auto selection = controller.selected;
    PatternGrid grid (controller);
    grid.setSize (Layout::fullWidth, juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();

    grid.modifierKeysChanged (juce::ModifierKeys (juce::ModifierKeys::altModifier));
    CHECK (grid.isCopyMode());
    CHECK (grid.displayLayer() == controller.layer);
    grid.modifierKeysChanged ({});
    CHECK_FALSE (grid.isCopyMode());
    CHECK (grid.displayLayer() == controller.layer);
    CHECK (controller.selected == selection);
}

TEST_CASE ("Select drags move or duplicate cells from either grid layer", "[native-ui][grid-selection]")
{
    const auto layer = GENERATE (Layer::chance, Layer::velocity);
    const bool latched = GENERATE (false, true);
    const bool fromCycleStrip = GENERATE (false, true);
    const bool copy = GENERATE (false, true);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 90, 60, 4, 5, 3 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    controller.layer = layer;
    if (latched)
    {
        controller.beginSelect();
        controller.endSelect (false, true);
    }
    const Location source { 0, 2 };
    const Location destination = latched ? Location { 2, 2 } : Location { 0, 5 };
    controller.selected.set (static_cast<size_t> (source.key()));
    grid.sync();
    const auto original = controller.cell (source);
    const auto bounds = grid.cellBounds (source);
    const juce::Point<float> start { bounds.getCentreX(), bounds.getY() + bounds.getHeight() * (fromCycleStrip ? 0.9f : 0.5f) };
    const auto end = grid.layout.cell (destination).getCentre();
    const int left = juce::ModifierKeys::leftButtonModifier;
    const int option = copy ? juce::ModifierKeys::altModifier : 0;
    grid.mouseDown (gridEvent (grid, start, start, left));
    grid.mouseDrag (gridEvent (grid, end, start, left, true));
    grid.modifierKeysChanged (juce::ModifierKeys (left | option));
    grid.mouseUp (gridEvent (grid, end, start, option, true));
    CHECK (controller.cell (destination) == original);
    CHECK (controller.cell (source).enabled == copy);
    CHECK (controller.selection() == std::vector<Location> { destination });
    CHECK (controller.layer == layer);
    controller.undo();
    CHECK (controller.cell (source) == original);
    CHECK_FALSE (controller.cell (destination).enabled);
}

TEST_CASE ("Roll is edited in the inspector while permanent grid layers remain velocity and chance", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 80, 4, 5, 1 }));
    NativeEditor editor (processor);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
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
        if (auto* control = dynamic_cast<ValueControl*> (child); control && control->caption == "Split")
            roll = control;
    REQUIRE (roll != nullptr);
    for (auto layer : { Layer::velocity, Layer::chance })
    {
        controller.layer = layer;
        roll->onBegin();
        const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
        if (directory.isNotEmpty())
        {
            auto stream = juce::File (directory).getChildFile ("layer-hint-" + juce::String (static_cast<int> (layer)) + ".png").createOutputStream();
            REQUIRE (stream != nullptr);
            REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds()), *stream));
        }
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

TEST_CASE ("Chance grid clicks create fresh steps even in older projects", "[native-ui][chance-gestures]")
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
    const auto expected = Cell { .enabled = true };
    CHECK (controller.cell ({ 0, 0 }) == expected);
    click();
    CHECK (controller.cell ({ 0, 0 }) == Cell {});
    controller.undo();
    CHECK (controller.cell ({ 0, 0 }) == expected);
    controller.undo();
    CHECK (controller.cell ({ 0, 0 }) == before);
    controller.redo();
    CHECK (controller.cell ({ 0, 0 }) == expected);
    controller.redo();
    CHECK (controller.cell ({ 0, 0 }) == Cell {});
}

TEST_CASE ("Chance grid paints and erases like Velocity while preserving undo and cancellation", "[native-ui][chance-gestures]")
{
    PluginProcessor processor;
    const bool enabled = GENERATE (false, true);
    for (int step = 1; step <= 5; ++step)
        REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, step, enabled ? 1 : 0, 90, 60, 4, 5, 3 }));
    EditorController controller (processor);
    controller.layer = GENERATE (Layer::velocity, Layer::chance);
    PatternGrid grid (controller);
    grid.layout.compact = GENERATE (false, true);
    grid.setSize (1338, 554);
    grid.sync();
    const auto before = controller.cell ({ 0, 0 });
    const auto start = grid.layout.cell ({ 0, 0 }).getCentre();
    const auto end = grid.layout.cell ({ 0, 4 }).getCentre().translated (0, 2);
    const int left = juce::ModifierKeys::leftButtonModifier;
    grid.mouseDown (gridEvent (grid, start, start, left));
    grid.mouseDrag (gridEvent (grid, end, start, left, true));
    grid.sync();
    CHECK (grid.getMouseCursor() == juce::MouseCursor::NormalCursor);
    const auto expected = Cell { .enabled = !enabled };
    for (int step = 0; step < 5; ++step)
        CHECK (controller.cell ({ 0, step }) == expected);
    CHECK_FALSE (controller.cell ({ 0, 5 }).enabled);
    const bool cancel = GENERATE (false, true);
    if (cancel)
        grid.cancelGesture();
    else
    {
        grid.mouseUp (gridEvent (grid, end, start, 0, true));
        REQUIRE (controller.canUndo());
        controller.undo();
    }
    for (int step = 0; step < 5; ++step)
        CHECK (controller.cell ({ 0, step }) == before);
    CHECK_FALSE (controller.canUndo());
    if (!cancel)
    {
        controller.redo();
        for (int step = 0; step < 5; ++step)
            CHECK (controller.cell ({ 0, step }) == expected);
    }
}

TEST_CASE ("Chance grid vertical drags adjust probability and preserve cycle and roll", "[native-ui][chance-gestures]")
{
    PluginProcessor processor;
    const bool selected = GENERATE (false, true);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 60, 4, 5, selected ? 3 : 1 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 2, 1, 70, 80, 3, 2, 4 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    controller.layer = Layer::chance;
    const auto before = controller.cell ({ 0, 0 });
    const auto second = controller.cell ({ 0, 1 });
    if (selected)
    {
        controller.selected.set (0);
        controller.selected.set (1);
    }
    const auto bounds = grid.layout.cell ({ 0, 0 });
    const float editableLeft = grid.loopHandleBounds (0, false).getRight();
    const auto start = juce::Point<float> (juce::jmax (editableLeft + 1, bounds.getX() + GENERATE (2.0f, 27.0f, 52.0f)),
                                          bounds.getY() + GENERATE (2.0f, 27.0f, 33.0f));
    const int modifiers = juce::ModifierKeys::leftButtonModifier;
    const float horizontalDrag = GENERATE (0.0f, 12.0f);
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
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 60, 4, 5, 1 }));
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
            else if (control->caption == "Prob" || control->caption == "Chance") expected = Layer::chance;
            else if (control->caption == "Nudge") expected = Layer::nudge;
            else if (control->caption == "Split") expected = Layer::roll;
            else if (control->caption != "Length") continue;
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
    CHECK_FALSE (controller.inspectorHoverLayer.has_value());
    for (auto* child : inspector.getChildren())
    {
        if (!child->isVisible())
            continue;
        inspector.mouseEnter (event (*child, child->getLocalBounds().toFloat().getCentre()));
        inspector.mouseMove (event (*child, child->getLocalBounds().toFloat().getCentre()));
        CHECK (grid.displayLayer() == chosen);
        CHECK_FALSE (controller.inspectorHoverLayer.has_value());
    }
    for (auto* child : cycle->getChildren())
    {
        if (!child->isVisible())
            continue;
        inspector.mouseEnter (event (*child, child->getLocalBounds().toFloat().getCentre()));
        CHECK (grid.displayLayer() == chosen);
    }
    controller.selected.set (0);
    inspector.sync();
    inspector.mouseEnter (event (*cycle, cycle->getLocalBounds().toFloat().getCentre()));
    CHECK (grid.displayLayer() == Layer::chance);
    inspector.mouseExit (event (inspector, { -1, -1 }));
    CHECK (grid.displayLayer() == chosen);
}

TEST_CASE ("Piano note chooser browses without editing and assigns one undoable note", "[native-ui][note-chooser]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    PatternGrid* grid = nullptr;
    NoteChooser* chooser = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<PatternGrid*> (child)) grid = candidate;
        if (auto* candidate = dynamic_cast<NoteChooser*> (child)) chooser = candidate;
    }
    REQUIRE (grid != nullptr);
    REQUIRE (chooser != nullptr);
    NoteInput* input = nullptr;
    for (auto* child : grid->getChildren())
        if (child->getTitle() == "MIDI note channel 1") input = dynamic_cast<NoteInput*> (child);
    REQUIRE (input != nullptr);
    const int initial = editor.controller.state.playback.channels[0].note;
    processor.markDocumentClean();
    const auto start = input->getLocalBounds().toFloat().getCentre();
    input->mouseDown (gridEvent (*input, start, start, juce::ModifierKeys::leftButtonModifier));
    input->mouseDrag (gridEvent (*input, start.translated (0, -100), start, juce::ModifierKeys::leftButtonModifier, true));
    input->mouseUp (gridEvent (*input, start.translated (0, -100), start, 0, true));
    CHECK_FALSE (chooser->isVisible());
    CHECK_FALSE (input->keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
    CHECK (editor.controller.state.playback.channels[0].note == initial);
    const juce::Point<float> inset { start.x, 2.0f };
    CHECK_FALSE (input->valueFaceBounds().contains (inset));
    input->mouseDown (gridEvent (*input, inset, inset, juce::ModifierKeys::leftButtonModifier));
    input->mouseUp (gridEvent (*input, inset, inset, 0));
    REQUIRE (chooser->isVisible());
    const auto click = [&] (const juce::String& title)
    {
        for (auto* child : chooser->getChildren())
            if (child->getTitle() == title)
                if (auto* button = dynamic_cast<juce::TextButton*> (child))
                {
                    button->onClick();
                    return true;
                }
        return false;
    };
    REQUIRE (click ("Scroll keyboard right"));
    CHECK (editor.controller.state.playback.channels[0].note == initial);
    CHECK_FALSE (processor.isDocumentModified());
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        auto stream = juce::File (directory).getChildFile ("note-chooser.png").createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds()), *stream));
    }
    REQUIRE (click ("G4"));
    CHECK (chooser->isVisible());
    CHECK (editor.controller.state.playback.channels[0].note == 67);
    editor.controller.undo();
    CHECK (editor.controller.state.playback.channels[0].note == initial);
    editor.controller.redo();
    CHECK (editor.controller.state.playback.channels[0].note == 67);
    input->onChoose();
    CHECK (chooser->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (editor.controller.state.playback.channels[0].note == 67);
    input->onChoose();
    REQUIRE (processor.dispatchUiEngineCommand ("channel_note", { 1, 60 }));
    editor.controller.refresh();
    CHECK_FALSE (chooser->isVisible());
    CHECK (editor.controller.state.playback.channels[0].note == 60);
}

TEST_CASE ("The note chooser applies all eight notes in one undo step", "[native-ui][note-chooser]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("channels", { 4 }));
    NativeEditor editor (processor);
    editor.setSize (Layout::fullWidth, Layout::fullHeight);
    PatternGrid* grid = nullptr;
    NoteChooser* chooser = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<PatternGrid*> (child)) grid = candidate;
        if (auto* candidate = dynamic_cast<NoteChooser*> (child)) chooser = candidate;
    }
    REQUIRE (grid != nullptr);
    REQUIRE (chooser != nullptr);
    grid->rowNoteInput (2).onChoose();
    REQUIRE (chooser->isVisible());
    const auto notes = [&]
    {
        NoteChooser::RowNotes result;
        const auto& channels = editor.controller.state.playback.channels;
        std::transform (channels.begin(), channels.end(), result.begin(), [] (const auto& ch) { return ch.note; });
        return result;
    };
    const auto original = notes();
    SECTION ("The octave button transposes every row")
    {
        juce::TextButton* octaveUp = nullptr;
        for (auto* child : chooser->getChildren())
            if (child->getTitle() == "Transpose all rows up an octave")
                octaveUp = dynamic_cast<juce::TextButton*> (child);
        REQUIRE (octaveUp != nullptr);
        REQUIRE (octaveUp->isEnabled());
        CHECK (octaveUp->getWidth() == Layout::touchTarget);
        CHECK (octaveUp->getHeight() == Layout::touchTarget);
        octaveUp->onClick();
        auto expected = original;
        for (auto& note : expected)
            note += 12;
        CHECK (notes() == expected);
        CHECK (chooser->isVisible());
    }
    SECTION ("A scale assignment replaces every row")
    {
        const NoteChooser::RowNotes scale { 36, 38, 40, 41, 43, 45, 47, 48 };
        chooser->onPresetAll (scale, NoteChooser::PresetOctave::current);
        CHECK (notes() == scale);
        CHECK (chooser->isVisible());
    }
    SECTION ("A scale follows the current octave and keeps its intervals at the MIDI limit")
    {
        const auto octavesUp = GENERATE (2, 7);
        chooser->onTransposeAll (12 * octavesUp);
        const auto transposed = notes();
        const NoteChooser::RowNotes scale { 36, 38, 40, 41, 43, 45, 47, 48 };
        chooser->onPresetAll (scale, NoteChooser::PresetOctave::current);
        auto expected = scale;
        for (auto& note : expected)
            note += 12 * std::min (octavesUp, 6);
        CHECK (notes() == expected);
        REQUIRE (editor.controller.canUndo());
        editor.controller.undo();
        CHECK (notes() == transposed);
    }
    SECTION ("Ruismaker retains its fixed MIDI notes")
    {
        const NoteChooser::RowNotes mapping { 49, 51, 54, 56, 58, 61, 63, 66 };
        chooser->onPresetAll (mapping, NoteChooser::PresetOctave::fixed);
        CHECK (notes() == mapping);
    }
    REQUIRE (editor.controller.canUndo());
    editor.controller.undo();
    CHECK (notes() == original);
    CHECK_FALSE (editor.controller.canUndo());
}

TEST_CASE ("Piano note chooser switches rows with one outside click", "[native-ui][note-chooser]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    editor.setVisible (true);
    PatternGrid* grid = nullptr;
    NoteChooser* chooser = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<PatternGrid*> (child)) grid = candidate;
        if (auto* candidate = dynamic_cast<NoteChooser*> (child)) chooser = candidate;
    }
    REQUIRE (grid != nullptr);
    REQUIRE (chooser != nullptr);
    std::array<NoteInput*, 2> inputs {};
    for (auto* child : grid->getChildren())
        for (size_t i = 0; i < inputs.size(); ++i)
            if (child->getTitle() == "MIDI note channel " + juce::String (static_cast<int> (i + 1)))
                inputs[i] = dynamic_cast<NoteInput*> (child);
    REQUIRE (inputs[0] != nullptr);
    REQUIRE (inputs[1] != nullptr);
    const int firstNote = editor.controller.state.playback.channels[0].note;
    const int secondNote = editor.controller.state.playback.channels[1].note;
    processor.markDocumentClean();
    inputs[0]->onChoose();
    for (const int row : { 1, 0, 1 })
    {
        const auto position = chooser->getLocalPoint (inputs[static_cast<size_t> (row)], inputs[static_cast<size_t> (row)]->getLocalBounds().getCentre()).toFloat();
        REQUIRE (editor.getComponentAt (editor.getLocalPoint (chooser, position.toInt())) == chooser);
        chooser->mouseDown (gridEvent (*chooser, position, position, juce::ModifierKeys::leftButtonModifier));
        REQUIRE (chooser->isVisible());
        for (auto* child : chooser->getChildren())
            if (auto* key = dynamic_cast<juce::TextButton*> (child); key != nullptr && key->getToggleState())
                CHECK (key->getTitle() == juce::MidiMessage::getMidiNoteName (row == 0 ? firstNote : secondNote, true, true, 4));
        CHECK_FALSE (processor.isDocumentModified());
    }
    for (auto* child : chooser->getChildren())
        if (child->getTitle() == "C#4")
            if (auto* key = dynamic_cast<juce::TextButton*> (child)) key->onClick();
    CHECK (chooser->isVisible());
    CHECK (editor.controller.state.playback.channels[0].note == firstNote);
    CHECK (editor.controller.state.playback.channels[1].note == 61);
    editor.controller.undo();
    CHECK (editor.controller.state.playback.channels[1].note == secondNote);
    const auto firstRow = editor.layout.reverseRows ? 1u : 0u;
    const auto secondRow = 1u - firstRow;
    inputs[firstRow]->onChoose();
    CHECK (chooser->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (chooser->keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (chooser->isVisible());
    CHECK (editor.controller.state.playback.channels[firstRow].note == (firstRow == 0 ? firstNote : secondNote) + 1);
    CHECK (chooser->getDescription().contains ("track " + juce::String (static_cast<int> (secondRow + 1))));
    CHECK (chooser->keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
    CHECK (chooser->getDescription().contains ("track " + juce::String (static_cast<int> (firstRow + 1))));
    inputs[0]->onChoose();
    chooser->mouseDown (gridEvent (*chooser, { 1, 1 }, { 1, 1 }, juce::ModifierKeys::leftButtonModifier));
    CHECK_FALSE (chooser->isVisible());
}

TEST_CASE ("Piano note chooser covers MIDI endpoints without duplicate entry controls", "[native-ui][note-chooser]")
{
    NoteChooser chooser;
    chooser.setSize (900, 600);
    int assigned = -1;
    chooser.onChoose = [&] (int note) { assigned = note; };
    const auto open = [&] { chooser.open ({ 100, 100, 44, 32 }, "1", 36, nullptr); };
    const auto button = [&] (const juce::String& title) -> juce::TextButton*
    {
        for (auto* child : chooser.getChildren())
            if (child->getTitle() == title)
                if (auto* candidate = dynamic_cast<juce::TextButton*> (child)) return candidate;
        return nullptr;
    };
    open();
    REQUIRE (button ("C1") != nullptr);
    REQUIRE (button ("B4") != nullptr);
    REQUIRE (button ("Scroll keyboard left") != nullptr);
    for (int i = 0; i < 2; ++i)
        button ("Scroll keyboard left")->onClick();
    CHECK_FALSE (button ("Scroll keyboard left")->isEnabled());
    REQUIRE (button ("C-1") != nullptr);
    button ("C-1")->onClick();
    CHECK (assigned == 0);
    open();
    for (int i = 0; i < 7; ++i)
        button ("Scroll keyboard right")->onClick();
    CHECK_FALSE (button ("Scroll keyboard right")->isEnabled());
    REQUIRE (button ("G9") != nullptr);
    button ("G9")->onClick();
    CHECK (assigned == 127);
    open();
    for (auto* child : chooser.getChildren())
    {
        CHECK (dynamic_cast<juce::TextEditor*> (child) == nullptr);
        CHECK (child->getTitle() != "Assign MIDI note");
    }
    CHECK (chooser.isAlwaysOnTop());

}

TEST_CASE ("Piano keys assign on press and audition each scrubbed note once", "[native-ui][note-chooser]")
{
    const bool compact = GENERATE (false, true);
    PluginProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    NativeEditor editor (processor);
    if (compact)
        editor.setCompactViewport (true, 814, 409);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    NoteChooser* chooser = nullptr;
    PatternGrid* grid = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (auto* candidate = dynamic_cast<NoteChooser*> (child)) chooser = candidate;
        if (auto* candidate = dynamic_cast<PatternGrid*> (child)) grid = candidate;
    }
    REQUIRE (chooser != nullptr);
    REQUIRE (grid != nullptr);
    for (auto* child : grid->getChildren())
        if (child->getTitle() == "MIDI note channel 1")
            dynamic_cast<NoteInput*> (child)->onChoose();
    REQUIRE (chooser->isVisible());
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        auto stream = juce::File (directory).getChildFile (compact ? "note-chooser-four-octaves-mobile.png" : "note-chooser-four-octaves.png").createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds()), *stream));
    }
    const auto key = [&] (const juce::String& name) -> juce::Component*
    {
        for (auto* child : chooser->getChildren())
            if (child->getTitle() == name) return child;
        return nullptr;
    };
    auto* startKey = key ("C1");
    REQUIRE (startKey != nullptr);
    const auto start = startKey->getLocalBounds().toFloat().getCentre().withY (90);
    startKey->mouseDown (gridEvent (*startKey, start, start, juce::ModifierKeys::leftButtonModifier));
    CHECK (editor.controller.state.playback.channels[0].note == 24);
    REQUIRE (chooser->isVisible());
    for (const auto& name : { "F#2", "F#2", "B4" })
    {
        auto* target = key (name);
        REQUIRE (target != nullptr);
        const auto point = startKey->getLocalPoint (target, target->getLocalBounds().getCentre()).toFloat();
        startKey->mouseDrag (gridEvent (*startKey, point, start, juce::ModifierKeys::leftButtonModifier, true));
        CHECK (chooser->isVisible());
    }
    CHECK (editor.controller.state.playback.channels[0].note == 71);
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    processor.processBlock (audio, midi);
    std::vector<int> heard;
    for (const auto event : midi)
        if (event.getMessage().isNoteOn()) heard.push_back (event.getMessage().getNoteNumber());
    CHECK (heard == std::vector<int> { 24, 42, 71 });
    chooser->dismiss();
    CHECK (processor.engineStateSnapshot().channels[0].note == 71);
}

TEST_CASE ("Inspector cycle edits show the complete Chance cell", "[native-ui]")
{
    PluginProcessor processor;
    const int roll = GENERATE (1, 3);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 60, 4, 5, roll }));
    EditorController controller (processor);
    controller.selected.set (0);
    PatternGrid grid (controller);
    grid.setSize (Layout::fullWidth, juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();
    const auto cellBounds = grid.cellBounds ({ 0, 0 }).toNearestInt();
    controller.layer = Layer::chance;
    const auto chanceCell = grid.createComponentSnapshot (cellBounds);
    controller.layer = Layer::velocity;
    PreviewPanel inspector (controller);
    CycleEditor* cycle = nullptr;
    for (auto* child : inspector.getChildren())
        if (auto* editor = dynamic_cast<CycleEditor*> (child))
            cycle = editor;
    REQUIRE (cycle != nullptr);
    cycle->sync();
    bool sawCycle = false;
    controller.changed = [&]
    {
        if (controller.layer != Layer::cycle || sawCycle)
            return;
        sawCycle = true;
        const auto cycleCell = grid.createComponentSnapshot (cellBounds);
        int changedPixels = 0;
        for (int y = 0; y < cycleCell.getHeight(); ++y)
            for (int x = 0; x < cycleCell.getWidth(); ++x)
                if (cycleCell.getPixelAt (x, y) != chanceCell.getPixelAt (x, y))
                    ++changedPixels;
        CHECK (changedPixels == 0);
    };
    juce::Component* handle = nullptr;
    for (auto* child : cycle->getChildren())
        if (child->getTitle() == "Cycle length")
            handle = child;
    REQUIRE (handle != nullptr);
    const auto length = [&]
    {
        const auto& cell = controller.cell ({ 0, 0 });
        return roll == 1 ? cell.cycle : cell.splitGates.front().cycle();
    };
    const int originalLength = length();
    CHECK (handle->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (sawCycle);
    CHECK (controller.layer == Layer::velocity);
    CHECK (length() == originalLength + 1);
    CHECK (controller.cell ({ 0, 0 }).probability == 60);
    controller.undo();
    CHECK (length() == originalLength);
    controller.changed = {};
}

TEST_CASE ("Inspector overview layers stay visible through value gestures and reject grid painting", "[native-ui][inspector-overview]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 60, 4, 5, 3 }));
    EditorController controller (processor);
    controller.selected.set (0);
    PreviewPanel inspector (controller);
    PatternGrid grid (controller);
    inspector.sync();
    const auto layer = GENERATE (Layer::nudge, Layer::roll);
    const auto title = layer == Layer::nudge ? "Selected timing nudge" : "Selected split";
    ValueControl* control = nullptr;
    for (auto* child : inspector.getChildren())
        if (child->getTitle() == title) control = dynamic_cast<ValueControl*> (child);
    REQUIRE (control != nullptr);
    control->onBegin();
    CHECK (grid.displayLayer() == layer);
    const auto before = controller.cell ({ 0, 0 });
    control->onChange (layer == Layer::nudge ? 20 : 6);
    CHECK (grid.displayLayer() == layer);
    control->onEnd();
    CHECK (controller.layer == Layer::velocity);
    const auto after = controller.cell ({ 0, 0 });
    CHECK (after != before);
    controller.undo();
    CHECK (controller.cell ({ 0, 0 }) == before);
    controller.inspectorHoverLayer = layer;
    grid.layout.steps = controller.state.playback.stepCount;
    grid.layout.channels = controller.state.playback.channelCount;
    grid.setSize (1340, 600);
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        juce::File folder (directory);
        folder.createDirectory();
        juce::FileOutputStream output (folder.getChildFile ("overview-" + juce::String (static_cast<int> (layer)) + ".png"));
        juce::PNGImageFormat().writeImageToStream (grid.createComponentSnapshot (grid.getLocalBounds()), output);
    }
    const auto centre = grid.layout.cell ({ 0, 0 }).getCentre();
    grid.mouseDown (gridEvent (grid, centre, centre, juce::ModifierKeys::leftButtonModifier));
    grid.mouseUp (gridEvent (grid, centre, centre, 0));
    CHECK_FALSE (controller.editing());
    CHECK (controller.cell ({ 0, 0 }) == before);
    // Overview values are observational even if generic value helpers are called.
    CHECK (withValue (before, layer, 99) == before);
}

TEST_CASE ("Inspector double-click resets one field across the selection in one undo", "[native-ui][inspector-reset]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 86, 73, 4, 5, 3 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 2, 1, 71, 62, 3, 2, 2 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 95, 88, 2, 1, 4 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell_nudge", { 1, 1, 1, 15 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell_nudge", { 1, 1, 2, -8 }));
    EditorController controller (processor);
    controller.begin ("Set up split cycles");
    for (int step : { 0, 1 })
    {
        auto cell = controller.cell ({ 0, step });
        for (int part = 0; part < cell.roll; ++part)
            cell.splitGates[static_cast<size_t> (part)].setCycle (4, 5);
        controller.editCell ({ 0, step }, cell);
    }
    controller.commit();
    controller.clearHistory();
    controller.selected.set (0);
    controller.selected.set (1);
    PreviewPanel inspector (controller);
    inspector.setSize (1400, 174);
    inspector.setInspectorBounds ({ 288, 0, 1090, 174 });
    inspector.sync();
    const auto original = std::array { controller.cell ({ 0, 0 }), controller.cell ({ 0, 1 }), controller.cell ({ 0, 2 }) };
    auto expected = original;
    const auto field = GENERATE ("Velocity", "Prob", "Nudge", "Split", "Cycle");
    juce::Component* clickTarget = nullptr;
    juce::Component* handle = nullptr;
    CycleEditor* cycleEditor = nullptr;
    std::vector<std::pair<juce::Component*, juce::Rectangle<int>>> beatBounds;
    for (auto* child : inspector.getChildren())
    {
        if (auto* value = dynamic_cast<ValueControl*> (child); value != nullptr && value->caption == field)
            clickTarget = value;
        if (auto* cycle = dynamic_cast<CycleEditor*> (child); cycle != nullptr && juce::String (field) == "Cycle")
        {
            cycleEditor = cycle;
            for (auto* part : cycle->getChildren())
            {
                if (part->getTitle() == "Cycle length")
                    handle = part;
                if (auto* beat = dynamic_cast<juce::Button*> (part))
                {
                    beatBounds.emplace_back (beat, beat->getBounds());
                }
            }
        }
    }
    if (cycleEditor != nullptr)
    {
        const auto area = GENERATE (0, 1, 2, 3, 4);
        CAPTURE (area);
        REQUIRE (beatBounds.size() == 8);
        // Active, inactive and out-of-cycle beats, the handle, a gutter, and the caption.
        clickTarget = area < 3 ? beatBounds[std::array<size_t, 3> { 2, 1, 6 }[static_cast<size_t> (area)]].first
                              : area == 3 ? handle : cycleEditor;
    }
    REQUIRE (clickTarget != nullptr);
    const auto* cycleBeat = cycleEditor != nullptr ? dynamic_cast<juce::Button*> (clickTarget) : nullptr;
    const bool beatWasOn = cycleBeat != nullptr && cycleBeat->getToggleState();
    const int handleX = handle != nullptr ? handle->getX() : 0;
    auto point = clickTarget->getLocalBounds().toFloat().getCentre();
    if (clickTarget == cycleEditor)
    {
        const bool caption = GENERATE (false, true);
        point = caption ? cycleEditor->getLocalBounds().toFloat().getTopLeft().translated (2.0f, 5.0f)
                        : point.withX (1.0f);
        CHECK (cycleEditor->getComponentAt (point.toInt()) == cycleEditor);
    }
    for (int clicks : { 1, 2 })
    {
        const auto event = gridEvent (*clickTarget, point, point, juce::ModifierKeys::leftButtonModifier, false, clicks);
        clickTarget->mouseDown (event);
        clickTarget->mouseUp (event);
        if (clicks == 1)
        {
            CHECK_FALSE (controller.canUndo());
            if (cycleBeat != nullptr && cycleBeat->getButtonText().getIntValue() <= original.front().splitGates.front().cycle())
                CHECK (cycleBeat->getToggleState() != beatWasOn);
        }
        else
            clickTarget->mouseDoubleClick (event);
    }
    for (auto& cell : std::span (expected).first (2))
    {
        const juce::String name (field);
        if (name == "Velocity") cell.velocity = CellDefaults::velocity;
        else if (name == "Prob") cell.probability = CellDefaults::probability;
        else if (name == "Nudge") cell.nudge = CellDefaults::nudge;
        else if (name == "Split") cell.roll = CellDefaults::roll;
        else for (int part = 0; part < cell.roll; ++part)
            cell.splitGates[static_cast<size_t> (part)].setCycle (CellDefaults::cycle, CellDefaults::cycleMask);
    }
    for (int i = 0; i < 3; ++i)
        CHECK (controller.cell ({ 0, i }) == expected[static_cast<size_t> (i)]);
    for (const auto& [beat, bounds] : beatBounds)
        CHECK (beat->getBounds() == bounds);
    if (handle != nullptr)
        CHECK (handle->getX() < handleX);
    REQUIRE (controller.canUndo());
    controller.undo();
    for (int i = 0; i < 3; ++i)
        CHECK (controller.cell ({ 0, i }) == original[static_cast<size_t> (i)]);
    CHECK_FALSE (controller.canUndo());
}

TEST_CASE ("Inspector cycle edits support both whole steps and individual split hits", "[native-ui][split-edits]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    const Location split { 0, 0 }, single { 0, 8 }, unselected { 0, 9 };
    const int span = GENERATE (1, 4);
    const bool individual = GENERATE (false, true);
    Cell roll { .enabled = true, .velocity = 84, .cycle = 2, .cycleMask = 1, .roll = 3, .span = span };
    roll.splitGates[0].setCycle (4, 5);
    roll.splitGates[1].setCycle (4, 4);
    roll.splitGates[1].setProbability (23);
    roll.splitGates[3].setCycle (7, 64);
    roll.setSplitVelocity (2, 43);
    const Cell plain { .enabled = true, .cycle = 4, .cycleMask = 5 };
    controller.begin ("Set up cycle selection");
    controller.editCell (split, roll);
    controller.editCell (single, plain);
    controller.editCell (unselected, roll);
    controller.commit();
    controller.clearHistory();
    controller.selected.set (static_cast<size_t> (split.key()));
    controller.selected.set (static_cast<size_t> (single.key()));

    PatternGrid grid (controller);
    if (individual)
    {
        controller.layer = Layer::chance;
        controller.selectLatched = true;
        grid.setSize (1338, 554);
        grid.sync();
        const auto body = grid.layout.cellArtwork (grid.cellBounds (split));
        const auto point = body.getCentre().withY (body.getY() + 0.82f * body.getHeight());
        grid.mouseDown (gridEvent (grid, point, point, juce::ModifierKeys::leftButtonModifier));
        grid.mouseUp (gridEvent (grid, point, point, 0));
        REQUIRE (controller.selection() == std::vector { split });
        REQUIRE (controller.selectedSplitCycle());
        CHECK (controller.selectedSplitCycle()->index == 1);
    }

    PreviewPanel inspector (controller);
    inspector.setSize (1400, 174);
    inspector.setInspectorBounds ({ 288, 0, 1090, 174 });
    inspector.sync();
    CycleEditor* cycle = nullptr;
    for (auto* child : inspector.getChildren())
        if (auto* candidate = dynamic_cast<CycleEditor*> (child))
            cycle = candidate;
    REQUIRE (cycle != nullptr);
    juce::Component* handle = nullptr;
    std::array<juce::TextButton*, 8> beats {};
    for (auto* child : cycle->getChildren())
    {
        if (child->getTitle() == "Cycle length") handle = child;
        if (auto* beat = dynamic_cast<juce::TextButton*> (child))
            beats[static_cast<size_t> (beat->getButtonText().getIntValue() - 1)] = beat;
    }
    REQUIRE (handle != nullptr);
    REQUIRE (beats[0] != nullptr);
    REQUIRE (beats[1] != nullptr);
    REQUIRE (beats[2] != nullptr);
    REQUIRE (beats[3] != nullptr);
    CHECK_FALSE (beats[1]->getToggleState());
    CHECK (beats[0]->getToggleState() != individual);
    CHECK (beats[2]->getToggleState());

    const int gesture = GENERATE (0, 1, 2);
    int length = 4, mask = individual ? 4 : 5;
    if (gesture == 0)
    {
        beats[1]->onClick();
        mask ^= 1 << 1;
    }
    else if (gesture == 1)
    {
        CHECK (handle->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
        length = 5;
    }
    else
    {
        auto& beat = static_cast<juce::Component&> (*beats[2]);
        const auto start = beat.getLocalBounds().toFloat().getCentre();
        const auto end = beat.getLocalPoint (beats[3], beats[3]->getLocalBounds().toFloat().getCentre());
        beat.mouseDown (gridEvent (beat, start, start, juce::ModifierKeys::leftButtonModifier));
        beat.mouseDrag (gridEvent (beat, end, start, juce::ModifierKeys::leftButtonModifier, true));
        beat.mouseUp (gridEvent (beat, end, start, 0, true));
        mask = (mask & ~(1 << 2)) | (1 << 3);
    }

    auto expectedRoll = roll;
    for (int part = 0; part < roll.roll; ++part)
        if (!individual || part == 1)
            expectedRoll.splitGates[static_cast<size_t> (part)].setCycle (length, mask);
    auto expectedPlain = plain;
    if (!individual)
    {
        expectedPlain.cycle = length;
        expectedPlain.cycleMask = mask;
    }
    CHECK (controller.cell (split) == expectedRoll);
    CHECK (controller.cell (single) == expectedPlain);
    CHECK (controller.cell (unselected) == roll);
    REQUIRE (controller.canUndo());
    controller.undo();
    CHECK (controller.cell (split) == roll);
    CHECK (controller.cell (single) == plain);
    CHECK_FALSE (controller.canUndo());
    controller.redo();
    CHECK (controller.cell (split) == expectedRoll);
    CHECK (controller.cell (single) == expectedPlain);
}

TEST_CASE ("Cycle beat drags preview and commit only on a valid release", "[native-ui][cycle-drag]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 60, 4, 1, 2 }));
    EditorController controller (processor);
    CycleEditor cycle (controller);
    cycle.locations = [] { return std::vector<Location> { { 0, 0 } }; };
    cycle.setSize (400, 120);
    cycle.setVisible (true);
    cycle.sync();
    juce::Button* from = nullptr;
    juce::Button* to = nullptr;
    juce::Button* next = nullptr;
    juce::Component* handle = nullptr;
    for (auto* child : cycle.getChildren())
    {
        if (child->getTitle() == "Cycle length") handle = child;
        if (auto* button = dynamic_cast<juce::Button*> (child))
        {
            if (button->getButtonText() == "1") from = button;
            if (button->getButtonText() == "4") to = button;
            if (button->getButtonText() == "5") next = button;
        }
    }
    REQUIRE (from != nullptr);
    REQUIRE (to != nullptr);
    REQUIRE (next != nullptr);
    REQUIRE (handle != nullptr);
    CHECK (handle->getWidth() == Layout::rowHandleWidth);
    CHECK (handle->getX() < to->getRight());
    CHECK (to->getRight() + 4 == next->getX());
    CHECK (handle->getBounds().getCentreX() == to->getRight() + 2);
    CHECK (cycle.getComponentAt (handle->getBounds().getCentre()) == handle);
    CHECK (cycle.getComponentAt (to->getBounds().getCentre()) == to);
    const auto before = controller.cell ({ 0, 0 });
    const auto start = from->getLocalBounds().toFloat().getCentre();
    auto destination = from->getLocalPoint (to, to->getLocalBounds().toFloat().getCentre());
    const int mode = GENERATE (0, 1, 2, 3);
    if (mode == 2)
        destination.y = -20;
    auto& dragComponent = static_cast<juce::Component&> (*from);
    dragComponent.mouseDown (gridEvent (*from, start, start, juce::ModifierKeys::leftButtonModifier));
    dragComponent.mouseDrag (gridEvent (*from, destination, start, juce::ModifierKeys::leftButtonModifier, true));
    CHECK (controller.cell ({ 0, 0 }) == before);
    CHECK_FALSE (controller.canUndo());
    if (mode == 1)
        cycle.cancelGesture();
    if (mode == 3)
    {
        REQUIRE (processor.dispatchUiEngineCommand ("cell_probability", { 1, 1, 1, 30 }));
        controller.refresh();
    }
    dragComponent.mouseUp (gridEvent (*from, destination, start, 0, true));
    auto expected = before;
    if (mode == 0)
        expected.cycleMask = 8;
    if (mode == 3)
        expected.probability = 30;
    CHECK (controller.cell ({ 0, 0 }) == expected);
    if (mode == 0)
    {
        REQUIRE (controller.canUndo());
        controller.undo();
        CHECK (controller.cell ({ 0, 0 }) == before);
        CHECK_FALSE (controller.canUndo());
        controller.redo();
        CHECK (controller.cell ({ 0, 0 }) == expected);
    }
    else
        CHECK_FALSE (controller.canUndo());
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
    CHECK (handle->getMouseCursor() == horizontalResizeCursor());
    CHECK (handle->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (controller.cell ({ 0, 0 }).cycle == 5);
}

TEST_CASE ("Standalone restores the saved UI scale before creating its editor", "[native-ui]")
{
    juce::TemporaryFile temporary (".settings");
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    const int percent = GENERATE (50, 75, 100, 120, 200, 400);
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
    CHECK (editor->getWidth() == juce::roundToInt (view->layout.width() * UiScale::factor (percent)));
    CHECK (editor->getHeight() == juce::roundToInt (view->layout.height() * UiScale::factor (percent)));
    CHECK (nextSession.getProjectUiScalePercent() == percent);
}

TEST_CASE ("Legacy standalone zoom migrates to the smaller 100 percent baseline once", "[native-ui]")
{
    PluginProcessor processor;
    juce::PropertySet settings;
    settings.setValue ("uiScalePercent", 85);
    ksh::restoreStandaloneUiScale (processor, settings);
    CHECK (processor.getProjectUiScalePercent() == 100);
    ksh::saveStandaloneUiScale (processor, settings);
    ksh::restoreStandaloneUiScale (processor, settings);
    CHECK (processor.getProjectUiScalePercent() == 100);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    CHECK (editor->getWidth() == juce::roundToInt (Layout {}.width() * UiScale::factor (100)));
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
    CHECK (processor.getProjectUiScalePercent() == 400);
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
    const int subdivision = GENERATE (0, 1);
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
    runner.processBlock (controller.state.playback, 0.0, 120.0, true, 60000, midi);
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

TEST_CASE ("Hiding swing moves grid hits and editing targets while retaining nudge", "[native-ui][swing-display]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 4, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell_nudge", { 1, 1, 4, 15 }));
    REQUIRE (processor.dispatchUiEngineCommand ("swing", { 80 }));
    REQUIRE (processor.dispatchUiEngineCommand ("swing_subdivision", { 1 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (1338, 554);
    grid.sync();
    const Location location { 0, 3 };
    const float width = grid.layout.cellWidth();
    const auto swung = grid.cellBounds (location);
    const auto document = processor.editorDocument();

    grid.layout.showSwing = false;
    grid.sync();
    const auto straight = grid.cellBounds (location);
    CHECK (straight.getX() == Catch::Approx (grid.layout.cell (location).getX() + width * 0.15f));
    CHECK (swung.getX() - straight.getX() == Catch::Approx (width * 0.8f));
    REQUIRE (grid.getChildComponent (3) != nullptr);
    CHECK (grid.getChildComponent (3)->getBounds() == straight.toNearestInt());
    CHECK (processor.engineStateSnapshot().swing == 80);
    CHECK (processor.editorDocument() == document);

    const auto point = straight.getCentre();
    const int shift = juce::ModifierKeys::shiftModifier;
    grid.mouseDown (gridEvent (grid, point, point, shift | juce::ModifierKeys::leftButtonModifier));
    grid.mouseUp (gridEvent (grid, point, point, shift));
    CHECK (controller.selected.test (3));
}

TEST_CASE ("Selected empty steps follow visible swing in the grid", "[native-ui][swing-display]")
{
    const bool compact = GENERATE (false, true);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 2, 4, 1, 90 }));
    REQUIRE (processor.dispatchUiEngineCommand ("swing", { 80 }));
    REQUIRE (processor.dispatchUiEngineCommand ("swing_subdivision", { 1 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.layout.compact = compact;
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    const Location empty { 0, 3 };
    const auto slot = grid.layout.cell (empty);
    CHECK (grid.cellBounds (empty) == slot);

    controller.selected.set (static_cast<size_t> (empty.key()));
    grid.sync();
    const auto swung = grid.cellBounds (empty);
    CHECK (swung.getX() == Catch::Approx (slot.getX() + 0.8f * slot.getWidth()));
    CHECK (swung.getX() == Catch::Approx (grid.cellBounds ({ 1, 3 }).getX()));
    auto* accessible = grid.getChildComponent (empty.key());
    REQUIRE (accessible != nullptr);
    CHECK (accessible->getBounds() == swung.toNearestInt());

    grid.layout.showSwing = false;
    grid.sync();
    CHECK (grid.cellBounds (empty) == slot);
    CHECK (accessible->getBounds() == slot.toNearestInt());
    CHECK (controller.selected.test (static_cast<size_t> (empty.key())));

    grid.layout.showSwing = true;
    grid.sync();
    const auto point = swung.getCentre();
    REQUIRE (grid.layout.at (point) == Location { 0, 4 });
    grid.mouseDown (gridEvent (grid, point, point, juce::ModifierKeys::leftButtonModifier));
    grid.mouseUp (gridEvent (grid, point, point, 0));
    CHECK (controller.cell (empty).enabled);
    CHECK_FALSE (controller.cell ({ 0, 4 }).enabled);
}

TEST_CASE ("Swing display stays fixed through channel loops and directions", "[native-ui][swing-display]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 3, 1, 90 }));
    const int length = GENERATE (3, 4, 5);
    REQUIRE (processor.dispatchUiEngineCommand ("channel_loop_length", { 1, length, 2 }));
    REQUIRE (processor.dispatchUiEngineCommand ("swing", { 73 }));
    REQUIRE (processor.dispatchUiEngineCommand ("swing_subdivision", { 1 }));
    const int mode = GENERATE (0, 1, 2, 3);
    const std::array<const char*, 4> modes { "normal", "reverse", "ping_pong", "reverse_ping_pong" };
    REQUIRE (processor.dispatchUiEngineCommand ("channel_playback_mode", { 1, modes[static_cast<size_t> (mode)] }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.sync();
    const std::array<int, 6> reference { 0, 5, 8, 15, 32, 0 };
    const std::array<int, 4> expected { 1, length - 2, 1, length - 2 };
    const auto initialBounds = grid.getChildComponent (2)->getBounds();
    for (size_t visit = 0; visit < reference.size(); ++visit)
    {
        controller.playingStep = reference[visit] + 1;
        controller.playingGlobalStep = reference[visit];
        grid.sync();
        CHECK (grid.getChildComponent (2)->getBounds() == initialBounds);
        const double offset = swingDelayStepsForPosition (expected[static_cast<size_t> (mode)], 73, 1);
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
    controller.layer = GENERATE (Layer::velocity, Layer::chance, Layer::nudge, Layer::roll, Layer::cycle);
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
    if (overviewLayer (controller.layer))
    {
        CHECK (controller.selected.none());
        CHECK_FALSE (controller.editing());
    }
    else
    {
        CHECK (controller.selected.test (2));
        CHECK (controller.selected.count() == 1);
    }
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

TEST_CASE ("Pattern buttons distinguish clicks from MIDI drags and suppress selection after a drag", "[native-ui][midi-export]")
{
    PatternButton button;
    button.setBounds (0, 0, 80, 40);
    button.setVisible (true);
    int clicks = 0, drags = 0;
    button.onClick = [&] { ++clicks; };
    button.onDrag = [&] { ++drags; };
    const auto start = juce::Point<float> (20, 20);
    const int modifiers = juce::ModifierKeys::leftButtonModifier | GENERATE (0, static_cast<int> (juce::ModifierKeys::shiftModifier));
    auto down = [&]
    {
        button.mouseDown (gridEvent (button, start, start, modifiers));
        CHECK (button.isDown());
    };
    down();
    button.mouseUp (gridEvent (button, start, start, 0));
    CHECK (clicks == 1);

    down();
    button.mouseDrag (gridEvent (button, start.translated (3, 0), start, modifiers, true));
    CHECK (drags == 0);
    button.mouseDrag (gridEvent (button, start.translated (8, 0), start, modifiers, true));
    button.mouseDrag (gridEvent (button, start.translated (20, 0), start, modifiers, true));
    CHECK (drags == 1);
    // Returning over the button after a cancelled native drag must not select or copy it.
    button.mouseUp (gridEvent (button, start, start, 0, true));
    CHECK (clicks == 1);
    CHECK_FALSE (button.isDown());

    down();
    button.mouseUp (gridEvent (button, start, start, 0));
    CHECK (clicks == 2);
}

TEST_CASE ("Record lock stays independent beside Record with a full touch target on iPhone", "[native-ui][record-ui][render]")
{
    const bool compact = GENERATE (false, true);
    PluginProcessor processor;
    auto editor = std::make_unique<NativeEditor> (processor);
    if (compact)
    {
        editor->setCompactViewport (true, 844, 390);
        editor->setSize (844, 390);
    }
    else
        editor->setSize (juce::roundToInt (editor->layout.width()), juce::roundToInt (editor->layout.height()));
    editor->sync();
    if (compact)
    {
        juce::TextButton* pattern = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == "Pattern")
                pattern = button;
        REQUIRE (pattern != nullptr);
        pattern->onClick();
    }
    SettingsButton* lock = nullptr;
    juce::TextButton* record = nullptr;
    for (auto* child : editor->getChildren())
    {
        if (auto* button = dynamic_cast<SettingsButton*> (child); button != nullptr && button->getProperties()["icon"].toString() == "recordLock")
            lock = button;
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getProperties()["icon"].toString() == "record")
            record = button;
    }
    REQUIRE (lock != nullptr);
    REQUIRE (record != nullptr);
    CHECK (lock->isVisible());
    CHECK (lock->isEnabled());
    CHECK (lock->getToggleState());
    if (!compact)
    {
        CHECK (lock->getBounds().getCentreX() > record->getBounds().getCentreX());
        CHECK (lock->getBounds().getCentreY() == record->getBounds().getCentreY());
        CHECK_FALSE (lock->getBounds().intersects (record->getBounds()));
        CHECK (lock->getX() > record->getRight());
        CHECK (lock->getY() == record->getY());
        CHECK (lock->getHeight() == record->getHeight());
    }
    else
    {
        CHECK_FALSE (record->isVisible());
        CHECK (lock->getWidth() >= 44);
        CHECK (lock->getHeight() >= 44);
    }
    CHECK (static_cast<bool> (lock->getProperties()["outlinedIcon"]));
    if (!compact)
        CHECK_FALSE (lock->getBounds().contains (record->getBounds().getCentre()));
    CHECK (lock->hitTest (0, 0));
    CHECK (lock->hitTest (lock->getWidth() - 1, lock->getHeight() - 1));
    CHECK (lock->hitTest (lock->getWidth() / 2, lock->getHeight() / 2 + 4));
    CHECK (lock->getTitle() == "Record quantization on");
    lock->setToggleState (false, juce::dontSendNotification);
    lock->onClick();
    CHECK_FALSE (processor.isPatternRecordingQuantized());
    CHECK_FALSE (processor.isPatternRecordingEnabled());
    CHECK (lock->getTitle() == "Record quantization off");
    record->onClick();
    CHECK (processor.isPatternRecordingEnabled());
    CHECK_FALSE (processor.isPatternRecordingQuantized());
    processor.setPatternRecordingQuantizeStrength (50);
    editor->sync();
    CHECK_FALSE (lock->getToggleState());
    CHECK (lock->getTooltip().contains ("50%"));
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        auto stream = juce::File (directory).getChildFile (compact ? "record-lock-compact.png" : "record-lock-desktop.png").createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), *stream));
    }
    editor.reset();
    NativeEditor reopened (processor);
    CHECK (processor.getPatternRecordingQuantizeStrength() == 50);
    CHECK (processor.isPatternRecordingEnabled());
}

TEST_CASE ("Inactive cycle turns flash without a grid note hit", "[native-ui]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 90, 100, 3, 5, 1 }));
    EditorController controller (processor);
    CycleEditor cycle (controller);
    cycle.locations = [] { return std::vector<Location> { { 0, 0 } }; };
    cycle.setSize (400, 120);
    controller.playingStep = 1;
    cycle.sync();
    juce::TextButton* inactive = nullptr;
    for (auto* child : cycle.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == "2")
            inactive = button;
    REQUIRE (inactive != nullptr);
    REQUIRE_FALSE (inactive->getToggleState());
    const auto before = inactive->createComponentSnapshot (inactive->getLocalBounds());

    processor.getUiBridge().emitCycleVisit ({ 1, 1, 1, 2 });
    cycle.updatePlayback();
    const auto after = inactive->createComponentSnapshot (inactive->getLocalBounds());
    const int midY = inactive->getHeight() / 2;
    CHECK (after.getPixelAt (1, midY).getPerceivedBrightness() > before.getPixelAt (1, midY).getPerceivedBrightness());
    CHECK (after.getPixelAt (4, midY) == before.getPixelAt (4, midY));
    CHECK_FALSE (inactive->getToggleState());
    CHECK (controller.sourceFlashes[0][0] == 0.0);
    CHECK (controller.generatedFlashes[0][0] == 0.0);

    controller.playingStep = 0;
    cycle.updatePlayback();
    CHECK (inactive->createComponentSnapshot (inactive->getLocalBounds()).getPixelAt (1, midY) == before.getPixelAt (1, midY));
}

TEST_CASE ("Transport leaves cells unchanged without note hits", "[native-ui][render]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.layout.compact = GENERATE (false, true);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();
    const auto before = grid.createComponentSnapshot (grid.getLocalBounds());
    controller.playingStep = 1;
    controller.playingGlobalStep = 0;
    grid.updatePlayback();
    const auto pulsing = grid.createComponentSnapshot (grid.getLocalBounds());
    for (int ch = 0; ch < grid.layout.channels; ++ch)
    {
        const auto active = grid.layout.cell ({ ch, 0 }).getSmallestIntegerContainer().getCentre();
        const auto neighbor = grid.layout.cell ({ ch, 1 }).getSmallestIntegerContainer().getCentre();
        CHECK (pulsing.getPixelAt (active.x, active.y) == before.getPixelAt (active.x, active.y));
        CHECK (pulsing.getPixelAt (neighbor.x, neighbor.y) == before.getPixelAt (neighbor.x, neighbor.y));
        CHECK (controller.sourceFlashes[static_cast<size_t> (ch)][0] == 0.0);
    }
    controller.playingStep = 0;
    grid.updatePlayback();
    const auto stopped = grid.createComponentSnapshot (grid.getLocalBounds());
    const auto active = grid.layout.cell ({ 0, 0 }).getSmallestIntegerContainer().getCentre();
    CHECK (stopped.getPixelAt (active.x, active.y) == before.getPixelAt (active.x, active.y));
}

TEST_CASE ("Grid hits brighten the whole cell with values shown or hidden", "[native-ui][render]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 40 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 2, 1, 40 }));
    EditorController controller (processor);
    controller.graphicalCellValues = GENERATE (false, true);
    PatternGrid grid (controller);
    grid.setSize (juce::roundToInt (grid.layout.width()), juce::roundToInt (grid.layout.gridHeight()));
    const auto cell = grid.layout.cell ({ 0, 0 }).getSmallestIntegerContainer();
    const auto neighbor = grid.layout.cell ({ 0, 1 }).getSmallestIntegerContainer();
    const auto before = grid.createComponentSnapshot (grid.getLocalBounds());
    NativeHit hit;
    hit.uiChannel = hit.uiSource = hit.uiSourceStep = hit.uiGeneratedStep = 1;
    processor.getUiBridge().emitNoteHit (hit);
    grid.updatePlayback();
    const auto after = grid.createComponentSnapshot (grid.getLocalBounds());
    for (int y : { cell.getY() + 6, cell.getBottom() - 6 })
        CHECK (after.getPixelAt (cell.getX() + 6, y).getPerceivedBrightness()
               > before.getPixelAt (cell.getX() + 6, y).getPerceivedBrightness());
    CHECK (after.getPixelAt (neighbor.getX() + 6, neighbor.getY() + 6)
           == before.getPixelAt (neighbor.getX() + 6, neighbor.getY() + 6));
    controller.sourceFlashes[0][0] = 0.0;
    grid.updatePlayback();
    const auto settled = grid.createComponentSnapshot (grid.getLocalBounds());
    CHECK (settled.getPixelAt (cell.getX() + 6, cell.getY() + 6)
           == before.getPixelAt (cell.getX() + 6, cell.getY() + 6));
    processor.getUiBridge().emitNoteHit (hit);
    CHECK (controller.sourceFlashes[0][0] > juce::Time::getMillisecondCounterHiRes());
}

TEST_CASE ("Logo letters and backdrop edit the shared theme without changing the project", "[native-ui][render][accent]")
{
    struct RestoreAccent
    {
        bool blue = Palette::blueAccentSelected();
        Palette::AccentColours colours { Palette::accentOverride (false), Palette::accentOverride (true) };
        Palette::BackgroundTone backdrop = Palette::backgroundTone();
        ~RestoreAccent()
        {
            Palette::setAccentPreferences (blue, colours);
            Palette::setBackgroundTone (backdrop);
        }
    } restore;
    juce::TemporaryFile settings (".settings");
    juce::PropertiesFile::Options options;
    options.millisecondsBeforeSaving = -1;
    const auto preferences = std::make_shared<juce::PropertiesFile> (settings.getFile(), options);
    PluginProcessor firstProcessor, secondProcessor;
    const auto firstEditor = std::make_unique<NativeEditor> (firstProcessor, preferences);
    const auto secondEditor = std::make_unique<NativeEditor> (secondProcessor, preferences);
    auto& first = *firstEditor;
    auto& second = *secondEditor;
    first.setSize (juce::roundToInt (first.layout.width()), juce::roundToInt (first.layout.height()));
    second.setSize (juce::roundToInt (second.layout.width()), juce::roundToInt (second.layout.height()));
    PreviewPanel* inspector = nullptr;
    for (auto* child : first.getChildren())
        if (auto* candidate = dynamic_cast<PreviewPanel*> (child))
            inspector = candidate;
    REQUIRE (inspector != nullptr);
    AccentWordmark* wordmark = nullptr;
    for (auto* child : inspector->getChildren())
        if (auto* candidate = dynamic_cast<AccentWordmark*> (child))
            wordmark = candidate;
    REQUIRE (wordmark != nullptr);
    juce::TextButton* orange = nullptr;
    juce::TextButton* blue = nullptr;
    juce::TextButton* backdrop = nullptr;
    for (auto* child : wordmark->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child))
        {
            if (button->getTitle() == "Orange accent") orange = button;
            if (button->getTitle() == "Blue accent") blue = button;
            if (button->getTitle() == "Background tone") backdrop = button;
        }
    REQUIRE (orange != nullptr);
    REQUIRE (blue != nullptr);
    REQUIRE (backdrop != nullptr);
    REQUIRE (orange->isVisible());
    REQUIRE (blue->isVisible());
    CHECK (orange->getRight() < blue->getX());
    const auto document = firstProcessor.editorDocument();
    blue->onClick();
    CHECK_FALSE (first.controller.canUndo());
    for (auto* button : { blue, orange, blue })
    {
        button->onClick();
        const auto expected = button == blue ? Palette::blueAccent : Palette::orangeAccent;
        CHECK (Palette::accent == expected);
        CHECK (Palette::channel (1) == Palette::orangeAccent);
        CHECK (Palette::channel (0, false) == expected);
        for (auto* editor : { &first, &second })
        {
            CHECK (editor->getLookAndFeel().findColour (juce::PopupMenu::highlightedTextColourId) == expected);
            CHECK (editor->getLookAndFeel().findColour (juce::TextEditor::focusedOutlineColourId) == expected);
        }
        CHECK_FALSE (second.controller.canUndo());
        CHECK (firstProcessor.editorDocument() == document);
    }
    const auto event = [] (juce::TextButton& button, int y, int modifiers = juce::ModifierKeys::leftButtonModifier, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        const auto center = button.getLocalBounds().toFloat().getCentre();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), center.translated (0, static_cast<float> (y)),
                                juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &button, &button, now, center, now, clicks, y != 0);
    };
    const auto renderLogo = []
    {
        juce::Image image (juce::Image::ARGB, 300, 140, true);
        juce::Graphics graphics (image);
        AccentWordmark::paintBranding (graphics, image.getBounds().toFloat());
        return image;
    };
    const auto factoryLogo = renderLogo();
    const auto checkShadeLimit = [] (juce::Colour base, bool brighter)
    {
        CHECK (Palette::accent.getHue() == Catch::Approx (base.getHue()).margin (0.006f));
        CHECK (Palette::accent.getSaturationHSL() == Catch::Approx (base.getSaturationHSL()).margin (0.015f));
        CHECK (Palette::accent.getLightness() == Catch::Approx (brighter ? 0.88f : 0.20f).margin (0.004f));
    };
    auto& blueDragTarget = static_cast<juce::Component&> (*blue);
    auto& orangeDragTarget = static_cast<juce::Component&> (*orange);
    orange->onClick();
    blueDragTarget.mouseDown (event (*blue, 0));
    CHECK (Palette::blueAccentSelected());
    CHECK (Palette::accent == Palette::blueAccent);
    blueDragTarget.mouseDrag (event (*blue, 120));
    checkShadeLimit (Palette::blueAccent, false);
    blueDragTarget.mouseDrag (event (*blue, -120));
    checkShadeLimit (Palette::blueAccent, true);
    blueDragTarget.mouseDrag (event (*blue, -45));
    CHECK (Palette::accent.getPerceivedBrightness() > Palette::blueAccent.getPerceivedBrightness());
    CHECK (second.getLookAndFeel().findColour (juce::PopupMenu::highlightedTextColourId) == Palette::accent);
    blueDragTarget.mouseUp (event (*blue, -45, 0));
    const auto adjustedBlue = Palette::accent;
    orangeDragTarget.mouseDown (event (*orange, 0));
    CHECK_FALSE (Palette::blueAccentSelected());
    CHECK (Palette::accent == Palette::orangeAccent);
    orangeDragTarget.mouseDrag (event (*orange, 250));
    checkShadeLimit (Palette::orangeAccent, false);
    orangeDragTarget.mouseDrag (event (*orange, -120));
    checkShadeLimit (Palette::orangeAccent, true);
    orangeDragTarget.mouseDrag (event (*orange, 30));
    orangeDragTarget.mouseUp (event (*orange, 30, 0));
    const auto adjustedOrange = Palette::accent;
    CHECK (adjustedOrange.getPerceivedBrightness() < Palette::orangeAccent.getPerceivedBrightness());
    CHECK (Palette::accentColour (true) == adjustedBlue);
    CHECK_FALSE (Palette::blueAccentSelected());
    const auto shadedLogo = renderLogo();
    CHECK (shadedLogo.getPixelAt (41, 70).getPerceivedBrightness() < factoryLogo.getPixelAt (41, 70).getPerceivedBrightness());
    CHECK (shadedLogo.getPixelAt (193, 70).getPerceivedBrightness() > factoryLogo.getPixelAt (193, 70).getPerceivedBrightness());
    REQUIRE (first.controller.canUndo());
    first.controller.undo();
    CHECK (Palette::blueAccentSelected());
    CHECK_FALSE (Palette::accentOverride (false).has_value());
    CHECK (Palette::accent == adjustedBlue);
    first.controller.undo();
    CHECK_FALSE (Palette::blueAccentSelected());
    CHECK_FALSE (Palette::accentOverride (true).has_value());
    CHECK (Palette::accent == Palette::orangeAccent);
    juce::PropertiesFile undoneColour (settings.getFile(), options);
    CHECK_FALSE (undoneColour.getBoolValue ("blueAccent", true));
    CHECK_FALSE (undoneColour.containsKey ("blueAccentColour"));
    first.controller.redo();
    CHECK (Palette::accent == adjustedBlue);
    first.controller.redo();
    CHECK (Palette::accent == adjustedOrange);
    CHECK (firstProcessor.editorDocument() == document);

    Palette::setAccentPreferences (true, {});
    const auto reopenedPreferences = std::make_shared<juce::PropertiesFile> (settings.getFile(), options);
    NativeEditor reopened (secondProcessor, reopenedPreferences);
    CHECK_FALSE (Palette::blueAccentSelected());
    CHECK (Palette::accentColour (true) == adjustedBlue);
    CHECK (Palette::accentColour (false) == adjustedOrange);
    CHECK (reopened.getLookAndFeel().findColour (juce::PopupMenu::highlightedTextColourId) == adjustedOrange);
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        juce::File folder (directory);
        REQUIRE (folder.createDirectory().wasOk());
        auto output = folder.getChildFile ("custom-accent.png").createOutputStream();
        REQUIRE (output != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (first.createComponentSnapshot (first.getLocalBounds()), *output));
    }
    blue->mouseDoubleClick (event (*blue, 0, 0, 2));
    CHECK_FALSE (Palette::accentOverride (true).has_value());
    CHECK (Palette::accentColour (true) == Palette::blueAccent);
    CHECK (Palette::accent == adjustedOrange);
    orangeDragTarget.mouseDown (event (*orange, 0));
    const auto centre = orange->getLocalBounds().toFloat().getCentre();
    const auto dragColour = [&] (float y)
    {
        orangeDragTarget.mouseDrag (gridEvent (*orange, centre.translated (80, y), centre, juce::ModifierKeys::leftButtonModifier, true));
    };
    dragColour (0);
    CHECK (std::abs (Palette::accent.getHue() - adjustedOrange.getHue()) > 0.25f);
    CHECK (Palette::accent.getLightness() == Catch::Approx (adjustedOrange.getLightness()).margin (0.004f));
    const float horizontalHue = Palette::accent.getHue();
    dragColour (-35);
    const auto custom = Palette::accent;
    CHECK (custom.getHue() == Catch::Approx (horizontalHue).margin (0.006f));
    CHECK (custom.getLightness() > adjustedOrange.getLightness());
    CHECK (Palette::accentColour (true) == Palette::blueAccent);
    CHECK (second.getLookAndFeel().findColour (juce::PopupMenu::highlightedTextColourId) == custom);
    orangeDragTarget.mouseUp (event (*orange, -35, 0));
    juce::PropertiesFile colourSettings (settings.getFile(), options);
    CHECK (colourSettings.getValue ("orangeAccentColour") == custom.toString());

    const auto originalBackground = Palette::app;
    const auto originalRaised = Palette::raised;
    juce::TextButton themeButton;
    themeButton.setLookAndFeel (&first.getLookAndFeel());
    themeButton.setSize (64, 44);
    const auto buttonFaceColour = [&]
    {
        return themeButton.createComponentSnapshot (themeButton.getLocalBounds()).getPixelAt (16, 12);
    };
    const auto originalButton = buttonFaceColour();
    const auto colourTemperature = [] (juce::Colour colour)
    {
        return static_cast<int> (colour.getRed()) - colour.getBlue();
    };
    auto& backdropTarget = static_cast<juce::Component&> (*backdrop);
    const auto backdropCentre = backdrop->getLocalBounds().toFloat().getCentre();
    const auto dragBackdrop = [&] (float x, float y)
    {
        backdropTarget.mouseDrag (gridEvent (*backdrop, backdropCentre.translated (x, y), backdropCentre,
                                             juce::ModifierKeys::leftButtonModifier, true));
    };
    backdropTarget.mouseDown (event (*backdrop, 0));
    dragBackdrop (160, 0);
    CHECK (Palette::backgroundTone() == Palette::BackgroundTone { 0, 100 });
    CHECK (Palette::raised.getRed() > Palette::raised.getBlue());
    const int buttonWarming = colourTemperature (buttonFaceColour()) - colourTemperature (originalButton);
    CHECK (buttonWarming > 0);
    CHECK (buttonWarming < colourTemperature (Palette::raised) - colourTemperature (originalRaised));
    dragBackdrop (-160, 0);
    CHECK (Palette::raised.getBlue() > Palette::raised.getRed());
    dragBackdrop (80, -80);
    CHECK (Palette::backgroundTone() == Palette::BackgroundTone { 56, 56 });
    CHECK (Palette::app.getPerceivedBrightness() > originalBackground.getPerceivedBrightness());
    CHECK (Palette::raised.getPerceivedBrightness() > originalRaised.getPerceivedBrightness());
    const float buttonBrightening = buttonFaceColour().getPerceivedBrightness() - originalButton.getPerceivedBrightness();
    CHECK (buttonBrightening > 0);
    CHECK (buttonBrightening < Palette::raised.getPerceivedBrightness() - originalRaised.getPerceivedBrightness());
    CHECK (second.getLookAndFeel().findColour (juce::TextEditor::backgroundColourId) == Palette::controls.app);
    CHECK (Palette::accent == custom);
    dragBackdrop (-80, 80);
    CHECK (Palette::app.getPerceivedBrightness() < originalBackground.getPerceivedBrightness());
    CHECK (Palette::raised.getPerceivedBrightness() < originalRaised.getPerceivedBrightness());
    const float buttonDarkening = originalButton.getPerceivedBrightness() - buttonFaceColour().getPerceivedBrightness();
    CHECK (buttonDarkening > 0);
    CHECK (buttonDarkening < originalRaised.getPerceivedBrightness() - Palette::raised.getPerceivedBrightness());
    backdropTarget.mouseUp (event (*backdrop, 80, 0));
    juce::PropertiesFile savedTone (settings.getFile(), options);
    CHECK (savedTone.getIntValue ("backgroundBrightness") == -56);
    CHECK (savedTone.getIntValue ("backgroundTint") == -56);
    first.controller.begin ("Enable step");
    auto step = first.controller.cell ({ 0, 0 });
    step.enabled = !step.enabled;
    first.controller.editCell ({ 0, 0 }, step);
    first.controller.commit();
    first.controller.undo();
    CHECK (firstProcessor.editorDocument() == document);
    CHECK (Palette::backgroundTone() == Palette::BackgroundTone { -56, -56 });
    const juce::KeyPress undo ('Z', juce::ModifierKeys::commandModifier, 'z');
    CHECK (first.keyPressed (undo));
    CHECK (Palette::backgroundTone() == Palette::BackgroundTone { 0, 0 });
    CHECK (Palette::accent == custom);
    first.controller.redo();
    CHECK (Palette::backgroundTone() == Palette::BackgroundTone { -56, -56 });
    CHECK (firstProcessor.editorDocument() == document);
    backdropTarget.mouseDoubleClick (event (*backdrop, 0, 0, 2));
    CHECK (Palette::backgroundTone() == Palette::BackgroundTone { 0, 0 });
    CHECK (Palette::app == originalBackground);
    CHECK (Palette::raised == originalRaised);
    CHECK (buttonFaceColour() == originalButton);
    CHECK (Palette::accent == custom);
    juce::PropertiesFile resetTone (settings.getFile(), options);
    CHECK (resetTone.getIntValue ("backgroundBrightness", 1) == 0);
    CHECK (resetTone.getIntValue ("backgroundTint", 1) == 0);
    orange->mouseDoubleClick (event (*orange, 0, 0, 2));
    CHECK_FALSE (Palette::accentOverride (false).has_value());
    CHECK (Palette::accent == Palette::orangeAccent);
    juce::PropertiesFile resetColour (settings.getFile(), options);
    CHECK_FALSE (resetColour.containsKey ("orangeAccentColour"));

    preferences->setValue ("orangeAccentShade", -30);
    preferences->setValue ("blueAccentColourMode", true);
    preferences->setValue ("blueAccentColour", adjustedBlue.toString());
    const auto legacy = std::make_unique<NativeEditor> (secondProcessor, preferences);
    const auto legacyOrange = Palette::orangeAccent.withLightness (Palette::orangeAccent.getLightness() * 0.85f);
    CHECK (Palette::accentColour (false) == legacyOrange);
    CHECK (Palette::accentColour (true) == adjustedBlue);
    CHECK (first.controller.canUndo());
    CHECK (firstProcessor.editorDocument() == document);
}

TEST_CASE ("Track color popover stays above toolbar segments and follows the visible row order", "[native-ui]")
{
    PluginProcessor processor;
    NativeEditor editor (processor);
    PatternGrid* grid = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* candidate = dynamic_cast<PatternGrid*> (child))
            grid = candidate;
    REQUIRE (grid != nullptr);
    const int firstTrack = editor.layout.channelForRow (0);
    const int secondTrack = editor.layout.channelForRow (1);
    auto* anchor = grid->rowTitle (firstTrack);
    REQUIRE (anchor != nullptr);
    const auto point = anchor->getLocalBounds().toFloat().getCentre();
    anchor->mouseDown (gridEvent (*anchor, point, point, juce::ModifierKeys::rightButtonModifier));
    anchor->mouseUp (gridEvent (*anchor, point, point, 0));

    auto* popup = dynamic_cast<juce::CallOutBox*> (juce::Component::getCurrentlyModalComponent());
    REQUIRE (popup != nullptr);
    REQUIRE (popup->getParentComponent() == &editor);
    REQUIRE (popup->getNumChildComponents() == 1);
    auto* picker = popup->getChildComponent (0);
    CHECK (picker->getTitle() == "Track " + juce::String (firstTrack + 1) + " color");
    editor.sync();
    CHECK (popup->isAlwaysOnTop());
    CHECK (editor.getChildComponent (editor.getNumChildComponents() - 1) == popup);
    CHECK (picker->keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (picker->getTitle() == "Track " + juce::String (secondTrack + 1) + " color");
    CHECK (picker->keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
    CHECK (picker->getTitle() == "Track " + juce::String (firstTrack + 1) + " color");
    auto* secondAnchor = grid->rowTitle (secondTrack);
    REQUIRE (secondAnchor != nullptr);
    const auto secondPoint = secondAnchor->getLocalBounds().toFloat().getCentre();
    secondAnchor->mouseDown (gridEvent (*secondAnchor, secondPoint, secondPoint, juce::ModifierKeys::rightButtonModifier));
    secondAnchor->mouseUp (gridEvent (*secondAnchor, secondPoint, secondPoint, 0));
    CHECK (juce::Component::getCurrentlyModalComponent() == popup);
    CHECK (picker->getTitle() == "Track " + juce::String (secondTrack + 1) + " color");
    popup->exitModalState (0);
    popup->setVisible (false);
}

TEST_CASE ("Non-phone compact accent belongs to View and leaves both step pages available", "[native-ui][accent][compact]")
{
    PluginProcessor processor;
    const int steps = GENERATE (16, 32);
    REQUIRE (processor.dispatchUiEngineCommand ("steps", { steps }));
    NativeEditor editor (processor);
    editor.setCompactViewport (true, 814, 409, false);
    editor.setSize (814, 409);
    juce::TextButton* logo = nullptr;
    juce::Component* page = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (child->getTitle() == "Switch accent")
            logo = dynamic_cast<juce::TextButton*> (child);
        if (child->getTitle() == "Switch step page")
            page = child;
    }
    REQUIRE (logo != nullptr);
    REQUIRE (page != nullptr);
    CHECK_FALSE (logo->isVisible());
    if (steps == 32)
        CHECK (page->isVisible());
    for (const auto& caption : { juce::String ("Settings"), juce::String ("View") })
        for (auto* child : editor.getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->isVisible() && button->getButtonText() == caption)
                button->onClick();
    REQUIRE (logo->isVisible());
    CHECK_FALSE (page->isVisible());
    const bool initialBlue = Palette::blueAccentSelected();
    const auto document = processor.editorDocument();
    logo->onClick();
    CHECK (Palette::blueAccentSelected() == !initialBlue);
    CHECK (Palette::channel (1) == Palette::orangeAccent);
    CHECK (processor.editorDocument() == document);
    logo->onClick();
    CHECK (Palette::blueAccentSelected() == initialBlue);
}

TEST_CASE ("Standalone MIDI destination is reachable in full and compact layouts without changing the project", "[native-ui][standalone-midi]")
{
    const bool compact = GENERATE (false, true);
    const bool standalone = GENERATE (false, true);
    juce::AudioProcessor::setTypeOfNextNewPlugin (standalone ? juce::AudioProcessor::wrapperType_Standalone : juce::AudioProcessor::wrapperType_AudioUnitv3);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    juce::Component* menuTarget = nullptr;
    juce::String destination = "Network Session 1";
    if (standalone)
    {
        processor.showStandaloneMidiOutputMenu = [&] (juce::Component& target) { menuTarget = &target; };
        processor.standaloneMidiOutputName = [&] { return destination; };
    }
    NativeEditor editor (processor);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    if (compact)
    {
        editor.setCompactViewport (true, 814, 409);
        editor.setSize (814, 409);
        for (const auto& caption : { juce::String ("Menu"), juce::String ("Settings") })
            for (auto* child : editor.getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == caption)
                    button->onClick();
    }
    editor.sync();
    juce::TextButton* output = nullptr;
    for (auto* child : editor.getChildren())
        if (child->getTitle() == "MIDI output destination")
            output = dynamic_cast<juce::TextButton*> (child);
    REQUIRE (output != nullptr);
    CHECK (output->isVisible() == standalone);
    if (standalone)
    {
        CHECK (editor.getLocalBounds().contains (output->getBounds()));
        CHECK (output->getHeight() >= 32);
        CHECK (output->getTooltip().contains (destination));
        const auto document = processor.editorDocument();
        output->onClick();
        CHECK (menuTarget == output);
        destination = "USB MIDI";
        editor.sync();
        CHECK (output->getTooltip().contains (destination));
        CHECK (processor.editorDocument() == document);
    }
}

TEST_CASE ("Metronome settings open inside the scaled standalone editor", "[native-ui][metronome]")
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        SKIP ("JUCE popup menus require a display.");

    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    juce::Component windowContent;
    NativeEditor editor (processor);
    editor.setSize (juce::roundToInt (editor.layout.width()), juce::roundToInt (editor.layout.height()));
    editor.setTransform (juce::AffineTransform::scale (0.65f));
    const auto editorBounds = editor.getBoundsInParent();
    windowContent.setSize (editorBounds.getWidth(), editorBounds.getHeight());
    windowContent.addAndMakeVisible (editor);

    const auto children = editor.getChildren();
    const auto metronome = std::find_if (children.begin(), children.end(),
                                        [] (const auto* child) { return child->getTitle() == "Metronome"; });
    REQUIRE (metronome != children.end());
    REQUIRE ((*metronome)->keyPressed (juce::KeyPress (juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0)));
    if (const auto* popup = juce::Component::getCurrentlyModalComponent())
    {
        CHECK (popup->getParentComponent() == &windowContent);
        CHECK (windowContent.getLocalBounds().contains (popup->getBounds()));
        juce::PopupMenu::dismissAllActiveMenus();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
    }
    else
        FAIL ("The metronome settings popup did not open.");
}

TEST_CASE ("Shared desktop and iPad controls fit the full layout on a small tablet", "[native-ui][unified-layout][render]")
{
    const bool standalone = GENERATE (false, true);
    juce::AudioProcessor::setTypeOfNextNewPlugin (standalone ? juce::AudioProcessor::wrapperType_Standalone : juce::AudioProcessor::wrapperType_Undefined);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    int audioSettingsOpens = 0;
    if (standalone)
    {
        processor.showStandaloneMidiOutputMenu = [] (juce::Component&) {};
        processor.showStandaloneLinkSettings = [] (juce::Component&) {};
        processor.showStandaloneAudioSettings = [&] { ++audioSettingsOpens; };
    }
    REQUIRE (processor.dispatchUiEngineCommand ("channels", { 8 }));
    const std::array<const char*, 8> names { "Kick", "Snare", "Hat", "Open hat", "Clap", "Rim", "Tom", "Perc" };
    for (int channel = 0; channel < 8; ++channel)
    {
        REQUIRE (processor.dispatchUiEngineCommand ("channel_label", { channel + 1, names[static_cast<size_t> (channel)] }));
        for (int step = 0; step < 16; step += channel % 4 + 2)
            REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, channel + 1, step + 1, 1, 80 + step * 2, 100, 1, 1, 1 }));
    }
    NativeEditor editor (processor);
    editor.setSize (Layout::fullWidth, Layout::fullHeight);
    editor.setVisible (true);
    if (standalone)
        editor.zenModeChanged = [&editor] (bool enabled) { editor.setZenMode (enabled); };
    editor.setZenMode (true);
    editor.controller.selected.set (0);
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 100, 100, 1, 1, 1 }));
    editor.controller.refresh();
    editor.sync();
    constexpr float tabletScale = 744.0f / Layout::fullHeight;
    const auto touchSized = [] (juce::Rectangle<int> bounds) {
        CHECK (static_cast<float> (bounds.getWidth()) * tabletScale >= 44.0f);
        CHECK (static_cast<float> (bounds.getHeight()) * tabletScale >= 44.0f);
    };
    std::vector<juce::Component*> toolbar;
    std::vector<juce::Component*> footer;
    juce::Component* swingEye = nullptr;
    ValueControl* swingTarget = nullptr;
    juce::TextButton* settingsButton = nullptr;
    for (auto* child : editor.getChildren())
    {
        if (child->getTitle() == "Settings")
            settingsButton = dynamic_cast<juce::TextButton*> (child);
        if (child->getTitle() == "Shift/Option shortcuts" || child->getTitle() == "Channel colors"
            || child->getTitle() == "Show values" || child->getTitle() == "Reverse row order")
            CHECK_FALSE (child->isVisible());
        if (!child->isVisible()) continue;
        if (child->getTitle() == "Show swing on grid")
            swingEye = child;
        if (child->getTitle() == "Swing target")
            swingTarget = dynamic_cast<ValueControl*> (child);
        if (child->getTitle() == "Ableton Link settings")
        {
            auto* link = dynamic_cast<juce::TextButton*> (child);
            REQUIRE (link != nullptr);
            CHECK (link->getButtonText() == "Link");
            CHECK (link->getWidth() == Layout::touchTarget);
        }
        CHECK (editor.getLocalBounds().contains (child->getBounds()));
        if (dynamic_cast<juce::TextButton*> (child) != nullptr || dynamic_cast<ValueControl*> (child) != nullptr)
        {
            if (static_cast<bool> (child->getProperties()["insetFooterArtwork"]))
            {
                footer.push_back (child);
                touchSized (child->getBounds());
                CHECK (child->getHeight() == Layout::touchTarget);
                CHECK (child->getY() == editor.getHeight() - Layout::footerHeight + Layout::footerPadding);
                CHECK (child->getBottom() == editor.getHeight() - Layout::footerPadding);
                CHECK (editor.getComponentAt (child->getBounds().getTopLeft() + juce::Point<int> (1, 1)) == child);
            }
            else
                touchSized (child->getBounds());
            if (child->getBottom() <= juce::roundToInt (editor.layout.gridY()))
                toolbar.push_back (child);
        }
    }
    REQUIRE (settingsButton != nullptr);
    CHECK (std::find (footer.begin(), footer.end(), settingsButton) != footer.end());
    const auto findSettingsItem = [] (const juce::String& title) -> juce::AccessibilityHandler*
    {
        const auto find = [&title] (auto&& self, juce::Component& component) -> juce::AccessibilityHandler*
        {
            if (auto* button = dynamic_cast<juce::TextButton*> (&component); button != nullptr && button->getButtonText() == title)
                return button->getAccessibilityHandler();
            for (auto* child : component.getChildren())
                if (auto* handler = self (self, *child))
                    return handler;
            return nullptr;
        };
        auto* popup = juce::Component::getCurrentlyModalComponent();
        return popup != nullptr ? find (find, *popup) : nullptr;
    };
    // JUCE popup windows require a display, which is absent in headless test runs.
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
    {
        settingsButton->onClick();
        auto* popup = juce::Component::getCurrentlyModalComponent();
        REQUIRE (popup != nullptr);
        CHECK (popup->getParentComponent() == &editor);
        CHECK (popup->getBottom() <= settingsButton->getY());
        CHECK (findSettingsItem ("Footer tooltips") != nullptr);
        CHECK (findSettingsItem ("Reverse row order") != nullptr);
        auto* numbers = findSettingsItem ("Show values");
        REQUIRE (numbers != nullptr);
        auto* colors = findSettingsItem ("Channel colors");
        REQUIRE (colors != nullptr);
        auto& numberRow = numbers->getComponent();
        const auto rowBounds = numberRow.getLocalBounds();
        const auto backgroundPoint = juce::Point<int> (rowBounds.getRight() - 16, 8);
        const auto restingRow = numberRow.createComponentSnapshot (rowBounds);
        numberRow.grabKeyboardFocus();
        const auto focusedRow = numberRow.createComponentSnapshot (rowBounds);
        CHECK (focusedRow.getPixelAt (backgroundPoint.x, backgroundPoint.y)
               == restingRow.getPixelAt (backgroundPoint.x, backgroundPoint.y));
        const bool initialColors = editor.controller.drumColors;
        const bool initialNumbers = !editor.controller.graphicalCellValues;
        CHECK (colors->getCurrentState().isChecked() == initialColors);
        CHECK (numbers->getCurrentState().isChecked() == initialNumbers);
        const auto clickToggle = [popup] (juce::AccessibilityHandler& row)
        {
            row.getActions().invoke (juce::AccessibilityActionType::press);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
            REQUIRE (juce::Component::getCurrentlyModalComponent() == popup);
        };
        clickToggle (*numbers);
        const auto toggledRow = numberRow.createComponentSnapshot (rowBounds);
        CHECK (toggledRow.getPixelAt (backgroundPoint.x, backgroundPoint.y)
               == restingRow.getPixelAt (backgroundPoint.x, backgroundPoint.y));
        CHECK (toggledRow.getPixelAt (22, rowBounds.getCentreY())
               != restingRow.getPixelAt (22, rowBounds.getCentreY()));
        clickToggle (*colors);
        CHECK (editor.controller.graphicalCellValues == initialNumbers);
        CHECK (editor.controller.drumColors != initialColors);
        CHECK (numbers->getCurrentState().isChecked() != initialNumbers);
        CHECK (colors->getCurrentState().isChecked() != initialColors);
        clickToggle (*numbers);
        clickToggle (*colors);
        auto* shortcuts = findSettingsItem ("Shift/Option shortcuts");
        REQUIRE (shortcuts != nullptr);
        CHECK (shortcuts->getCurrentState().isChecked() == editor.controller.keyboardModifiersEnabled);
        CHECK (findSettingsItem ("Dim text") == nullptr);
        auto* zenItem = findSettingsItem ("Zen mode");
        CHECK ((zenItem != nullptr) == standalone);
        if (zenItem != nullptr)
            CHECK (zenItem->getCurrentState().isChecked());
        auto* audioMenuItem = findSettingsItem (juce::String::fromUTF8 (u8"Audio/MIDI Settings…"));
        CHECK ((audioMenuItem != nullptr) == standalone);
        if (audioMenuItem != nullptr)
            audioMenuItem->getActions().invoke (juce::AccessibilityActionType::press);
        else
            CHECK (popup->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
        CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
        CHECK (audioSettingsOpens == (standalone ? 1 : 0));
    }
    REQUIRE (footer.size() == (standalone ? 4 : 1));
    std::sort (footer.begin(), footer.end(), [] (auto* a, auto* b) { return a->getX() < b->getX(); });
    CHECK (footerArtworkBounds (footer.back()->getBounds().toFloat()).toNearestInt().getRight() == editor.getWidth() - Layout::outerGutter);
    for (size_t i = 1; i < footer.size(); ++i)
        CHECK (footer[i]->getX() - footer[i - 1]->getRight() == Layout::footerGap);
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds());
    for (auto* child : footer)
    {
        const auto bounds = child->getBounds();
        const auto x = bounds.getCentreX();
        CHECK (image.getPixelAt (x, bounds.getY() + 2) == Palette::app);
        CHECK (image.getPixelAt (x, bounds.getY() + 10) != Palette::app);
        CHECK (image.getPixelAt (x, bounds.getBottom() - 3) == Palette::app);
        CHECK (image.getPixelAt (bounds.getX() + 4, bounds.getCentreY()) == Palette::app);
        CHECK (image.getPixelAt (bounds.getX() + 7, bounds.getCentreY()) != Palette::app);
        CHECK (image.getPixelAt (bounds.getRight() - 5, bounds.getCentreY()) == Palette::app);
        CHECK (image.getPixelAt (bounds.getRight() - 8, bounds.getCentreY()) != Palette::app);
    }
    REQUIRE (swingEye != nullptr);
    REQUIRE (swingTarget != nullptr);
    const auto swingValue = swingTarget->getBounds().withTrimmedTop (18 + swingTarget->captionGap);
    CHECK_FALSE (swingEye->getBounds().intersects (swingValue));
    CHECK (editor.getComponentAt (swingEye->getBounds().getBottomLeft().translated (1, -1)) == swingEye);
    CHECK (editor.getComponentAt (swingValue.getCentre()) == swingTarget);
    for (size_t i = 0; i < toolbar.size(); ++i)
        for (size_t j = i + 1; j < toolbar.size(); ++j)
        {
            INFO (toolbar[i]->getTitle() << " / " << toolbar[j]->getTitle());
            // Segmented controls share a one-point border.
            const auto overlap = toolbar[i]->getBounds().getIntersection (toolbar[j]->getBounds());
            if ((toolbar[i] == swingEye && toolbar[j] == swingTarget) || (toolbar[i] == swingTarget && toolbar[j] == swingEye))
                continue;
            CHECK ((overlap.isEmpty() || overlap.getWidth() <= 1));
        }
    auto* grid = [&] {
        for (auto* child : editor.getChildren())
            if (auto* result = dynamic_cast<PatternGrid*> (child)) return result;
        return static_cast<PatternGrid*> (nullptr);
    }();
    REQUIRE (grid != nullptr);
    const auto cell = grid->layout.cell ({ 0, 0 });
    CHECK (cell.getWidth() * tabletScale >= 44.0f);
    CHECK (cell.getHeight() * tabletScale >= 44.0f);
    CHECK (grid->layout.cell ({ 7, 15 }).getBottom() <= grid->getHeight());
    for (auto* child : grid->getChildren())
        if (child->isVisible() && (dynamic_cast<juce::TextButton*> (child) != nullptr || dynamic_cast<ValueControl*> (child) != nullptr))
        {
            INFO (child->getTitle());
            CHECK (child->getWidth() >= 40);
            CHECK (static_cast<float> (child->getHeight()) * tabletScale >= 44.0f);
            CHECK (grid->getLocalBounds().contains (child->getBounds()));
        }
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        auto stream = juce::File (directory).getChildFile (standalone ? "unified-standalone.png" : "unified-plugin.png").createOutputStream();
        REQUIRE (stream != nullptr);
        REQUIRE (stream->setPosition (0));
        REQUIRE (stream->truncate().wasOk());
        REQUIRE (juce::PNGImageFormat().writeImageToStream (editor.createComponentSnapshot (editor.getLocalBounds(), true, tabletScale), *stream));
    }
}

TEST_CASE ("Row controls stay inline and handle gestures retain undoable channel edits", "[native-ui][unified-layout]")
{
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 3, 1, 1, 100, 100, 1, 1, 1 }));
    EditorController controller (processor);
    PatternGrid grid (controller);
    grid.setSize (Layout::fullWidth, juce::roundToInt (grid.layout.gridHeight()));
    grid.sync();
    juce::TextButton* clear = nullptr;
    RowPlaybackButton* direction = nullptr;
    RowPlaybackButton* pingPong = nullptr;
    RowPlaybackButton* power = nullptr;
    for (auto* child : grid.getChildren())
    {
        if (child->getTitle() == "clear channel 3") clear = dynamic_cast<juce::TextButton*> (child);
        if (child->getTitle() == "forward channel 3") direction = dynamic_cast<RowPlaybackButton*> (child);
        if (child->getTitle() == "ping channel 3") pingPong = dynamic_cast<RowPlaybackButton*> (child);
        if (child->getTitle() == "power channel 3") power = dynamic_cast<RowPlaybackButton*> (child);
    }
    REQUIRE (clear != nullptr);
    CHECK (clear->getWidth() == 42);
    CHECK (clear->getRight() == grid.getWidth());
    CHECK (static_cast<float> (clear->getX()) >= grid.loopHandleBounds (2, true).getRight());
    clear->onClick();
    CHECK (controller.cell ({ 2, 0 }).enabled);
    CHECK (static_cast<bool> (clear->getProperties()["clearPrompt"]));
    clear->onClick();
    CHECK_FALSE (controller.cell ({ 2, 0 }).enabled);
    controller.undo();
    CHECK (controller.cell ({ 2, 0 }).enabled);
    const auto handle = grid.loopHandleBounds (2, true).getCentre();
    grid.mouseMove (gridEvent (grid, handle, handle, 0));
    CHECK (grid.getMouseCursor() == horizontalResizeCursor());
    grid.mouseMove (gridEvent (grid, grid.layout.cell ({ 2, 0 }).getCentre(), handle, 0));
    CHECK (grid.getMouseCursor() == juce::MouseCursor::NormalCursor);
    const auto destination = handle.translated (-8 * grid.layout.cellWidth(), 0);
    const auto event = [&] (juce::Point<float> position, int clicks = 1) {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent { juce::Desktop::getInstance().getMainMouseSource(), position,
            juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, &grid, &grid, now, handle, now, clicks, position != handle };
    };
    for (int click = 1; click <= 6; ++click)
    {
        grid.mouseDown (event (handle, click));
        grid.mouseUp (event (handle, click));
        CHECK (controller.cell ({ 2, click / 2 }).enabled);
    }
    for (int step = 2; step >= 0; --step)
    {
        controller.undo();
        CHECK (controller.cell ({ 2, step }).enabled);
    }
    grid.mouseDown (event (handle));
    grid.mouseDrag (event (destination));
    grid.mouseUp (event (destination));
    CHECK (controller.loop (2).loopLength == 8);
    CHECK (controller.loop (0).loopLength == 16);
    controller.undo();
    CHECK (controller.loop (2).loopLength == 16);
    constexpr float tabletScale = 744.0f / Layout::fullHeight;
    for (const bool end : { false, true })
    {
        const auto target = grid.loopHandleBounds (2, end);
        if (end)
        {
            CHECK (target.getRight() == Catch::Approx (clear->getX()));
            CHECK (target.getX() - grid.layout.cell ({ 2, 15 }).getRight() == Catch::Approx (Layout::rowVelocityGutter));
        }
        else
            CHECK (grid.layout.cell ({ 2, 0 }).getX() - target.getRight() == Catch::Approx (Layout::rowVelocityGutter));
        CHECK (target.getHeight() * tabletScale >= 44.0f);
        CHECK (grid.getLocalBounds().toFloat().contains (target));
        for (int step = 0; step < 16; ++step)
            CHECK_FALSE (target.contains (grid.layout.cell ({ 2, step }).getCentre()));
    }
    for (const bool touch : { false, true })
    {
        grid.layout.touchRows = touch;
        grid.resized();
        const auto& note = grid.rowNoteInput (2);
        const auto noteFace = note.valueFaceBounds();
        CHECK (noteFace.getHeight() == Catch::Approx (42.0f));
        CHECK (noteFace.getHeight() < note.getHeight());
        for (auto* button : { direction, pingPong, power })
        {
            REQUIRE (button != nullptr);
            CHECK (button->isVisible());
            const auto target = button->getLocalBounds();
            const auto face = rowControlFace (target.toFloat());
            CHECK_FALSE (button->getMouseClickGrabsKeyboardFocus());
            CHECK (button->getWantsKeyboardFocus());
            CHECK (face.getCentre() == target.toFloat().getCentre());
            CHECK (face.getWidth() < target.getWidth());
            CHECK (face.getHeight() == Catch::Approx (noteFace.getHeight()));
            CHECK (face.getCentreY() + button->getY() == Catch::Approx (noteFace.getCentreY() + note.getY()));
            CHECK (button->hitTest (1, target.getCentreY()));
        }
    }
    CHECK_FALSE (direction->getToggleState());
    CHECK_FALSE (pingPong->getToggleState());
    CHECK (power->getToggleState());
    direction->onClick();
    pingPong->onClick();
    power->onClick();
    grid.sync();
    CHECK (direction->getToggleState());
    CHECK (pingPong->getToggleState());
    CHECK_FALSE (power->getToggleState());
    controller.command ("channel_playback_mode", { 2, "reverse_ping_pong" });
    controller.command ("channel_playback_mode", { 1, "reverse" });
    const auto swipe = [&] (RowPlaybackButton& button)
    {
        const auto from = button.getLocalBounds().getCentre().toFloat();
        const auto to = button.getLocalPoint (&grid, grid.layout.cell ({ 0, 0 }).getCentre()).withX (from.x);
        button.mouseDown (gridEvent (button, from, from, juce::ModifierKeys::leftButtonModifier));
        button.mouseDrag (gridEvent (button, to, from, juce::ModifierKeys::leftButtonModifier, true));
        button.mouseUp (gridEvent (button, to, from, 0, true));
    };
    swipe (*direction);
    CHECK (controller.state.playback.channels[0].playbackMode == PlaybackMode::normal);
    CHECK (controller.state.playback.channels[1].playbackMode == PlaybackMode::ping_pong);
    CHECK (controller.state.playback.channels[2].playbackMode == PlaybackMode::ping_pong);
    controller.undo();
    CHECK (controller.state.playback.channels[0].playbackMode == PlaybackMode::reverse);
    CHECK (controller.state.playback.channels[1].playbackMode == PlaybackMode::reverse_ping_pong);
    CHECK (controller.state.playback.channels[2].playbackMode == PlaybackMode::reverse_ping_pong);
    swipe (*pingPong);
    for (int ch = 0; ch < 3; ++ch)
        CHECK (controller.state.playback.channels[static_cast<size_t> (ch)].playbackMode == PlaybackMode::reverse);
    const auto bounds = power->getLocalBounds();
    const auto face = rowControlFace (bounds.toFloat());
    const auto renderPower = [&] (bool active)
    {
        power->setToggleState (active, juce::dontSendNotification);
        juce::Image image (juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);
        juce::Graphics graphics (image);
        power->paintButton (graphics, false, false);
        return image;
    };
    const auto off = renderPower (false);
    const auto on = renderPower (true);
    const int edgeX = juce::roundToInt (face.getX()) + 1;
    CHECK (on.getPixelAt (edgeX, bounds.getCentreY()) == off.getPixelAt (edgeX, bounds.getCentreY()));
    const auto nearStroke = bounds.getCentre() + juce::Point<int> (2, -5);
    CHECK (on.getPixelAt (nearStroke.x, nearStroke.y).getBrightness() > off.getPixelAt (nearStroke.x, nearStroke.y).getBrightness() + 0.03f);
}

TEST_CASE ("The XY randomizer repeats coordinates and preserves the drum foundation", "[native-ui][super-randomizer]")
{
    for (float variation : { 0.0f, 0.25f, 0.51f, 0.7f, 0.8f, 0.9f, 1.0f })
    {
        int active = 0, withProbability = 0, withCycle = 0;
        for (int step = 0; step < 32; ++step)
        {
            for (int channel = 0; channel < 8; ++channel)
            {
                const auto sparse = generatedDrumCell (channel, step, 0.1f, variation);
                const auto dense = generatedDrumCell (channel, step, 0.9f, variation);
                CHECK (dense == generatedDrumCell (channel, step, 0.9f, variation));
                CHECK ((!sparse.enabled || dense.enabled));
                CHECK (dense.velocity >= 1);
                CHECK (dense.velocity <= 127);
                active += dense.enabled;
                withProbability += dense.probability < 100;
                withCycle += dense.cycle > 1;
                CHECK (dense.probability >= 55);
                CHECK (dense.probability <= 100);
                if (!dense.enabled)
                {
                    CHECK (dense.probability == 100);
                    CHECK (dense.cycle == 1);
                }
                if (dense.cycle > 1)
                {
                    CHECK (dense.cycle <= 6);
                    CHECK (dense.cycleMask > 0);
                    CHECK (dense.cycleMask < cycleMaskForLength (dense.cycle));
                }
                if (variation <= 0.8f)
                    CHECK (dense.nudge == 0);
                CHECK (std::abs (dense.nudge) <= 45);
            }
        }
        if (variation <= 0.7f)
        {
            CHECK (withProbability == 0);
            CHECK (withCycle == 0);
        }
        else if (variation < 1.0f)
        {
            CHECK (withProbability > 0);
            CHECK (withProbability < active);
            CHECK (withCycle > 0);
            CHECK (withCycle < active);
        }
        else
        {
            CHECK (withProbability == active);
            CHECK (withCycle == active);
        }
        if (variation <= 0.8f)
        {
            CHECK (generatedDrumCell (0, 0, 0, variation).enabled);
            CHECK (generatedDrumCell (1, 4, 0, variation).enabled);
            CHECK (generatedDrumCell (1, 12, 0, variation).enabled);
        }
    }
    int differences = 0;
    for (int channel = 0; channel < 8; ++channel)
        for (int step = 0; step < 16; ++step)
            differences += generatedDrumCell (channel, step, 0.6f, 0).enabled != generatedDrumCell (channel, step, 0.6f, 1).enabled;
    CHECK (differences > 8);
    int displaced = 0;
    for (int step = 0; step < 32; ++step)
    {
        const auto hit = generatedDrumCell (0, step, 0.9f, 1.0f);
        displaced += hit.enabled && std::abs (hit.nudge) >= 15;
    }
    CHECK (displaced > 4);
    CHECK ((!generatedDrumCell (0, 0, 0, 1.0f).enabled
            || !generatedDrumCell (1, 4, 0, 1.0f).enabled
            || !generatedDrumCell (1, 12, 0, 1.0f).enabled));
}

TEST_CASE ("The XY randomizer edits one pattern with undo and cancels stale drags", "[native-ui][super-randomizer]")
{
    PluginProcessor processor;
    EditorController controller (processor);
    controller.command ("source_steps", { 1, 64 });
    PatternXYPad pad (controller);
    pad.setSize (400, 400);
    const auto event = [&pad] (float x, float y)
    {
        const auto now = juce::Time::getCurrentTime();
        const juce::Point<float> point { x, y };
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                                &pad, &pad, now, point, now, 1, true);
    };
    // Full documents and playback snapshots grow with grid capacity. Keep the
    // saved states off the stack so the test can run with the normal stack limit.
    const auto original = std::make_unique<DocumentState>();
    const auto generated = std::make_unique<DocumentState>();
    const auto current = std::make_unique<DocumentState>();
    const auto uiState = std::make_unique<PluginProcessor::UiState>();
    processor.copyEditorDocument (*original);
    int notifications = 0;
    controller.changed = [&] { ++notifications; };
    pad.mouseDown (event (100, 300));
    processor.copyNativeUiState (*uiState);
    const auto firstPosition = uiState->playback.sources[0];
    const int firstNotifications = notifications;
    REQUIRE (controller.editing());
    pad.mouseDrag (event (300, 100));
    // Both the engine and UI receive the new pattern before mouse-up commits undo.
    processor.copyNativeUiState (*uiState);
    const auto& live = uiState->playback;
    CHECK (live.sources[0] != firstPosition);
    CHECK (live.stepCount == 64);
    CHECK (live.sources[0][0][56].enabled);
    CHECK (live.sources[0][0][56] != firstPosition[0][56]);
    CHECK (live.sources[0] == controller.state.playback.sources[0]);
    for (const auto& row : live.sources[0])
        for (const auto& cell : row)
            CHECK (cell.roll == 1);
    CHECK (live.sources[1] == original->sources[1]);
    CHECK (live.generationMode == GenerationMode::staticSource);
    CHECK (notifications > firstNotifications);
    CHECK (controller.editing());
    const auto chaoticEdge = event (300, 26);
    pad.mouseDrag (chaoticEdge);
    processor.copyNativeUiState (*uiState);
    int gatedSteps = 0;
    for (const auto& row : uiState->playback.sources[0])
        for (const auto& cell : row)
            if (cell.enabled)
            {
                CHECK (cell.probability < 100);
                CHECK (cell.cycle > 1);
                ++gatedSteps;
            }
    CHECK (gatedSteps > 0);
    processor.copyEditorDocument (*current);
    pad.mouseUp (chaoticEdge);
    processor.copyEditorDocument (*generated);
    CHECK (*generated == *current);
    CHECK (*generated != *original);
    CHECK (controller.canUndo());
    controller.undo();
    processor.copyEditorDocument (*current);
    CHECK (*current == *original);
    controller.redo();
    processor.copyEditorDocument (*current);
    CHECK (*current == *generated);
    pad.mouseDown (event (100, 300));
    pad.mouseDrag (chaoticEdge);
    pad.mouseUp (chaoticEdge);
    processor.copyEditorDocument (*current);
    CHECK (*current == *generated);
    pad.mouseDown (event (100, 300));
    pad.cancelGesture();
    processor.copyEditorDocument (*current);
    CHECK (*current == *generated);
    pad.mouseDown (event (100, 300));
    controller.selectSource (1);
    const auto switched = std::make_unique<DocumentState>();
    processor.copyEditorDocument (*switched);
    pad.mouseDrag (event (350, 50));
    pad.mouseUp (event (350, 50));
    processor.copyEditorDocument (*current);
    CHECK (*current == *switched);
}

TEST_CASE ("XY seeds add core triplets below the chaotic roll range", "[native-ui][super-randomizer]")
{
    const bool extended = GENERATE (false, true);
    const float variation = GENERATE (0.0f, 0.16f, 0.44f, 1.0f);
    const int length = extended ? 64 : 16;
    const int halfBeatSteps = extended ? 4 : 2;
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("source_steps", { 1, length }));
    REQUIRE (processor.dispatchUiEngineCommand ("source_rate", { 1, extended ? "32n" : "16n" }));
    REQUIRE (processor.dispatchUiEngineCommand ("channel_loop_length", { 1, length - 4, 3 }));
    REQUIRE (processor.dispatchUiEngineCommand ("cell", { 1, 1, 1, 1, 88 }));
    EditorController controller (processor);
    PatternXYPad pad (controller);
    pad.setSize (400, 400);
    const juce::Point<float> position { 300, 374.0f - juce::jmax (0.5f, variation * 348.0f) };
    const auto press = gridEvent (pad, position, position, juce::ModifierKeys::leftButtonModifier);
    pad.mouseDown (press);
    pad.mouseUp (press);
    const auto outsideLoop = controller.cell ({ 0, 0 });
    REQUIRE (outsideLoop.enabled);
    CHECK (outsideLoop.velocity == 88);

    for (int seed = 0; seed < generatorSeedCount; ++seed)
    {
        CAPTURE (seed, length, variation);
        pad.selectSeed (seed);
        const auto& pattern = controller.state.playback.sources[0];
        int rolls = 0, spans = 0, triplets = 0;
        for (int channel = 0; channel < controller.state.playback.channelCount; ++channel)
        {
            const auto loop = controller.loop (channel);
            const auto& row = pattern[static_cast<size_t> (channel)];
            for (int step = loop.loopStart; step < loop.loopStart + loop.loopLength; ++step)
            {
                const auto& cell = row[static_cast<size_t> (step)];
                if (!cell.enabled)
                    continue;
                rolls += cell.roll > 1 && cell.span == 1;
                spans += cell.span > 1;
                if (cell.span > 1)
                {
                    CHECK (cell.roll > 1);
                    CHECK (rollSpanFits (row, loop, step, cell.span));
                }
                if (cell.roll == 3)
                {
                    ++triplets;
                    CHECK ((cell.span == halfBeatSteps || cell.span == 2 * halfBeatSteps || cell.span == 4 * halfBeatSteps));
                    if (variation < 0.8f)
                    {
                        CHECK (channel == 2);
                        CHECK (cell.span == (seed == 1 ? halfBeatSteps : 2 * halfBeatSteps));
                    }
                }
            }
        }
        if (variation > 0.8f)
        {
            CHECK (rolls > 0);
            CHECK (spans > 0);
            CHECK ((triplets > 0) == (seed == 1 || seed == 3));
        }
        else
        {
            const int phrases = length / (8 * halfBeatSteps);
            const int expected = (seed == 1 || seed == 3) && variation > 0.1f ? phrases * (variation > 0.38f ? 2 : 1) : 0;
            CHECK (triplets == expected);
            CHECK (spans == triplets);
            CHECK (rolls == 0);
            CHECK (controller.cell ({ 1, 4 }).enabled);
            CHECK (controller.cell ({ 1, 12 }).enabled);
        }
        CHECK (controller.cell ({ 0, 0 }) == outsideLoop);
    }
}

TEST_CASE ("Generate toggles the XY generator", "[native-ui][generator][render]")
{
    PluginProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* view = dynamic_cast<NativeEditor*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    PreviewPanel* inspector = nullptr;
    juce::Viewport* viewport = nullptr;
    juce::TextButton* generate = nullptr;
    juce::Component* pads = nullptr;
    for (auto* child : view->getChildren())
    {
        if (auto* candidate = dynamic_cast<PreviewPanel*> (child)) inspector = candidate;
        if (auto* candidate = dynamic_cast<juce::Viewport*> (child)) viewport = candidate;
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button && button->getButtonText() == "Generate")
            generate = button;
        if (child->getTitle() == "Show pads")
            pads = child;
    }
    REQUIRE (inspector != nullptr);
    REQUIRE (viewport != nullptr);
    REQUIRE (generate != nullptr);
    REQUIRE (pads != nullptr);
    CHECK (generate->isVisible());
    CHECK (generate->getRight() <= pads->getX());
    generate->onClick();
    REQUIRE (viewport->isVisible());
    CHECK (generate->getToggleState());
    PatternXYPad* pad = nullptr;
    for (auto* child : viewport->getViewedComponent()->getChildren())
    {
        if (auto* candidate = dynamic_cast<PatternXYPad*> (child)) pad = candidate;
    }
    REQUIRE (pad != nullptr);
    CHECK (pad->isVisible());
    CHECK (pad->getY() < Layout::touchTarget);
    CHECK (pad->getWidth() >= 300);
    const auto padBounds = pad->getLocalBounds().toFloat().reduced (10, 26);
    const float padSide = juce::jmin (padBounds.getWidth(), padBounds.getHeight());
    const auto position = padBounds.getCentre().translated (padSide * 0.15f, -padSide * 0.45f);
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent drag (juce::Desktop::getInstance().getMainMouseSource(), position,
                                juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                                pad, pad, now, position, now, 1, false);
    pad->mouseDown (drag);
    pad->mouseUp (drag);
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KSH_NATIVE_CAPTURE_DIR", "");
    if (directory.isNotEmpty())
    {
        juce::File folder (directory);
        REQUIRE (folder.createDirectory().wasOk());
        auto output = folder.getChildFile ("top-secret-randomizer.png").createOutputStream();
        REQUIRE (output != nullptr);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), *output));
    }
    generate->onClick();
    CHECK_FALSE (viewport->isVisible());
}

TEST_CASE ("Feel inputs cap edits at 99 percent while preserving existing full values", "[native-ui][feel]")
{
    juce::TemporaryFile settings (".settings");
    juce::PropertiesFile::Options options;
    options.millisecondsBeforeSaving = -1;
    const auto preferences = std::make_shared<juce::PropertiesFile> (settings.getFile(), options);
    PluginProcessor processor;
    const std::array controls { std::pair { "Amount", "swing" }, std::pair { "Velocity", "velocity_humanize" }, std::pair { "Timing", "timing_humanize" } };
    for (const auto& [caption, parameter] : controls)
        REQUIRE (processor.dispatchUiEngineCommand (parameter, { 100 }));

    NativeEditor editor (processor, preferences);
    for (const auto& [caption, parameter] : controls)
    {
        auto* control = findValueControl (editor, caption);
        REQUIRE (control != nullptr);
        auto handler = control->createAccessibilityHandler();
        auto* value = handler->getValueInterface();
        REQUIRE (value != nullptr);
        CHECK (value->getCurrentValue() == Catch::Approx (99.0));
        auto* raw = processor.getValueTreeState().getRawParameterValue (parameter);
        REQUIRE (raw != nullptr);
        CHECK (raw->load() == Catch::Approx (100.0f));
        value->setValue (100.0);
        CHECK (value->getCurrentValue() == Catch::Approx (99.0));
        CHECK (raw->load() == Catch::Approx (99.0f));
    }
}

TEST_CASE ("MPC swing input converts decimal percentages and preserves undo", "[native-ui][swing-scale]")
{
    juce::TemporaryFile settings (".settings");
    juce::PropertiesFile::Options options;
    options.millisecondsBeforeSaving = -1;
    const auto preferences = std::make_shared<juce::PropertiesFile> (settings.getFile(), options);
    preferences->setValue ("mpcSwingScale", true);
    PluginProcessor processor;
    REQUIRE (processor.dispatchUiEngineCommand ("swing", { 28 }));
    NativeEditor editor (processor, preferences);
    auto* amount = findValueControl (editor, "Amount");
    REQUIRE (amount != nullptr);
    CHECK (amount->valueText (amount->value) == "57");
    CHECK (amount->valueText (0) == "50");
    CHECK (amount->valueText (100) == "75");

    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), { 4, 4 },
        juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
        amount, amount, now, { 4, 4 }, now, 2, false);
    editor.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    editor.setVisible (true);
    amount->mouseDoubleClick (event);
    auto* input = dynamic_cast<juce::TextEditor*> (amount->getChildComponent (0));
    REQUIRE (input != nullptr);
    CHECK (input->getText() == "57");
    input->setText ("57.25");
    input->onReturnKey();
    CHECK (editor.controller.state.playback.swing == 29);
    CHECK (amount->valueText (amount->value) == "57.25");
    editor.controller.undo();
    CHECK (editor.controller.state.playback.swing == 28);

    auto handler = amount->createAccessibilityHandler();
    auto* accessible = handler->getValueInterface();
    REQUIRE (accessible != nullptr);
    CHECK (accessible->getCurrentValue() == Catch::Approx (57.0));
    accessible->setValue (60.0);
    CHECK (editor.controller.state.playback.swing == 40);
    editor.controller.undo();
    CHECK (editor.controller.state.playback.swing == 28);
}
