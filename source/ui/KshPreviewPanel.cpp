#include "KshPreviewPanel.h"
#include "KshLayout.h"
namespace ksh::ui
{
    namespace
    {
        Cell withInspectorOffset (const Cell& cell, Layer layer, int offset)
        {
            auto next = withValue (cell, layer, cellValue (cell, layer) + offset);
            if (cell.roll < 2 || offset == 0)
                return next;

            for (int part = 0; part < cell.roll; ++part)
            {
                const auto index = static_cast<size_t> (part);
                if (layer == Layer::chance)
                    next.splitGates[index].setProbability (cell.splitGates[index].probability() + offset);
                else if (layer == Layer::velocity && cell.splitVelocities[index] != 0)
                    next.setSplitVelocity (part, juce::jlimit (1, 127, cell.splitVelocity (part) + offset));
            }
            return next;
        }

        struct RandomLabLayout
        {
            RandomLabLayout (juce::Rectangle<int> bounds, int rightInset)
            {
                auto content = bounds.reduced (0, 12).withTrimmedLeft (Layout::outerGutter).withTrimmedRight (rightInset);
                controls = content.removeFromRight (468);
                preview = content.withTrimmedRight (16);
            }

            juce::Rectangle<int> preview, controls;
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

    AccentWordmark::AccentWordmark (float scale)
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
        addAndMakeVisible (textBrightnessLogo);
        textBrightnessLogo.setTitle ("Text brightness");
        textBrightnessLogo.setTooltip ("Drag up to brighten text or down to dim it. Double-click to reset.");
        textBrightnessLogo.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        textBrightnessLogo.pressed = [this] (const juce::MouseEvent& event)
        {
            if (!event.mods.isLeftButtonDown() || event.mods.isPopupMenu())
                return;
            startingTextBrightness = Palette::textBrightness();
            if (onThemeGesture)
                onThemeGesture (true);
        };
        textBrightnessLogo.dragged = [this] (const juce::MouseEvent& event)
        {
            if (startingTextBrightness && onTextBrightnessEdited)
                onTextBrightnessEdited (*startingTextBrightness - event.getDistanceFromDragStartY());
        };
        textBrightnessLogo.released = [this] (const juce::MouseEvent&)
        {
            if (std::exchange (startingTextBrightness, std::nullopt) && onThemeGesture)
                onThemeGesture (false);
        };
        textBrightnessLogo.onDoubleClick = [this]
        {
            if (onTextBrightnessEdited)
                onTextBrightnessEdited (0);
        };
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
        textBrightnessLogo.setBounds (orangeLogo.getBounds().withX (orangeLogo.getRight()).withRight (blueLogo.getX()));
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
            std::array<juce::DrawableShape*, 3> result {};
            for (auto* child : artwork->getChildren())
                if (auto* shape = dynamic_cast<juce::DrawableShape*> (child))
                {
                    if (shape->getFill().colour == Palette::orangeAccent)
                        result[0] = shape;
                    else if (shape->getFill().colour == Palette::blueAccent)
                        result[1] = shape;
                    else if (shape->getFill().colour == juce::Colour (0xffdbdee5))
                        result[2] = shape;
                }
            return result;
        }();
        const std::array colours { Palette::accentColour (false), Palette::accentColour (true), Palette::white };
        for (size_t index = 0; index < letters.size(); ++index)
            if (auto* letter = letters[index])
            {
                const auto colour = colours[index];
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
            addAndMakeVisible (xyPad);
            for (size_t i = 0; i < xySeeds.size(); ++i)
            {
                auto& button = xySeeds[i];
                addAndMakeVisible (button);
                button.setButtonText (juce::String::charToString ('A' + static_cast<int> (i)));
                button.setTitle ("Generator seed " + button.getButtonText());
                button.setToggleState (i == 0, juce::dontSendNotification);
                button.onClick = [this, i]
                {
                    xyPad.selectSeed (static_cast<int> (i));
                    for (size_t seed = 0; seed < xySeeds.size(); ++seed)
                        xySeeds[seed].setToggleState (seed == i, juce::dontSendNotification);
                };
            }
        }
        else
        {
            getProperties().set ("hideDisabledHelp", true);
            addAndMakeVisible (wordmark);
            wordmark.onAccentSelected = [this] (bool blue) { if (onAccentSelected) onAccentSelected (blue); };
            wordmark.onThemeGesture = [this] (bool started) { if (onThemeGesture) onThemeGesture (started); };
            wordmark.onAccentColourEdited = [this] (bool blue, std::optional<juce::Colour> colour) { if (onAccentColourEdited) onAccentColourEdited (blue, colour); };
            wordmark.onBackgroundToneEdited = [this] (Palette::BackgroundTone tone) { if (onBackgroundToneEdited) onBackgroundToneEdited (tone); };
            wordmark.onTextBrightnessEdited = [this] (int value) { if (onTextBrightnessEdited) onTextBrightnessEdited (value); };
        }
        addAndMakeVisible (velocity);
        addAndMakeVisible (probability);
        addAndMakeVisible (nudge);
        addAndMakeVisible (span);
        addAndMakeVisible (roll);
        addAndMakeVisible (remove);
        addAndMakeVisible (cycle);
        velocity.setTitle ("Selected velocity offset");
        probability.setTitle ("Selected probability offset");
        span.setTitle ("Selected span");
        span.setTooltip ("Adjust how many grid steps each selected hit spans");
        roll.setTitle ("Selected split");
        velocity.setTooltip ("Offsets selected step and split velocities together");
        probability.setTooltip ("Offsets selected step and split probabilities together");
        velocity.caption = "Velocity";
        probability.caption = "Prob";
        span.caption = "Span";
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
        span.minimum = span.resetValue = CellDefaults::span;
        span.horizontalDragOnTouch = true;
        roll.minimum = roll.resetValue = CellDefaults::roll;
        roll.maximum = Constants::maxRoll;
        velocity.onBegin = [this] { beginRelative ("Selected velocity"); };
        probability.onBegin = [this] { beginRelative ("Selected probability"); };
        span.onBegin = [this] { controller.begin ("Selected span"); };
        roll.onBegin = [this] { controller.begin ("Selected split"); };
        velocity.onChange = [this] (int v) { applyRelative (Layer::velocity, v); };
        probability.onChange = [this] (int v) { applyRelative (Layer::chance, v); };
        span.onChange = [this] (int length)
        {
            const auto selected = controller.selection (true);
            if (length > 1 && !std::ranges::all_of (selected, [this, length] (Location location) { return controller.canFitSpan (location, length); }))
                return;
            for (const auto location : selected)
            {
                auto cell = controller.cell (location);
                cell.span = length;
                controller.editCell (location, cell);
            }
            controller.notify();
        };
        roll.onChange = [this] (int v) { for (auto p : controller.selection (true)) { auto cell = controller.cell (p); cell.roll = juce::jlimit (1, Constants::maxRoll, v); controller.editCell (p, cell); } controller.notify(); };
        velocity.onDoubleClick = [this] { resetSelectedValue ("Reset velocity", &Cell::velocity, CellDefaults::velocity); };
        probability.onDoubleClick = [this] { resetSelectedValue ("Reset probability", &Cell::probability, CellDefaults::probability); };
        nudge.onDoubleClick = [this] { resetSelectedValue ("Reset nudge", &Cell::nudge, CellDefaults::nudge); };
        span.onDoubleClick = [this] { resetSelectedValue ("Reset span", &Cell::span, CellDefaults::span); };
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
        for (auto* control : { &velocity, &probability, &nudge, &span, &roll })
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
        cycle.editAllSplits = !randomSection;
        cycle.inlineLayout = !randomSection;
        cycle.resetOnDoubleClickAnywhere = !randomSection;
        for (auto* control : { &velocity, &probability, &nudge, &span, &roll })
        {
            control->valueFontSize = 22;
            control->captionFontSize = 16;
            control->captionGap = 6;
        }
        for (auto* control : { &velocity, &probability, &nudge, &span, &roll })
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
            controller.editCell (p, withInspectorOffset (c, layer, offset));
        controller.notify();
    }
    void PreviewPanel::resetSelectedValue (const juce::String& label, int Cell::* field, int value)
    {
        controller.begin (label);
        for (auto location : controller.selection (true))
        {
            auto cell = controller.cell (location);
            if (field == &Cell::velocity || field == &Cell::probability)
                cell = withInspectorOffset (cell, field == &Cell::velocity ? Layer::velocity : Layer::chance, value - cell.*field);
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

    void PreviewPanel::setCompact (bool enabled, bool usePhoneLayout, bool onPhone)
    {
        compact = enabled;
        cycle.compact = enabled;
        for (auto* control : { &velocity, &probability, &nudge, &span, &roll })
        {
            control->valueFontSize = compact ? 17.0f : 22.0f;
            control->captionFontSize = compact ? 10.0f : 16.0f;
            control->captionGap = compact ? 2 : 6;
        }
        probability.caption = compact && !usePhoneLayout ? "Chance" : "Prob";
        velocity.horizontalDragMultiplier = probability.horizontalDragMultiplier = onPhone ? 1.5f : 1.0f;
        velocity.alwaysShowHorizontalArrows = probability.alwaysShowHorizontalArrows = roll.alwaysShowHorizontalArrows = onPhone;
        span.horizontalAdjustment = roll.horizontalAdjustment = onPhone;
        span.sensitivity = roll.sensitivity = onPhone ? 24.0f : 4.0f;
        constexpr int phoneCaptionPadding = 7;
        for (auto [control, compactCaptionWidth] : { std::pair { &velocity, 44 }, { &probability, 44 }, { &nudge, 40 }, { &span, 28 }, { &roll, 28 } })
        {
            control->captionWidth = onPhone
                ? juce::GlyphArrangement::getStringWidthInt (font (control->captionFontSize, true), control->caption) + phoneCaptionPadding
                : compact ? compactCaptionWidth : 0;
        }
        resized();
        repaint();
    }
    void PreviewPanel::cancelGesture()
    {
        xyPad.cancelGesture();
        setHoverLayer (std::nullopt);
        for (auto* control : { &velocity, &probability, &nudge, &span, &roll })
            control->cancelGesture();
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
        constexpr int seedGap = 17;
        xyPad.setBounds (bounds.withTrimmedBottom (seedGap));
    }

    void PreviewPanel::resized()
    {
        wordmark.setVisible (!compact && !randomSection);
        if (!compact && !randomSection)
            wordmark.setBounds (brandingBoundsForInspector (inspectorBounds).toNearestInt());
        if (randomSection)
        {
            if (compact)
                layoutXYPad (PhoneGeneratorLayout (getLocalBounds()).controls.reduced (0, 12));
            else
                layoutXYPad (RandomLabLayout (getLocalBounds(), randomizerRightInset).controls.reduced (22, 20));
            return;
        }
        if (compact)
        {
            constexpr int side = 42;
            constexpr int inputY = (Layout::compactInspectorHeight - side) / 2;
            int inputX = 6;
            for (auto* control : { &velocity, &probability, &nudge, &span, &roll })
            {
                control->valueWidth = side;
                const int width = control->captionWidth + side;
                control->setBounds (inputX, inputY, width, side);
                inputX += width + 10;
            }
            remove.setBounds (getWidth() - 6 - side, inputY, side, side);
            cycle.captionHeight = 0;
            cycle.setBounds (inputX - 4, 2, juce::jlimit (0, cycle.preferredWidth (44), remove.getX() - inputX), 44);
            return;
        }
        if (inspectorBounds.isEmpty())
            return;
        constexpr int inputHeight = Layout::touchTarget;
        constexpr int captionHeight = 24;
        constexpr int minimumGap = 10;
        const auto inputs = std::array { &velocity, &probability, &nudge, &span, &roll };
        constexpr int inputCount = static_cast<int> (inputs.size());
        constexpr int gapCount = inputCount + 1; // Include the gaps before Cycle and Delete.
        const int captionY = inspectorBounds.getCentreY() - (captionHeight + inputHeight) / 2 - 2;
        const int inputY = captionY + captionHeight;
        const int left = inspectorBounds.getX() + 16;
        remove.setBounds (inspectorBounds.getRight() - 76, inputY, inputHeight, inputHeight);
        int inputWidth = inputHeight;
        for (const auto* control : inputs)
            inputWidth = juce::jmax (inputWidth, juce::GlyphArrangement::getStringWidthInt (font (control->captionFontSize, true), control->caption));
        const int cycleSpace = remove.getX() - left - inputCount * inputWidth;
        const int cycleWidth = juce::jlimit (0, cycle.preferredWidth (inputHeight + 8), cycleSpace - gapCount * minimumGap);
        const int gapSpace = juce::jmax (0, cycleSpace - cycleWidth);
        const auto inputX = [left, inputWidth, gapSpace] (int index)
        {
            return left + index * inputWidth + index * gapSpace / gapCount;
        };
        for (int index = 0; index < inputCount; ++index)
        {
            auto* control = inputs[static_cast<size_t> (index)];
            control->valueWidth = inputHeight;
            control->setBounds (inputX (index), captionY, inputWidth, captionHeight + inputHeight);
        }
        cycle.captionHeight = captionHeight - 4;
        cycle.setBounds (inputX (inputCount), captionY, cycleWidth, inputHeight + 8 + cycle.captionHeight);
    }
    void PreviewPanel::sync()
    {
        const auto& p = controller.state.playback;
        const auto selected = controller.selection (true);
        const bool active = !selected.empty();
        if (!active)
            setHoverLayer (std::nullopt);
        std::optional<juce::Colour> accent;
        if (!randomSection && active)
        {
            const int row = selected.front().channel;
            if (std::all_of (selected.begin(), selected.end(), [row] (Location location) { return location.channel == row; }))
                accent = Palette::channel (row, controller.drumColors);
        }
        if (std::exchange (rowAccent, accent) != accent)
            repaint();
        cycle.setBeatAccent (rowAccent);
        const float inspectorAlpha = active ? 1.0f : 0.25f;
        for (auto* control : { &velocity, &probability, &nudge, &roll })
        {
            control->setEnabled (active);
            control->setAlpha (inspectorAlpha);
            control->setHighlightAccent (rowAccent);
        }
        remove.setEnabled (active);
        remove.setAlpha (inspectorAlpha);
        cycle.setAlpha (inspectorAlpha);
        int largestSpan = span.minimum;
        for (const auto location : selected)
            largestSpan = juce::jmax (largestSpan, controller.cell (location).span);
        constexpr int manualSpanLimit = 8;
        span.maximum = active ? juce::jmax (manualSpanLimit, largestSpan) : span.minimum;
        while (span.maximum > span.minimum && !std::ranges::all_of (selected, [this] (Location location) { return controller.canFitSpan (location, span.maximum); }))
            --span.maximum;
        const bool canEditSpan = active && (span.maximum > span.minimum || largestSpan > span.minimum);
        span.setEnabled (canEditSpan);
        span.setAlpha (canEditSpan ? 1.0f : 0.25f);
        const int spanValue = active ? controller.cell (selected.front()).span : span.minimum;
        span.setValue (spanValue);
        const bool mixedSpan = !active || std::ranges::any_of (selected, [this, spanValue] (Location location) { return controller.cell (location).span != spanValue; });
        if (std::exchange (span.mixed, mixedSpan) != mixedSpan)
            span.repaint();
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
            next.stepCount = p.stepCount;
            next.drumColors = controller.drumColors;
            for (int channel = 0; channel < next.channelCount; ++channel)
            {
                const auto ch = static_cast<size_t> (channel);
                next.labels[ch] = p.channels[ch].label;
                next.channelColours[ch] = Palette::channel (channel, next.drumColors);
                if (next.source < 0)
                    continue;

                const auto& row = p.sources[static_cast<size_t> (next.source)][ch];
                const auto loop = controller.loop (channel);
                const int end = juce::jmin (next.stepCount, loop.loopStart + loop.loopLength);
                for (int step = loop.loopStart; step < end; ++step)
                {
                    const auto index = static_cast<size_t> (step);
                    const auto& cell = row[index];
                    if (!cell.enabled)
                        continue;

                    const bool fits = cell.span > 1 && step + cell.span <= end && rollSpanFits (row, loop, step, cell.span);
                    next.hits[ch][index] = { static_cast<uint8_t> (cell.velocity), static_cast<uint8_t> (cell.roll),
                                            static_cast<uint8_t> (fits ? cell.span : 1), cell.splitVelocities };
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
        if (!painted || painted->stepCount <= 0 || area.isEmpty())
            return;

        const auto labels = area.removeFromLeft (juce::jmin (labelWidth, area.getWidth() * 0.15f));
        const auto ruler = area.removeFromTop (16.0f);
        if (area.isEmpty())
            return;

        const int stepCount = painted->stepCount;
        const int channelCount = painted->channelCount;
        const float cellWidth = area.getWidth() / static_cast<float> (stepCount);
        const float rowHeight = area.getHeight() / static_cast<float> (Constants::maxChannels);
        const float firstY = area.getCentreY() - rowHeight * static_cast<float> (channelCount) * 0.5f;
        const float insetX = juce::jmin (1.5f, cellWidth * 0.09f);
        const float insetY = juce::jmin (1.5f, rowHeight * 0.08f);
        const float radius = juce::jmin (4.0f, cellWidth * 0.20f, rowHeight * 0.20f);
        const auto backdrop = gridBackdropColour();
        const bool narrowCells = cellWidth < 12.0f;
        const auto quietSlot = Palette::surface.interpolatedWith (backdrop, 0.62f);
        const auto quietBeat = quietSlot.darker (0.24f);
        const int rulerStride = cellWidth < 17.0f ? 8 : 4;
        for (int step = 0; step < stepCount; ++step)
        {
            if (step == 0 || step == stepCount - 1
                || (step % rulerStride == 0 && static_cast<float> (stepCount - 1 - step) * cellWidth >= 18.0f))
            {
                const float x = area.getX() + (static_cast<float> (step) + 0.5f) * cellWidth;
                text (g, juce::String (step + 1), { x - 9.0f, firstY - ruler.getHeight(), 18.0f, ruler.getHeight() }, 10, Palette::muted);
            }
        }

        for (int ch = 0; ch < channelCount; ++ch)
        {
            const auto channel = static_cast<size_t> (ch);
            const int row = painted->reverseRows ? channelCount - 1 - ch : ch;
            const float y = firstY + static_cast<float> (row) * rowHeight;
            const auto colour = painted->channelColours[channel];
            text (g, painted->labels[channel], labels.withTrimmedRight (8.0f).withY (y).withHeight (rowHeight),
                  labelSize, painted->drumColors ? colour : Palette::muted, true, juce::Justification::centredLeft);
            g.setColour (backdrop.overlaidWith (Palette::accent.withAlpha (0.05f)));
            g.fillRoundedRectangle (area.withY (y).withHeight (rowHeight).reduced (0, insetY * 0.5f), radius);

            const auto cellFace = [&] (int step, int spanLength)
            {
                return juce::Rectangle<float> (area.getX() + static_cast<float> (step) * cellWidth, y,
                                               static_cast<float> (spanLength) * cellWidth, rowHeight).reduced (insetX, insetY);
            };
            for (int step = 0; step < stepCount; ++step)
            {
                const auto cell = cellFace (step, 1);
                const bool beatStart = step % 4 == 0;
                if (narrowCells)
                {
                    g.setColour (beatStart ? quietBeat : quietSlot);
                    g.fillRoundedRectangle (cell, radius);
                }
                else
                    paintGridSlot (g, cell, backdrop, beatStart, radius);
            }

            for (int step = 0; step < stepCount; ++step)
            {
                const auto index = static_cast<size_t> (step);
                const auto& hit = painted->hits[channel][index];
                if (hit.velocity == 0)
                    continue;

                const auto cell = cellFace (step, hit.span);
                paintGridHitFill (g, cell, colour, hit.velocity, radius);
                const float splitWidth = cell.getWidth() / static_cast<float> (hit.roll);
                if (hit.roll > 1 && splitWidth >= 2.0f)
                {
                    const juce::Graphics::ScopedSaveState clip (g);
                    juce::Path face;
                    face.addRoundedRectangle (cell, radius);
                    g.reduceClipRegion (face);
                    for (size_t part = 0; part < hit.roll; ++part)
                    {
                        const auto split = cell.withX (cell.getX() + static_cast<float> (part) * splitWidth).withWidth (splitWidth);
                        if (const auto value = hit.splitVelocities[part]; value > 0 && value != hit.velocity)
                            paintGridHitFill (g, split, colour, value, 0.0f);
                        if (part % 2 != 0)
                        {
                            g.setColour (Palette::app.withAlpha (0.18f));
                            g.fillRect (split);
                        }
                    }
                }
                if (flashes[channel][index])
                {
                    g.setColour (colour.brighter (0.5f).withAlpha (0.55f));
                    g.fillRoundedRectangle (cell, radius);
                }
                if (!narrowCells || hit.span > 1)
                {
                    g.setColour (Palette::text.withAlpha (0.14f));
                    g.drawRoundedRectangle (cell.reduced (0.5f), radius, 1);
                }
            }
        }
    }

    void PreviewPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::app);
        if (randomSection)
        {
            const auto pattern = controller.source() < 0 ? juce::String ("Muted Pattern")
                                                        : "Pattern " + juce::String (controller.source() + 1);
            if (compact)
            {
                const PhoneGeneratorLayout phone (getLocalBounds());
                phoneMenu::card (g, phone.preview.toFloat(), pattern);
                phoneMenu::card (g, phone.controls.toFloat());
                paintRandomizerGrid (g, phone.preview.toFloat().reduced (14, 12).withTrimmedTop (26), 36.0f, 11.0f);
            }
            else
            {
                const RandomLabLayout lab (getLocalBounds(), randomizerRightInset);
                surface (g, lab.preview.toFloat());
                surface (g, lab.controls.toFloat());
                auto previewContent = lab.preview.toFloat().reduced (20, 16);
                text (g, pattern, previewContent.removeFromTop (24), 16, Palette::text, true, juce::Justification::centredLeft);
                previewContent.removeFromTop (12);
                paintRandomizerGrid (g, previewContent, 52.0f, 15.0f);
            }
            return;
        }
        if (compact)
        {
            surface (g, getLocalBounds().toFloat().reduced (1));
            return;
        }
        g.setColour (Palette::raised);
        g.drawHorizontalLine (0, 0, static_cast<float> (getWidth()));
        // The same warm/cool branding field as the original, rendered with cached font/vector primitives.
        g.setGradientFill (juce::ColourGradient (rowAccent.value_or (Palette::accent).withAlpha (0.09f), 0, 180,
                                                rowAccent.value_or (Palette::info).withAlpha (0.04f), static_cast<float> (getWidth()), 0, false));
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
                const bool next = controller.sourceFlashes[static_cast<size_t> (ch)][static_cast<size_t> (s)] > now;
                auto& previous = flashes[static_cast<size_t> (ch)][static_cast<size_t> (s)];
                if (next != previous)
                {
                    previous = next;
                    dirty = true;
                }
            }
        if (dirty)
        {
            if (compact)
                repaint();
            else
                repaint (RandomLabLayout (getLocalBounds(), randomizerRightInset).preview);
        }
    }
}
