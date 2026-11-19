#include "KshPreviewPanel.h"
#include "KshLayout.h"
namespace ksh::ui
{
    namespace
    {
        constexpr auto lockHeading = "Lock Channel to Pattern";

        struct RandomLabLayout
        {
            RandomLabLayout (juce::Rectangle<int> bounds, int rightInset)
            {
                auto content = bounds.reduced (0, 12).withTrimmedLeft (Layout::outerGutter).withTrimmedRight (rightInset);
                header = content.removeFromTop (Layout::touchTarget);
                close = header.removeFromRight (140);
                content.removeFromTop (12);
                controls = content.removeFromRight (468);
                preview = content.withTrimmedRight (16);
                auto fields = controls.reduced (22, 20);
                auto modeRow = fields.removeFromTop (20 + Layout::touchTarget);
                refresh = modeRow.removeFromRight (156);
                mode = modeRow.withTrimmedRight (12).withTrimmedTop (20);
                fields.removeFromTop (24);
                locksHeading = fields.removeFromTop (24);
                fields.removeFromTop (8);
                help = fields.removeFromBottom (80);
                fields.removeFromBottom (12);
                lockHeight = juce::jlimit (68, 108, (fields.getHeight() - 10) / 2);
                lockFields = fields.removeFromTop (lockHeight * 2 + 10);
                help.setY (lockFields.getBottom() + 12);
            }

            juce::Rectangle<int> lockBounds (int channel) const
            {
                const int stride = (lockFields.getWidth() + 8) / 4;
                return { lockFields.getX() + channel % 4 * stride, lockFields.getY() + channel / 4 * (lockHeight + 10), stride - 8, lockHeight };
            }

            juce::Rectangle<int> header, close, preview, controls, mode, refresh, locksHeading, lockFields, help;
            int lockHeight = 0;
        };

        const juce::Rectangle<float> wordmarkBounds { 24, 34.68f, 242.88f, 95.04f };
        const juce::Rectangle<float> makerBounds { 24, 19, 170, 20 };
        juce::AffineTransform wordmarkTransform (juce::Rectangle<float> bounds, float artworkScale)
        {
            constexpr float margin = 16.0f;
            constexpr float wordmarkScale = 0.9f;
            const float scale = wordmarkScale * artworkScale * juce::jmax (0.0f, bounds.getWidth() - 2.0f * margin) / wordmarkBounds.getWidth();
            const auto brandingBounds = wordmarkBounds.getUnion (makerBounds);
            return juce::AffineTransform::scale (scale).translated (bounds.getCentreX() - brandingBounds.getCentreX() * scale,
                                                                    bounds.getCentreY() - brandingBounds.getCentreY() * scale);
        }
        juce::Rectangle<float> brandingBoundsForInspector (juce::Rectangle<int> inspector)
        {
            return inspector.toFloat().withX (0).withWidth (static_cast<float> (inspector.getX()));
        }

        struct PhoneGeneratorLayout
        {
            explicit PhoneGeneratorLayout (juce::Rectangle<int> bounds)
            {
                controls = bounds.removeFromRight (bounds.getWidth() * 42 / 100);
                preview = bounds.withTrimmedRight (12);
            }

            juce::Rectangle<int> preview, controls;
        };
    }

    AccentWordmark::AccentWordmark (bool showRandomizer, float scale)
        : artworkScale (scale)
    {
        setInterceptsMouseClicks (false, true);
        addAndMakeVisible (backdrop);
        backdrop.setTitle ("Background tone");
        backdrop.setTooltip ("Drag up or down for brightness, left or right for tint. Double-click to reset.");
        backdrop.setWantsKeyboardFocus (false);
        backdrop.setMouseClickGrabsKeyboardFocus (false);
        backdrop.setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
        backdrop.pressed = [this] (const juce::MouseEvent& event)
        {
            startingTone.reset();
            draggingBackdrop = false;
            if (event.mods.isLeftButtonDown() && !event.mods.isPopupMenu())
            {
                startingTone = Palette::backgroundTone();
                if (onThemeGesture)
                    onThemeGesture (true);
            }
        };
        backdrop.dragged = [this] (const juce::MouseEvent& event)
        {
            if (startingTone && (draggingBackdrop || event.getDistanceFromDragStart() >= 3) && onBackgroundToneEdited)
            {
                constexpr float sensitivity = 0.7f;
                const int brightness = -juce::roundToInt (static_cast<float> (event.getDistanceFromDragStartY()) * sensitivity);
                const int tint = juce::roundToInt (static_cast<float> (event.getDistanceFromDragStartX()) * sensitivity);
                draggingBackdrop = true;
                onBackgroundToneEdited ({ startingTone->brightness + brightness, startingTone->tint + tint });
            }
        };
        backdrop.released = [this] (const juce::MouseEvent&)
        {
            const bool pressed = std::exchange (startingTone, std::nullopt).has_value();
            draggingBackdrop = false;
            if (pressed && onThemeGesture)
                onThemeGesture (false);
        };
        backdrop.onDoubleClick = [this]
        {
            startingTone.reset();
            draggingBackdrop = false;
            if (onBackgroundToneEdited)
                onBackgroundToneEdited ({ 0, 0 });
        };
        for (auto* letter : { &orangeLogo, &blueLogo })
        {
            addAndMakeVisible (*letter);
            letter->setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
            letter->onThemeGesture = [this] (bool started) { if (onThemeGesture) onThemeGesture (started); };
        }
        orangeLogo.setTitle ("Orange accent");
        blueLogo.setTitle ("Blue accent");
        for (auto* letter : { &orangeLogo, &blueLogo })
            letter->setTooltip ("Drag left or right for hue, up or down for luminosity. Double-click to reset.");
        orangeLogo.onClick = [this] { if (onAccentSelected) onAccentSelected (false); };
        blueLogo.onClick = [this] { if (onAccentSelected) onAccentSelected (true); };
        orangeLogo.onColourEdited = [this] (std::optional<juce::Colour> colour) { if (onAccentColourEdited) onAccentColourEdited (false, colour); };
        blueLogo.onColourEdited = [this] (std::optional<juce::Colour> colour) { if (onAccentColourEdited) onAccentColourEdited (true, colour); };
        if (showRandomizer)
        {
            addAndMakeVisible (randomizerLogo);
            randomizerLogo.setTitle ("Cycle randomizer view");
            randomizerLogo.setTooltip ("Cycle through Top Secret Randomizer, Super Secret Randomizer and the grid");
            randomizerLogo.onClick = [this] { if (onRandomizerCycle) onRandomizerCycle(); };
        }
    }

