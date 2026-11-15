#include "KshPreviewPanel.h"
#include "KshLayout.h"
namespace ksh::ui
{
    namespace
    {
        const juce::Rectangle<float> wordmarkBounds { 24, 34.68f, 242.88f, 95.04f };
        const juce::Rectangle<float> makerBounds { 24, 19, 170, 20 };
        juce::AffineTransform wordmarkTransform (juce::Rectangle<float> bounds)
        {
            constexpr float margin = 16.0f;
            constexpr float wordmarkScale = 0.9f;
            const float scale = wordmarkScale * juce::jmax (0.0f, bounds.getWidth() - 2.0f * margin) / wordmarkBounds.getWidth();
            const auto brandingBounds = wordmarkBounds.getUnion (makerBounds);
            return juce::AffineTransform::scale (scale).translated (bounds.getCentreX() - brandingBounds.getCentreX() * scale,
                                                                    bounds.getCentreY() - brandingBounds.getCentreY() * scale);
        }
        juce::Rectangle<float> brandingBoundsForInspector (juce::Rectangle<int> inspector)
        {
            return inspector.toFloat().withX (0).withWidth (static_cast<float> (inspector.getX()));
        }
    }

    void PreviewPanel::paintBranding (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        const juce::Graphics::ScopedSaveState savedState (g);
        g.addTransform (wordmarkTransform (bounds));
        // Outlined from the app icon so letter shapes and spacing never depend on UI fonts.
        static const auto artwork = juce::Drawable::createFromImageData (BinaryData::KshWordmark_svg, BinaryData::KshWordmark_svgSize);
        text (g, "OFSOUND", makerBounds, 13, Palette::text, true, juce::Justification::centredLeft);
        if (artwork == nullptr)
            return;

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
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillPath (outline, juce::AffineTransform::translation (0, 1.5f));
            artwork->drawWithin (g, wordmarkBounds, juce::RectanglePlacement::centred, 1.0f);

            juce::ColourGradient finish (juce::Colours::white.withAlpha (0.12f), wordmarkBounds.getTopLeft(),
                                         juce::Colours::black.withAlpha (0.14f), wordmarkBounds.getBottomLeft(), false);
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
        // Observe child hover without delivering our own double-click handler
        // a second time through Component's mouse-listener dispatch.
        addMouseListener (&childHoverListener, true);
        if (!randomSection)
        {
            getProperties().set ("hideDisabledHelp", true);
            for (auto* button : { &orangeLogo, &blueLogo })
            {
                addAndMakeVisible (*button);
                button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
            }
            orangeLogo.setTitle ("Orange accent");
            blueLogo.setTitle ("Blue accent");
            orangeLogo.setTooltip ("Orange accent. Double-click for controls.");
            blueLogo.setTooltip ("Blue accent. Double-click for controls.");
            orangeLogo.onClick = [this] { if (onAccentSelected) onAccentSelected (false); };
            blueLogo.onClick = [this] { if (onAccentSelected) onAccentSelected (true); };
        }
        addAndMakeVisible (mode);
        addAndMakeVisible (refresh);
        addAndMakeVisible (velocity);
        addAndMakeVisible (probability);
        addAndMakeVisible (roll);
        addAndMakeVisible (nudge);
        addAndMakeVisible (remove);
        addAndMakeVisible (cycle);
        mode.onClick = [this] { const auto m = controller.state.playback.generationMode; controller.command ("mode", { m == GenerationMode::staticSource ? "stack" : m == GenerationMode::stack ? "per_channel" : "static" }, "Generation mode"); };
        refresh.setTitle ("Generation refresh length");
        refresh.setTooltip ("Steps between generations");
        refresh.caption = "Length";
        refresh.minimum = 1;
        refresh.maximum = 32;
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
            lock.setTooltip ("Off lets generation pick a pattern");
            lock.format = [] (int v) { return v < 0 ? juce::String ("OFF") : "P" + juce::String (v + 1); };
            lock.onBegin = [this] { controller.begin ("Channel lock"); };
            lock.onChange = [this, ch] (int v) { controller.command ("channel_lock", v < 0 ? nlohmann::json::array ({ ch + 1, "random" }) : nlohmann::json::array ({ ch + 1, v + 1 })); };
            lock.onEnd = [this] { controller.commit(); };
        }
        velocity.setTitle ("Selected velocity offset");
        probability.setTitle ("Selected probability offset");
        roll.setTitle ("Selected roll");
        velocity.setTooltip ("Offsets selected velocities together");
        probability.setTooltip ("Offsets selected probabilities together");
        velocity.caption = "Velocity";
        probability.caption = "Probability";
        roll.caption = "Roll";
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
        roll.onBegin = [this] { controller.begin ("Selected roll"); };
        velocity.onChange = [this] (int v) { applyRelative (Layer::velocity, v); };
        probability.onChange = [this] (int v) { applyRelative (Layer::chance, v); };
        roll.onChange = [this] (int v) { for (auto p : controller.selection (true)) { auto cell = controller.cell (p); cell.roll = juce::jlimit (1, 8, v); controller.editCell (p, cell); } controller.notify(); };
        velocity.onDoubleClick = [this] { resetSelectedValue ("Reset velocity", &Cell::velocity, CellDefaults::velocity); };
        probability.onDoubleClick = [this] { resetSelectedValue ("Reset probability", &Cell::probability, CellDefaults::probability); };
        nudge.onDoubleClick = [this] { resetSelectedValue ("Reset nudge", &Cell::nudge, CellDefaults::nudge); };
        roll.onDoubleClick = [this] { resetSelectedValue ("Reset roll", &Cell::roll, CellDefaults::roll); };
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
    void PreviewPanel::mouseDoubleClick (const juce::MouseEvent& event)
    {
        if (!randomSection && event.mods.isLeftButtonDown()
            && wordmarkBounds.contains (event.position.transformedBy (wordmarkTransform (brandingBoundsForInspector (inspectorBounds)).inverted()))
            && onLogoDoubleClick)
            onLogoDoubleClick();
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
    void PreviewPanel::setCompact (bool enabled)
    {
        compact = enabled;
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
        setHoverLayer (std::nullopt);
        for (auto* control : { &velocity, &probability, &roll, &nudge, &refresh })
            control->cancelGesture();
        for (auto& lock : locks)
            lock.cancelGesture();
        cycle.cancelGesture();
    }
    void PreviewPanel::resized()
    {
        orangeLogo.setVisible (!compact && !randomSection);
        blueLogo.setVisible (!compact && !randomSection);
        if (!compact && !randomSection)
        {
            const auto transform = wordmarkTransform (brandingBoundsForInspector (inspectorBounds));
            const float unit = wordmarkBounds.getWidth() / 556.632813f;
            orangeLogo.setBounds (wordmarkBounds.withWidth (197.021484f * unit).transformedBy (transform).getSmallestIntegerContainer());
            blueLogo.setBounds (wordmarkBounds.withTrimmedLeft (358.0f * unit).transformedBy (transform).getSmallestIntegerContainer());
        }
        if (compact)
        {
            if (randomSection)
            {
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
            remove.setBounds (getWidth() - 42, 4, 36, 40);
            cycle.setBounds (inputX - 4, 2, juce::jlimit (0, cycle.preferredWidth (44), remove.getX() - inputX), 44);
            return;
        }
        mode.setBounds (378, 40, 180, 64);
        refresh.setBounds (574, 20, 144, 84);
        for (int ch = 0; ch < 8; ++ch)
            {
            auto& lock = locks[static_cast<size_t> (ch)];
            lock.setBounds (378 + ch * ((getWidth() - 408) / 8), 140, (getWidth() - 408) / 8 - 8, 84);
            lock.captionFontSize = 16;
            lock.captionGap = 2;
            lock.valueFontSize = 22;
        }
        refresh.captionFontSize = 16;
        refresh.captionGap = 2;
        refresh.valueFontSize = 22;
        if (randomSection || inspectorBounds.isEmpty())
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
        cycle.setBounds (inputX, inputY - 4, juce::jlimit (0, cycle.preferredWidth (inputHeight + 8), remove.getX() - inputX - 16), inputHeight + 8);
    }
    void PreviewPanel::sync()
    {
        const auto& p = controller.state.playback;
        mode.setButtonText (p.generationMode == GenerationMode::staticSource ? "Off" : p.generationMode == GenerationMode::stack ? "Stack"
                                                                                                                                 : "Per Channel");
        refresh.maximum = p.stepCount;
        refresh.setValue (controller.state.refreshSteps);
        for (int ch = 0; ch < 8; ++ch)
        {
            locks[static_cast<size_t> (ch)].setValue (p.channels[static_cast<size_t> (ch)].lock);
            locks[static_cast<size_t> (ch)].setEnabled (ch < p.channelCount);
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
            next.channelCount = p.channelCount;
            next.stepCount = p.stepCount;
            for (size_t ch = 0; ch < next.labels.size(); ++ch)
            {
                next.labels[ch] = p.channels[ch].label;
                for (size_t s = 0; s < next.generated[ch].size(); ++s)
                    next.generated[ch][s] = p.generated[ch][s].enabled;
            }
        }
        if (painted != next)
        {
            painted = std::move (next);
            repaint();
        }
    }
    void PreviewPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::app);
        if (compact)
        {
            surface (g, getLocalBounds().toFloat().reduced (1));
            if (randomSection)
            {
                text (g, "Generation mode", { 18, 8, 180, 18 }, 12, Palette::text, true, juce::Justification::centredLeft);
                text (g, "Channel pattern locks", { 18, 78, 300, 18 }, 12, Palette::muted, true, juce::Justification::centredLeft);
                text (g, "OFF lets the generator choose a pattern. Drag a lock to pin a channel to P1-P16.", { 18, 180, static_cast<float> (getWidth() - 36), 32 }, 12, Palette::muted, false, juce::Justification::centredLeft);
                const auto& state = controller.state.playback;
                const auto now = juce::Time::getMillisecondCounterHiRes();
                const float cw = static_cast<float> (getWidth() - 100) / static_cast<float> (state.stepCount);
                for (int ch = 0; ch < state.channelCount; ++ch)
                {
                    const int row = reverseRows ? state.channelCount - 1 - ch : ch;
                    const float y = 224.0f + static_cast<float> (row) * 26.0f;
                    text (g, state.channels[static_cast<size_t> (ch)].label, { 12, y, 78, 22 }, 11, Palette::muted, false, juce::Justification::centredLeft);
                    for (int step = 0; step < state.stepCount; ++step)
                    {
                        const auto index = static_cast<size_t> (step);
                        g.setColour (controller.generatedFlashes[static_cast<size_t> (ch)][index] > now ? Palette::text : state.generated[static_cast<size_t> (ch)][index].enabled ? Palette::info : Palette::surface);
                        g.fillRoundedRectangle (92.0f + static_cast<float> (step) * cw, y, cw - 3.0f, 22, 3);
                    }
                }
            }
            return;
        }
        g.setColour (Palette::raised);
        g.drawHorizontalLine (0, 0, static_cast<float> (getWidth()));
        // The same warm/cool branding field as the original, rendered with cached font/vector primitives.
        g.setGradientFill (juce::ColourGradient (Palette::accent.withAlpha (0.09f), 0, 180, Palette::info.withAlpha (0.04f), static_cast<float> (getWidth()), 0, false));
        g.fillAll();
        if (!randomSection)
        {
            surface (g, inspectorBounds.toFloat());
            const auto selectedCount = controller.selection (true).size();
            const auto labelColour = Palette::text.withAlpha (selectedCount == 0 ? 0.25f : 1.0f);
            text (g, "Cycle beats", cycle.getBounds().withY (velocity.getY()).withHeight (18).toFloat(), velocity.captionFontSize, labelColour, true, juce::Justification::centredLeft);
            text (g, "Delete", remove.getBounds().withY (velocity.getY()).withHeight (18).withWidth (76).toFloat(), velocity.captionFontSize, labelColour, true, juce::Justification::centredLeft);
        }
        else
        {
            surface (g, { 360, 10, static_cast<float> (getWidth() - 372), 224 });
            text (g, "Random Mode", { 378, 16, 180, 20 }, 16, Palette::text, true, juce::Justification::centredLeft);
            text (g, "Channel Lock", { 378, 114, 300, 20 }, 16, Palette::text, true, juce::Justification::centredLeft);
        }
        const auto& state = controller.state.playback;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const float cw = state.stepCount > 16 ? 288.0f / static_cast<float> (state.stepCount) : 18.0f;
        if (randomSection)
        for (int ch = 0; ch < state.channelCount; ++ch)
        {
            const int row = reverseRows ? state.channelCount - 1 - ch : ch;
            text (g, state.channels[static_cast<size_t> (ch)].label, { 8, 8 + static_cast<float> (row) * 18, 48, 15 }, 9, Palette::muted, false, juce::Justification::centredLeft);
            for (int s = 0; s < state.stepCount; ++s)
            {
                const auto colour = controller.generatedFlashes[static_cast<size_t> (ch)][static_cast<size_t> (s)] > now ? Palette::text : state.generated[static_cast<size_t> (ch)][static_cast<size_t> (s)].enabled ? Palette::info
                                                                                                                                                                                                        : Palette::surface;
                g.setColour (colour);
                g.fillRoundedRectangle (60 + static_cast<float> (s) * cw, 8 + static_cast<float> (row) * 18, cw - 3, 15, 3);
            }
        }
        if (randomSection)
        {
            const float width = static_cast<float> (getWidth() - 64);
            surface (g, { 24, 250, width + 16, static_cast<float> (getHeight() - 274) });
            text (g, "KSH / ofsound", { 48, 270, width, 44 }, 32, Palette::text, true, juce::Justification::centredLeft);
            text (g, "Rhythm, with a little mischief.", { 48, 322, width, 28 }, 18, Palette::accent, false, juce::Justification::centredLeft);
            text (g, "Made by ofsound. Built with JUCE.", { 48, 362, width, 24 }, 13, Palette::muted, false, juce::Justification::centredLeft);
            text (g, "You found the random laboratory. Try a mode, set the length, lock a channel. Leave the rest to chance.", { 48, 400, width, 48 }, 13, Palette::text, false, juce::Justification::centredLeft);
            text (g, "Double-click KSH again or press Escape to return.", { 48, static_cast<float> (getHeight() - 62), width, 24 }, 12, Palette::muted, false, juce::Justification::centredLeft);
            return;
        }
        paintBranding (g, brandingBoundsForInspector (inspectorBounds));
    }
    void PreviewPanel::updatePlayback()
    {
        cycle.updatePlayback();
        const auto now = juce::Time::getMillisecondCounterHiRes();
        bool dirty = false;
        for (int ch = 0; ch < 8; ++ch)
            for (int s = 0; s < 32; ++s)
            {
                const bool next = controller.generatedFlashes[static_cast<size_t> (ch)][static_cast<size_t> (s)] > now;
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
                repaint (0, 0, 350, 170);
        }
    }
}