    void AccentWordmark::AccentLogoButton::mouseDown (const juce::MouseEvent& event)
    {
        if (!isEnabled() || !event.mods.isLeftButtonDown() || event.mods.isPopupMenu())
            return;
        startingColour = Palette::accentColour (blueAccent);
        draggingAccent = false;
        if (onThemeGesture)
            onThemeGesture (true);
        LogoButton::mouseDown (event);
    }

    void AccentWordmark::AccentLogoButton::mouseDrag (const juce::MouseEvent& event)
    {
        const int delta = -event.getDistanceFromDragStartY();
        if (startingColour && (draggingAccent || event.getDistanceFromDragStart() >= 3))
        {
            draggingAccent = true;
            setState (buttonNormal);
            const float hue = startingColour->getHue() + static_cast<float> (event.getDistanceFromDragStartX()) / 200.0f;
            const float amount = juce::jlimit (-1.0f, 1.0f, static_cast<float> (delta) / 110.0f);
            const auto lightness = juce::jmap (std::abs (amount), startingColour->getLightness(), amount > 0 ? 0.88f : 0.20f);
            if (onColourEdited)
                onColourEdited (juce::Colour::fromHSL (hue - std::floor (hue), startingColour->getSaturationHSL(), lightness, 1.0f));
        }
        else
            LogoButton::mouseDrag (event);
    }

    void AccentWordmark::AccentLogoButton::mouseUp (const juce::MouseEvent& event)
    {
        const bool pressed = std::exchange (startingColour, std::nullopt).has_value();
        if (!std::exchange (draggingAccent, false))
            LogoButton::mouseUp (event);
        if (pressed && onThemeGesture)
            onThemeGesture (false);
    }

    void AccentWordmark::AccentLogoButton::mouseDoubleClick (const juce::MouseEvent& event)
    {
        if (!isEnabled() || event.mods.isPopupMenu() || event.mouseWasDraggedSinceMouseDown() || !onColourEdited)
            return;
        startingColour.reset();
        draggingAccent = false;
        onColourEdited (std::nullopt);
    }

    void AccentWordmark::setArtworkBounds (juce::Rectangle<float> bounds)
    {
        if (artworkBounds == bounds)
            return;
        artworkBounds = bounds;
        resized();
        repaint();
    }

    void AccentWordmark::resized()
    {
        backdrop.setBounds (getLocalBounds());
        const auto transform = wordmarkTransform (artworkBounds.value_or (getLocalBounds().toFloat()), artworkScale);
        const float unit = wordmarkBounds.getWidth() / 556.632813f;
        orangeLogo.setBounds (wordmarkBounds.withWidth (197.021484f * unit).transformedBy (transform).getSmallestIntegerContainer());
        blueLogo.setBounds (wordmarkBounds.withTrimmedLeft (358.0f * unit).transformedBy (transform).getSmallestIntegerContainer());
        randomizerLogo.setBounds (orangeLogo.getBounds().withX (orangeLogo.getRight()).withRight (blueLogo.getX()));
    }

    void AccentWordmark::paint (juce::Graphics& g)
    {
        paintBranding (g, artworkBounds.value_or (getLocalBounds().toFloat()), artworkScale);
    }

    void AccentWordmark::paintBranding (juce::Graphics& g, juce::Rectangle<float> bounds, float scale)
    {
        const juce::Graphics::ScopedSaveState savedState (g);
        g.addTransform (wordmarkTransform (bounds, scale));
        // Outlined from the app icon so letter shapes and spacing never depend on UI fonts.
        static const auto artwork = juce::Drawable::createFromImageData (BinaryData::KshWordmark_svg, BinaryData::KshWordmark_svgSize);
        text (g, "OFSOUND", makerBounds, 13, Palette::text, true, juce::Justification::centredLeft);
        if (artwork == nullptr)
            return;

        static const auto letters = []
        {
            std::array<juce::DrawableShape*, 2> result {};
            for (auto* child : artwork->getChildren())
                if (auto* shape = dynamic_cast<juce::DrawableShape*> (child))
                {
                    if (shape->getFill().colour == Palette::orangeAccent)
                        result[0] = shape;
                    else if (shape->getFill().colour == Palette::blueAccent)
                        result[1] = shape;
                }
            return result;
        }();
        for (size_t index = 0; index < letters.size(); ++index)
            if (auto* letter = letters[index])
            {
                const auto colour = Palette::accentColour (index == 1);
                if (letter->getFill().colour != colour)
                    letter->setFill (colour);
            }

        // Switch off to draw the original flat SVG with the same placement.
        constexpr bool useRaisedWordmark = true;
        if constexpr (useRaisedWordmark)
        {
            static const auto outline = []
            {
                auto path = artwork->getOutlineAsPath();
                path.applyTransform (juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                         .getTransformToFit (artwork->getDrawableBounds(), wordmarkBounds));
                return path;
            }();
            constexpr float effectIntensity = 1.12f;
            g.setColour (juce::Colours::black.withAlpha (0.25f * effectIntensity));
            g.fillPath (outline, juce::AffineTransform::translation (0, 1.5f));
            artwork->drawWithin (g, wordmarkBounds, juce::RectanglePlacement::centred, 1.0f);

            juce::ColourGradient finish (juce::Colours::white.withAlpha (0.12f * effectIntensity), wordmarkBounds.getTopLeft(),
                                         juce::Colours::black.withAlpha (0.14f * effectIntensity), wordmarkBounds.getBottomLeft(), false);
            finish.addColour (0.42, juce::Colours::transparentBlack);
            g.reduceClipRegion (outline);
            g.setGradientFill (finish);
            g.fillRect (wordmarkBounds);
        }
        else
            artwork->drawWithin (g, wordmarkBounds, juce::RectanglePlacement::centred, 1.0f);
    }
    PreviewPanel::PreviewPanel (EditorController& c, bool randomSectionIn) : controller (c), randomSection (randomSectionIn), cycle (c)
    {
        addMouseListener (&childHoverListener, true);
        if (randomSection)
        {
            addChildComponent (close);
            addChildComponent (xyPad);
            for (size_t i = 0; i < xySeeds.size(); ++i)
            {
                auto& button = xySeeds[i];
                addChildComponent (button);
                button.setButtonText (juce::String (i + 1));
                button.setTitle ("Generator seed " + button.getButtonText());
                button.setToggleState (i == 0, juce::dontSendNotification);
                button.onClick = [this, i]
                {
                    xyPad.selectSeed (static_cast<int> (i));
                    for (size_t seed = 0; seed < xySeeds.size(); ++seed)
                        xySeeds[seed].setToggleState (seed == i, juce::dontSendNotification);
                };
            }
            close.getProperties().set ("footerControl", true);
            close.getProperties().set ("backToGrid", true);
            close.setTooltip ("Return to the step grid (Esc)");
            close.onClick = [this] { if (onClose) onClose(); };
            mode.setTitle ("Random mode");
            mode.setTooltip ("Click to cycle through Off, Stack and Per Channel");
            addAndMakeVisible (patternLength);
            patternLength.setTitle ("Randomizer pattern length");
            patternLength.setTooltip ("Drag or double-click to set the generated pattern length");
            patternLength.caption = "Steps";
            patternLength.captionWidth = 42;
            patternLength.captionFontSize = 12;
            patternLength.minimum = 1;
            patternLength.maximum = Constants::maxSteps;
            patternLength.resetValue = 16;
            patternLength.onBegin = [this] { controller.begin ("Change generated pattern length"); };
            patternLength.onChange = [this] (int value) { controller.command ("generated_steps", { value }); };
            patternLength.onEnd = [this] { controller.commit(); };
            patternLength.onCancel = [this] { controller.cancel(); };
        }
        else
        {
            getProperties().set ("hideDisabledHelp", true);
            addAndMakeVisible (wordmark);
            wordmark.onAccentSelected = [this] (bool blue) { if (onAccentSelected) onAccentSelected (blue); };
            wordmark.onThemeGesture = [this] (bool started) { if (onThemeGesture) onThemeGesture (started); };
            wordmark.onAccentColourEdited = [this] (bool blue, std::optional<juce::Colour> colour) { if (onAccentColourEdited) onAccentColourEdited (blue, colour); };
            wordmark.onBackgroundToneEdited = [this] (Palette::BackgroundTone tone) { if (onBackgroundToneEdited) onBackgroundToneEdited (tone); };
            wordmark.onRandomizerCycle = [this] { if (onRandomizerCycle) onRandomizerCycle(); };
        }
        addAndMakeVisible (mode);
        addAndMakeVisible (refresh);
        addAndMakeVisible (velocity);
        addAndMakeVisible (probability);
        addAndMakeVisible (roll);
        addChildComponent (rhythm);
        rhythm.setTitle ("Step rhythm and triplets");
        rhythm.onClick = [this] { if (onRhythm) onRhythm(); };
        addAndMakeVisible (nudge);
        addAndMakeVisible (remove);
        addAndMakeVisible (cycle);
        mode.onClick = [this] { const auto m = controller.state.playback.generationMode; controller.command ("mode", { m == GenerationMode::staticSource ? "stack" : m == GenerationMode::stack ? "per_channel" : "static" }, "Generation mode"); };
        refresh.setTitle ("Refresh Every");
        refresh.setTooltip ("Steps between generations");
        refresh.caption = "Refresh Every";
        refresh.minimum = 1;
        refresh.maximum = Constants::maxSteps;
        refresh.suffix = " steps";
        refresh.onBegin = [this] { controller.begin ("Refresh length"); };
        refresh.onChange = [this] (int v) { controller.command ("refresh_steps", { v }); };
        refresh.onEnd = [this] { controller.commit(); };
        for (int ch = 0; ch < 8; ++ch)
        {
            auto& lock = locks[static_cast<size_t> (ch)];
            addAndMakeVisible (lock);
            lock.minimum = -1;
            lock.maximum = 15;
            lock.resetValue = -1;
            lock.sensitivity = 14;
            lock.caption = juce::String (ch + 1);
            lock.setTitle ("Pattern lock channel " + juce::String (ch + 1));
            lock.setTooltip ("Drag to choose a pattern. Right-click to set it to Off.");
            lock.format = [] (int v) { return v < 0 ? juce::String ("OFF") : "P" + juce::String (v + 1); };
            lock.onBegin = [this] { controller.begin ("Channel lock"); };
            lock.onChange = [this, ch] (int v) { controller.command ("channel_lock", v < 0 ? nlohmann::json::array ({ ch + 1, "random" }) : nlohmann::json::array ({ ch + 1, v + 1 })); };
            lock.onEnd = [this] { controller.commit(); };
        }
        velocity.setTitle ("Selected velocity offset");
        probability.setTitle ("Selected probability offset");
        roll.setTitle ("Selected split");
        velocity.setTooltip ("Offsets selected velocities together");
        probability.setTooltip ("Offsets selected probabilities together");
        velocity.caption = "Velocity";
        probability.caption = "Probability";
        roll.caption = "Split";
        nudge.caption = "Nudge";
        nudge.setTitle ("Selected timing nudge");
        nudge.setTooltip ("Drag left for earlier hits, right for later");
        nudge.minimum = -100;
        nudge.maximum = 100;
        nudge.resetValue = 0;
        nudge.sensitivity = 2;
        nudge.horizontalAdjustment = true;
        nudge.gestureOnly = true;
        nudge.onBegin = [this] { beginRelative ("Selected nudge"); };
        nudge.onChange = [this] (int v)
        {
            if (valueStarts.empty())
                return;
            int minimumDelta = -200, maximumDelta = 200;
            for (const auto& [p, initial] : valueStarts)
            {
                juce::ignoreUnused (p);
                minimumDelta = juce::jmax (minimumDelta, -100 - initial.nudge);
                maximumDelta = juce::jmin (maximumDelta, 100 - initial.nudge);
            }
            const int delta = juce::jlimit (minimumDelta, maximumDelta, v - valueStarts.front().second.nudge);
            for (const auto& [p, initial] : valueStarts)
            {
                auto next = controller.cell (p);
                next.nudge = initial.nudge + delta;
                controller.editCell (p, next);
            }
            controller.notify();
        };
        velocity.minimum = -127;
        velocity.maximum = 127;
        probability.minimum = -100;
        probability.maximum = 100;
        for (auto* control : { &velocity, &probability })
        {
            control->showAdjustmentHintAtZero = true;
            control->horizontalDragOnTouch = true;
        }
        roll.minimum = 1;
        roll.maximum = 8;
        roll.resetValue = 1;
        velocity.onBegin = [this] { beginRelative ("Selected velocity"); };
        probability.onBegin = [this] { beginRelative ("Selected probability"); };
        roll.onBegin = [this] { controller.begin ("Selected split"); };
        velocity.onChange = [this] (int v) { applyRelative (Layer::velocity, v); };
        probability.onChange = [this] (int v) { applyRelative (Layer::chance, v); };
        roll.onChange = [this] (int v) { for (auto p : controller.selection (true)) { auto cell = controller.cell (p); cell.roll = juce::jlimit (1, 8, v); controller.editCell (p, cell); } controller.notify(); };
        velocity.onDoubleClick = [this] { resetSelectedValue ("Reset velocity", &Cell::velocity, CellDefaults::velocity); };
        probability.onDoubleClick = [this] { resetSelectedValue ("Reset probability", &Cell::probability, CellDefaults::probability); };
        nudge.onDoubleClick = [this] { resetSelectedValue ("Reset nudge", &Cell::nudge, CellDefaults::nudge); };
        roll.onDoubleClick = [this] { resetSelectedValue ("Reset split", &Cell::roll, CellDefaults::roll); };
        for (auto [control, layer] : { std::pair { &velocity, Layer::velocity }, std::pair { &probability, Layer::chance }, std::pair { &nudge, Layer::nudge }, std::pair { &roll, Layer::roll } })
        {
            const auto begin = control->onBegin;
            control->onBegin = [this, begin, layer]
            {
                begin();
                previousLayer = controller.layer;
                controller.layer = layer;
                controller.notify();
            };
        }
        for (auto* control : { &velocity, &probability, &roll, &nudge })
        {
            control->onCancel = [this]
            {
                if (previousLayer.has_value())
                {
                    controller.layer = *previousLayer;
                    previousLayer.reset();
                }
                velocity.value = probability.value = 0;
                controller.cancel();
            };
            control->onEnd = [this]
            {
                if (previousLayer.has_value())
                {
                    controller.layer = *previousLayer;
                    previousLayer.reset();
                }
                controller.commit();
                velocity.value = probability.value = 0;
                controller.notify();
                sync();
            };
        }
        remove.getProperties().set ("icon", "clear");
        remove.setTooltip ("Delete key clears selected steps");
        remove.onClick = [this] { controller.deleteSelection(); };
        cycle.locations = [this] { return controller.selection (true); };
        cycle.inlineLayout = !randomSection;
        cycle.resetOnDoubleClickAnywhere = !randomSection;
        for (auto* control : { &velocity, &probability, &roll, &nudge })
        {
            control->valueFontSize = 22;
            control->captionFontSize = 16;
            control->captionGap = 6;
        }
        mode.setVisible (randomSection);
        refresh.setVisible (randomSection);
        for (auto& lock : locks)
            lock.setVisible (randomSection);
        for (auto* control : { &velocity, &probability, &roll, &nudge })
            control->setVisible (!randomSection);
        remove.setVisible (!randomSection);
        cycle.setVisible (!randomSection);
    }
    PreviewPanel::~PreviewPanel()
    {
        removeMouseListener (&childHoverListener);
        if (!randomSection)
            controller.inspectorHoverLayer.reset();
    }
    void PreviewPanel::setHoverLayer (std::optional<Layer> layer)
    {
        if (randomSection || controller.inspectorHoverLayer == layer)
            return;
        controller.inspectorHoverLayer = layer;
        controller.notify();
    }
    void PreviewPanel::updateHoverLayer (juce::Point<int> position)
    {
        if (controller.selection (true).empty())
        {
            setHoverLayer (std::nullopt);
            return;
        }
        for (auto [control, layer] : { std::pair<juce::Component*, Layer> { &velocity, Layer::velocity },
                 { &probability, Layer::chance }, { &nudge, Layer::nudge }, { &roll, Layer::roll }, { &cycle, Layer::chance } })
        {
            auto bounds = control->getBounds();
            if (control == &cycle && !compact)
                bounds = bounds.withTop (32);
            if (control->isVisible() && bounds.contains (position))
            {
                setHoverLayer (layer);
                return;
            }
        }
        setHoverLayer (std::nullopt);
    }
    void PreviewPanel::mouseMove (const juce::MouseEvent& event)
    {
        if (event.source.canHover())
            updateHoverLayer (event.getEventRelativeTo (this).getPosition());
    }
    void PreviewPanel::mouseEnter (const juce::MouseEvent& event) { mouseMove (event); }
    void PreviewPanel::mouseExit (const juce::MouseEvent& event) { mouseMove (event); }
    void PreviewPanel::visibilityChanged()
    {
        if (!isVisible())
            setHoverLayer (std::nullopt);
    }
    void PreviewPanel::setDisplayedLayer (Layer layer)
    {
        velocity.setLayerHighlighted (layer == Layer::velocity);
        probability.setLayerHighlighted (layer == Layer::chance);
        nudge.setLayerHighlighted (layer == Layer::nudge);
        roll.setLayerHighlighted (layer == Layer::roll);
    }
    void PreviewPanel::setXYRandomizer (bool enabled)
    {
        if (xyRandomizer == enabled)
            return;
        cancelGesture();
        xyRandomizer = enabled;
        xyPad.setVisible (enabled);
        for (auto& seed : xySeeds)
            seed.setVisible (enabled);
        patternLength.setVisible (!enabled);
        mode.setVisible (!enabled);
        refresh.setVisible (!enabled);
        for (auto& lock : locks)
            lock.setVisible (!enabled);
        resized();
        sync();
        repaint();
    }
    void PreviewPanel::beginRelative (const juce::String& label)
    {
        controller.begin (label);
        valueStarts.clear();
        for (auto p : controller.selection (true))
            valueStarts.emplace_back (p, controller.cell (p));
    }
    void PreviewPanel::applyRelative (Layer layer, int offset)
    {
        for (const auto& [p, c] : valueStarts)
            controller.editCell (p, withValue (c, layer, cellValue (c, layer) + offset));
        controller.notify();
    }
    void PreviewPanel::resetSelectedValue (const juce::String& label, int Cell::* field, int value)
    {
        controller.begin (label);
        for (auto location : controller.selection (true))
        {
            auto cell = controller.cell (location);
            cell.*field = value;
            controller.editCell (location, cell);
        }
        controller.commit();
        sync();
    }
    void PreviewPanel::setInspectorBounds (juce::Rectangle<int> bounds)
    {
        if (inspectorBounds == bounds)
            return;
        inspectorBounds = bounds;
        resized();
        repaint();
    }
    void PreviewPanel::setDesktopRandomizerSize (int width, int height, int rightInset)
    {
        const bool insetChanged = std::exchange (randomizerRightInset, rightInset) != rightInset;
        if (getWidth() != width || getHeight() != height)
            setSize (width, height);
        else if (insetChanged)
        {
            resized();
            repaint();
        }
    }

    void PreviewPanel::setCompact (bool enabled, bool phone)
    {
        if (enabled)
            setXYRandomizer (false);
        compact = enabled;
        phoneGenerator = randomSection && phone;
        rhythm.setVisible (!randomSection && phone);
        if (randomSection)
        {
            if (phoneGenerator)
                mode.getProperties().set ("phoneMenuIcon", static_cast<int> (phoneMenu::dice));
            else
                mode.getProperties().remove ("phoneMenuIcon");
        }
        cycle.compact = enabled;
        for (auto* control : { &velocity, &probability, &roll, &nudge })
        {
            control->valueFontSize = compact ? 17.0f : 22.0f;
            control->captionFontSize = compact ? 10.0f : 16.0f;
            control->captionGap = compact ? 2 : 6;
        }
        probability.caption = compact ? "Chance" : "Probability";
        velocity.captionWidth = compact ? 44 : 0;
        probability.captionWidth = compact ? 44 : 0;
        nudge.captionWidth = compact ? 40 : 0;
        roll.captionWidth = compact ? 28 : 0;
        resized();
        repaint();
    }
    void PreviewPanel::cancelGesture()
    {
        xyPad.cancelGesture();
        setHoverLayer (std::nullopt);
        for (auto* control : { &velocity, &probability, &roll, &nudge, &refresh, &patternLength })
            control->cancelGesture();
        for (auto& lock : locks)
            lock.cancelGesture();
        cycle.cancelGesture();
    }
    void PreviewPanel::layoutXYPad (juce::Rectangle<int> bounds)
    {
        constexpr int buttonHeight = 42, gap = 5;
        const auto buttons = bounds.removeFromBottom (buttonHeight);
        const int side = juce::jmax (0, juce::jmin (48, (buttons.getWidth() - gap * (generatorSeedCount - 1)) / generatorSeedCount));
        const int start = buttons.getCentreX() - (side * generatorSeedCount + gap * (generatorSeedCount - 1)) / 2;
        for (int i = 0; i < generatorSeedCount; ++i)
            xySeeds[static_cast<size_t> (i)].setBounds (start + i * (side + gap), buttons.getY(), side, buttonHeight);
        xyPad.setBounds (bounds.withTrimmedBottom (5));
    }

    void PreviewPanel::resized()
    {
        wordmark.setVisible (!compact && !randomSection);
        close.setVisible (randomSection && !compact && !phoneGenerator);
        if (!compact && !randomSection)
            wordmark.setBounds (brandingBoundsForInspector (inspectorBounds).toNearestInt());
        if (phoneGenerator || (compact && xyRandomizer))
        {
            const PhoneGeneratorLayout phone (getLocalBounds());
            patternLength.setBounds (phone.preview.reduced (14, 0).removeFromRight (84).withY (6).withHeight (32));
            const auto controls = phone.controls.reduced (14, 0);
            if (xyRandomizer)
            {
                layoutXYPad (controls.reduced (0, 12));
                return;
            }
            mode.setBounds (controls.withY (33).withWidth (controls.getWidth() - 118).withHeight (48));
            refresh.setBounds (controls.withTrimmedLeft (controls.getWidth() - 108).withY (14).withHeight (67));
            const int stride = (controls.getWidth() + 8) / 4;
            for (int ch = 0; ch < 8; ++ch)
                locks[static_cast<size_t> (ch)].setBounds (controls.getX() + ch % 4 * stride, 112 + ch / 4 * 66, stride - 8, 62);
            return;
        }
        if (compact)
        {
            if (randomSection)
            {
                patternLength.setBounds (getLocalBounds().reduced (18, 0).removeFromRight (90).withY (30).withHeight (44));
                mode.setBounds (18, 30, 180, 44);
                refresh.setBounds (216, 10, 110, 64);
                const int stride = (getWidth() - 36) / 8;
                for (int ch = 0; ch < 8; ++ch)
                    locks[static_cast<size_t> (ch)].setBounds (18 + ch * stride, 98, stride - 6, 66);
                return;
            }
            int inputX = 6;
            for (auto* control : { &velocity, &probability, &nudge, &roll })
            {
                const int width = control->captionWidth + 44;
                control->setBounds (inputX, 4, width, 40);
                inputX += width + 10;
            }
            if (rhythm.isVisible())
            {
                rhythm.setBounds (inputX, 4, 74, 40);
                inputX += 84;
            }
            remove.setBounds (getWidth() - 42, 4, 36, 40);
            cycle.captionHeight = 0;
            cycle.setBounds (inputX - 4, 2, juce::jlimit (0, cycle.preferredWidth (44), remove.getX() - inputX), 44);
            return;
        }
        if (randomSection)
        {
            const RandomLabLayout lab (getLocalBounds(), randomizerRightInset);
            close.setBounds (lab.close);
            patternLength.setBounds (lab.preview.reduced (20, 0).removeFromRight (90).withTrimmedTop (10).withHeight (36));
            if (xyRandomizer)
            {
                layoutXYPad (lab.controls.reduced (16, 12));
                return;
            }
            mode.setBounds (lab.mode);
            refresh.captionFontSize = 15;
            refresh.captionGap = 2;
            refresh.valueFontSize = 20;
            refresh.setBounds (lab.refresh);
            for (int ch = 0; ch < 8; ++ch)
            {
                auto& lock = locks[static_cast<size_t> (ch)];
                lock.captionFontSize = 14;
                lock.captionGap = 2;
                lock.valueFontSize = 20;
                lock.setBounds (lab.lockBounds (ch));
            }
            return;
        }
        if (inspectorBounds.isEmpty())
            return;
        const int x = inspectorBounds.getX();
        constexpr int inputHeight = Layout::touchTarget;
        constexpr int captionHeight = 24;
        constexpr int inputWidth = 90;
        const int captionY = inspectorBounds.getCentreY() - (captionHeight + inputHeight) / 2 - 2;
        const int inputY = captionY + captionHeight;
        int inputX = x + 16;
        for (auto* control : { &velocity, &probability, &nudge, &roll })
        {
            control->setBounds (inputX, captionY, inputWidth, captionHeight + inputHeight);
            inputX += inputWidth + 10;
        }
        remove.setBounds (inspectorBounds.getRight() - 76, inputY, inputHeight, inputHeight);
        cycle.captionHeight = captionHeight - 4;
        cycle.setBounds (inputX, captionY, juce::jlimit (0, cycle.preferredWidth (inputHeight + 8), remove.getX() - inputX - 16),
                         inputHeight + 8 + cycle.captionHeight);
    }
    void PreviewPanel::sync()
    {
        const auto& p = controller.state.playback;
        if (randomSection && !xyRandomizer)
            patternLength.setValue (p.generatedStepCount);
        mode.setButtonText (p.generationMode == GenerationMode::staticSource ? "Off" : p.generationMode == GenerationMode::stack ? "Stack"
                                                                                                                                 : "Per Channel");
        refresh.maximum = p.generatedStepCount;
        refresh.setValue (controller.state.refreshSteps);
        for (int ch = 0; ch < 8; ++ch)
        {
            auto& lock = locks[static_cast<size_t> (ch)];
            lock.setValue (p.channels[static_cast<size_t> (ch)].lock);
            lock.setEnabled (ch < p.channelCount);
            if (randomSection)
            {
                const auto colour = Palette::channel (ch, controller.drumColors);
                const auto caption = ch >= p.channelCount ? Palette::faint : controller.drumColors ? colour : Palette::muted;
                if (std::exchange (lock.captionColour, caption) != caption)
                    lock.repaint();
            }
        }
        const auto selected = controller.selection (true);
        const bool active = !selected.empty();
        if (!active)
            setHoverLayer (std::nullopt);
        const float inspectorAlpha = active ? 1.0f : 0.25f;
        for (auto* control : { &velocity, &probability, &roll, &nudge })
        {
            control->setEnabled (active);
            control->setAlpha (inspectorAlpha);
        }
        remove.setEnabled (active);
        rhythm.setEnabled (selected.size() == 1);
        rhythm.setAlpha (selected.size() == 1 ? 1.0f : 0.25f);
        remove.setAlpha (inspectorAlpha);
        cycle.setAlpha (inspectorAlpha);
        if (active)
        {
            nudge.setValue (controller.cell (selected.front()).nudge);
            nudge.mixed = std::any_of (selected.begin(), selected.end(), [&] (auto loc) { return controller.cell (loc).nudge != controller.cell (selected.front()).nudge; });
            const int value = controller.cell (selected.front()).roll;
            roll.setValue (value);
            roll.mixed = std::any_of (selected.begin(), selected.end(), [&] (auto loc) { return controller.cell (loc).roll != value; });
        }
        else
        {
            roll.mixed = true;
            nudge.mixed = true;
        }
        cycle.sync();
        Painted next;
        next.reverseRows = reverseRows;
        next.selectedCount = selected.size();
        if (randomSection)
        {
            next.source = controller.source();
            next.channelCount = p.channelCount;
            next.stepCount = xyRandomizer ? p.stepCount : p.generatedStepCount;
            for (size_t ch = 0; ch < next.labels.size(); ++ch)
            {
                next.labels[ch] = p.channels[ch].label;
                next.channelColours[ch] = Palette::channel (static_cast<int> (ch), controller.drumColors);
                for (size_t s = 0; s < next.velocities[ch].size(); ++s)
                {
                    const auto& cell = xyRandomizer ? controller.cell ({ static_cast<int> (ch), static_cast<int> (s) }) : p.generated[ch][s];
                    next.velocities[ch][s] = cell.enabled ? static_cast<uint8_t> (cell.velocity) : 0;
                }
            }
        }
        if (painted != next)
        {
            painted = std::move (next);
            repaint();
        }
    }
    void PreviewPanel::paintRandomizerGrid (juce::Graphics& g, juce::Rectangle<float> area,
                                              float labelWidth, float labelSize) const
    {
        const auto& state = controller.state.playback;
        if (!painted || area.isEmpty() || area.getWidth() <= labelWidth)
            return;

        auto labels = area.removeFromLeft (labelWidth);
        const auto ruler = area.removeFromTop (16.0f);
        labels.removeFromTop (ruler.getHeight());
        const int stepCount = painted->stepCount;
        const float cellWidth = area.getWidth() / static_cast<float> (stepCount);
        const float rowHeight = area.getHeight() / static_cast<float> (Constants::maxChannels);
        const float firstY = area.getCentreY() - rowHeight * static_cast<float> (state.channelCount) * 0.5f;
        const float gap = juce::jmin (3.0f, cellWidth * 0.2f);
        const float radius = juce::jmin (4.0f, cellWidth * 0.2f);
        const auto backdrop = gridBackdropColour();
        g.setColour (backdrop.overlaidWith (Palette::accent.withAlpha (0.05f)));
        g.fillRoundedRectangle (area.withY (firstY).withHeight (rowHeight * static_cast<float> (state.channelCount)), radius);
        const int rulerStride = cellWidth < 17.0f ? 8 : 4;
        for (int step = 0; step < stepCount; ++step)
        {
            const float x = area.getX() + static_cast<float> (step) * cellWidth;
            if (step == 0 || step == stepCount - 1
                || (step % rulerStride == 0 && static_cast<float> (stepCount - 1 - step) * cellWidth >= 18.0f))
                text (g, juce::String (step + 1), ruler.withX (x + cellWidth * 0.5f - 9.0f).withWidth (18.0f), 10, Palette::muted);
            if (step != 0 && step % 4 == 0)
            {
                g.setColour (Palette::border.withAlpha (step % 16 == 0 ? 0.85f : 0.42f));
                g.fillRect (x - 0.5f, firstY, 1.0f, rowHeight * static_cast<float> (state.channelCount));
            }
        }
        for (int ch = 0; ch < state.channelCount; ++ch)
        {
            const auto channel = static_cast<size_t> (ch);
            const int row = reverseRows ? state.channelCount - 1 - ch : ch;
            const float y = firstY + static_cast<float> (row) * rowHeight;
            const auto colour = painted->channelColours[channel];
            text (g, state.channels[channel].label, labels.withTrimmedRight (8.0f).withY (y).withHeight (rowHeight),
                  labelSize, controller.drumColors ? colour : Palette::muted, true, juce::Justification::centredLeft);
            for (int step = 0; step < stepCount; ++step)
            {
                const auto index = static_cast<size_t> (step);
                const float x = area.getX() + static_cast<float> (step) * cellWidth + gap * 0.5f;
                const juce::Rectangle<float> cell (x, y + 2.0f, cellWidth - gap, juce::jmax (0.0f, rowHeight - 4.0f));
                const int hitVelocity = painted->velocities[channel][index];
                if (flashes[channel][index])
                {
                    g.setColour (Palette::text);
                    g.fillRoundedRectangle (cell, radius);
                }
                else if (hitVelocity > 0)
                {
                    paintGridHitFill (g, cell, colour, hitVelocity, radius);
                    g.setColour (Palette::text.withAlpha (0.14f));
                    g.drawRoundedRectangle (cell.reduced (0.5f), radius, 1);
                }
                else
                    paintGridSlot (g, cell, backdrop, step % 4 == 0, radius);
            }
        }
    }

    void PreviewPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::app);
        if (phoneGenerator || (compact && xyRandomizer))
        {
            const PhoneGeneratorLayout phone (getLocalBounds());
            const auto pattern = controller.source() < 0 ? juce::String ("Muted Pattern")
                                                        : "Pattern " + juce::String (controller.source() + 1);
            phoneMenu::card (g, phone.preview.toFloat(), xyRandomizer ? pattern : juce::String ("Generated Pattern"));
            phoneMenu::card (g, phone.controls.toFloat());
            if (!xyRandomizer)
            {
                const auto heading = phone.controls.toFloat().reduced (14, 0);
                text (g, "Random Mode", heading.withY (static_cast<float> (refresh.getY())).withHeight (18),
                      refresh.captionFontSize, refresh.captionColour, true, juce::Justification::centredLeft);
                text (g, lockHeading, heading.withY (94).withHeight (18), 12, Palette::muted, true, juce::Justification::centredLeft);
            }

            paintRandomizerGrid (g, phone.preview.toFloat().reduced (14, 12).withTrimmedTop (26), 46.0f, 11.0f);
            return;
        }
        if (compact)
        {
            surface (g, getLocalBounds().toFloat().reduced (1));
            if (randomSection)
            {
                text (g, "Generation mode", { 18, 8, 180, 18 }, 12, Palette::text, true, juce::Justification::centredLeft);
                text (g, lockHeading, { 18, 78, 300, 18 }, 12, Palette::muted, true, juce::Justification::centredLeft);
                paintRandomizerGrid (g, getLocalBounds().toFloat().reduced (12, 8).withTrimmedTop (210), 78.0f, 11.0f);
            }
            return;
        }
        if (randomSection)
        {
            const RandomLabLayout lab (getLocalBounds(), randomizerRightInset);
            text (g, xyRandomizer ? "Top Secret Randomizer" : "Super Secret Randomizer", lab.header.toFloat(), 29, Palette::text, true, juce::Justification::centredLeft);
            surface (g, lab.preview.toFloat());
            surface (g, lab.controls.toFloat());
            auto previewContent = lab.preview.toFloat().reduced (20, 16);
            text (g, xyRandomizer ? "Pattern " + juce::String (controller.source() + 1) : juce::String ("Generated Pattern"), previewContent.removeFromTop (24), 16, Palette::text, true, juce::Justification::centredLeft);
            previewContent.removeFromTop (12);

            paintRandomizerGrid (g, previewContent, 76.0f, 15.0f);

            if (xyRandomizer)
                return;
            text (g, "Random Mode", lab.mode.withY (lab.refresh.getY()).withHeight (18).toFloat(),
                  refresh.captionFontSize, Palette::text, true, juce::Justification::centredLeft);
            text (g, lockHeading, lab.locksHeading.toFloat(), 17, Palette::text, true, juce::Justification::centredLeft);
            auto help = lab.help.toFloat();
            for (const auto* line : { "The randomizer draws from your non-empty patterns.",
                                      "Stack picks one per refresh. Per Channel can mix them per step.",
                                      "Refresh Every sets the number of steps between refreshes." })
                text (g, line, help.removeFromTop (18), 13, Palette::muted, false, juce::Justification::centredLeft);
            return;
        }
        g.setColour (Palette::raised);
        g.drawHorizontalLine (0, 0, static_cast<float> (getWidth()));
        // The same warm/cool branding field as the original, rendered with cached font/vector primitives.
        g.setGradientFill (juce::ColourGradient (Palette::accent.withAlpha (0.09f), 0, 180, Palette::info.withAlpha (0.04f), static_cast<float> (getWidth()), 0, false));
        g.fillAll();
        surface (g, inspectorBounds.toFloat());
        const auto selectedCount = controller.selection (true).size();
        const auto labelColour = Palette::text.withAlpha (selectedCount == 0 ? 0.25f : 1.0f);
        text (g, "Cycle", cycle.getBounds().withY (velocity.getY()).withHeight (18).toFloat(), velocity.captionFontSize, labelColour, true, juce::Justification::centredLeft);
        text (g, "Delete", remove.getBounds().withY (velocity.getY()).withHeight (18).withWidth (76).toFloat(), velocity.captionFontSize, labelColour, true, juce::Justification::centredLeft);
    }
    void PreviewPanel::updatePlayback()
    {
        cycle.updatePlayback();
        if (!randomSection)
            return;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        bool dirty = false;
        for (int ch = 0; ch < Constants::maxChannels; ++ch)
            for (int s = 0; s < Constants::maxSteps; ++s)
            {
                const bool next = (xyRandomizer ? controller.sourceFlashes : controller.generatedFlashes)[static_cast<size_t> (ch)][static_cast<size_t> (s)] > now;
                auto& previous = flashes[static_cast<size_t> (ch)][static_cast<size_t> (s)];
                if (next != previous)
                {
                    previous = next;
                    dirty = true;
                }
            }
        if (dirty)
        {
            if (compact || phoneGenerator)
                repaint();
            else
                repaint (RandomLabLayout (getLocalBounds(), randomizerRightInset).preview);
        }
    }
}
