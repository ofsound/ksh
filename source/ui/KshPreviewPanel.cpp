#include "KshPreviewPanel.h"
namespace ksh::ui
{
    namespace
    {
        const juce::Rectangle<float> wordmarkBounds { 24, 34.68f, 242.88f, 95.04f };
        juce::AffineTransform wordmarkTransform() { return juce::AffineTransform::scale (0.8f).translated (-3.2f, 16.4f); }
        void paintWordmark (juce::Graphics& g)
        {
            const juce::Graphics::ScopedSaveState savedState (g);
            g.addTransform (wordmarkTransform());
            // Outlined from the app icon so letter shapes and spacing never depend on UI fonts.
            static const auto artwork = juce::Drawable::createFromImageData (BinaryData::KshWordmark_svg, BinaryData::KshWordmark_svgSize);
            text (g, "OFSOUND", { 24, 19, 170, 20 }, 13, Palette::text, true, juce::Justification::centredLeft);
            if (artwork != nullptr)
                artwork->drawWithin (g, wordmarkBounds, juce::RectanglePlacement::centred, 1.0f);
        }
    }
    PreviewPanel::PreviewPanel (EditorController& c, bool randomSectionIn) : controller (c), randomSection (randomSectionIn), cycle (c)
    {
        addMouseListener (this, true);
        addAndMakeVisible (mode);
        addAndMakeVisible (refresh);
        addAndMakeVisible (velocity);
        addAndMakeVisible (probability);
        addAndMakeVisible (roll);
        addAndMakeVisible (nudge);
        addAndMakeVisible (remove);
        addAndMakeVisible (cycle);
        addChildComponent (cycleLength);
        cycleLength.caption = "Length";
        cycleLength.setTitle ("Selected cycle length");
        cycleLength.setTooltip ("Set the cycle length of selected cells from 1 to 8 beats");
        cycleLength.minimum = 1;
        cycleLength.maximum = 8;
        cycleLength.resetValue = 1;
        cycleLength.sensitivity = 12;
        cycleLength.onBegin = [this] { controller.begin ("Cycle length"); };
        cycleLength.onChange = [this] (int v)
        {
            const auto cells = controller.selection (true);
            for (auto p : cells)
            {
                const auto cell = controller.cell (p);
                controller.setCycle (v, cell.cycleMask, { p });
            }
            controller.notify();
        };
        cycleLength.onEnd = [this] { controller.commit(); };
        cycleLength.onCancel = [this] { controller.cancel(); };
        mode.onClick = [this] { const auto m = controller.state.playback.generationMode; controller.command ("mode", { m == GenerationMode::staticSource ? "stack" : m == GenerationMode::stack ? "per_channel" : "static" }, "Generation mode"); };
        mode.setTooltip ("Switch generation mode: Off, Stack, or Per Channel");
        refresh.setTitle ("Generation refresh length");
        refresh.setTooltip ("Set how many steps play before the generated pattern refreshes");
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
            lock.setTooltip ("Choose this channel's source pattern; Off lets generation choose");
            lock.format = [] (int v) { return v < 0 ? juce::String ("OFF") : "P" + juce::String (v + 1); };
            lock.onBegin = [this] { controller.begin ("Channel lock"); };
            lock.onChange = [this, ch] (int v) { controller.command ("channel_lock", v < 0 ? nlohmann::json::array ({ ch + 1, "random" }) : nlohmann::json::array ({ ch + 1, v + 1 })); };
            lock.onEnd = [this] { controller.commit(); };
        }
        velocity.setTitle ("Selected velocity offset");
        probability.setTitle ("Selected probability offset");
        roll.setTitle ("Selected roll");
        velocity.setTooltip ("Adjust selected cell velocities together, preserving their differences");
        probability.setTooltip ("Adjust selected cell probabilities together, preserving their differences");
        roll.setTooltip ("Set the number of repeated hits per step for selected cells");
        velocity.caption = "Velocity";
        probability.caption = "Probability";
        roll.caption = "Roll";
        nudge.caption = "Nudge";
        nudge.setTitle ("Selected timing nudge");
        nudge.setTooltip ("Move selected hits earlier (-) or later (+) by a percentage of one step, preserving their timing differences. Zero places the first selected hit on the beat.");
        nudge.minimum = -100;
        nudge.maximum = 100;
        nudge.resetValue = 0;
        nudge.sensitivity = 2;
        nudge.format = [] (int v) { return (v > 0 ? "+" : "") + juce::String (v) + "%"; };
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
            control->showAdjustmentHintAtZero = true;
        roll.minimum = 1;
        roll.maximum = 8;
        roll.resetValue = 1;
        velocity.onBegin = [this] { beginRelative ("Selected velocity"); };
        probability.onBegin = [this] { beginRelative ("Selected probability"); };
        roll.onBegin = [this] { controller.begin ("Selected roll"); };
        velocity.onChange = [this] (int v) { applyRelative (Layer::velocity, v); };
        probability.onChange = [this] (int v) { applyRelative (Layer::chance, v); };
        roll.onChange = [this] (int v) { for (auto p : controller.selection (true)) { auto cell = controller.cell (p); cell.roll = juce::jlimit (1, 8, v); controller.editCell (p, cell); } controller.notify(); };
        for (auto [control, layer] : { std::pair { &velocity, Layer::velocity }, std::pair { &probability, Layer::chance } })
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
        remove.setTooltip ("Delete selected cells");
        remove.onClick = [this] { controller.deleteSelection(); };
        cycle.locations = [this] { return controller.selection (true); };
        cycle.inlineLayout = !randomSection;
        for (auto* control : { &velocity, &probability, &roll, &nudge })
        {
            control->valueFontSize = 22;
            control->captionFontSize = 12;
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
        removeMouseListener (this);
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
        for (auto [control, layer] : { std::pair<juce::Component*, Layer> { &velocity, Layer::velocity },
                 { &probability, Layer::chance }, { &cycle, Layer::chance }, { &cycleLength, Layer::chance } })
        {
            auto bounds = control->getBounds();
            if (control == &cycle && !compact)
                bounds = bounds.withTop (32);
            if (control->isVisible() && control->isEnabled() && bounds.contains (position))
            {
                setHoverLayer (layer);
                return;
            }
        }
        setHoverLayer (std::nullopt);
    }
    void PreviewPanel::mouseEnter (const juce::MouseEvent& event) { updateHoverLayer (event.getEventRelativeTo (this).getPosition()); }
    void PreviewPanel::mouseMove (const juce::MouseEvent& event) { updateHoverLayer (event.getEventRelativeTo (this).getPosition()); }
    void PreviewPanel::mouseExit (const juce::MouseEvent& event) { updateHoverLayer (event.getEventRelativeTo (this).getPosition()); }
    void PreviewPanel::visibilityChanged()
    {
        if (!isVisible())
            setHoverLayer (std::nullopt);
    }
    void PreviewPanel::mouseDoubleClick (const juce::MouseEvent& event)
    {
        if (!randomSection && event.mods.isLeftButtonDown()
            && wordmarkBounds.contains (event.position.transformedBy (wordmarkTransform().inverted()))
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
        cycleLength.setVisible (compact && !randomSection);
        for (auto* control : { &velocity, &probability, &roll, &nudge })
        {
            control->valueFontSize = compact ? 17.0f : 22.0f;
            control->captionFontSize = compact ? 11.0f : 12.0f;
            control->captionGap = compact ? 2 : 6;
        }
        resized();
        repaint();
    }
    void PreviewPanel::cancelGesture()
    {
        setHoverLayer (std::nullopt);
        for (auto* control : { &velocity, &probability, &roll, &nudge, &cycleLength, &refresh })
            control->cancelGesture();
        for (auto& lock : locks)
            lock.cancelGesture();
        cycle.cancelGesture();
    }
    void PreviewPanel::resized()
    {
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
            nudge.setBounds (6, 8, 64, 64);
            velocity.setBounds (76, 8, 82, 64);
            probability.setBounds (166, 8, 88, 64);
            roll.setBounds (262, 8, 56, 64);
            cycleLength.setBounds (326, 8, 56, 64);
            remove.setBounds (getWidth() - 48, 28, 42, 44);
            cycle.setBounds (390, 26, juce::jlimit (0, cycle.preferredWidth (48), remove.getX() - 398), 48);
            return;
        }
        mode.setBounds (378, 44, 164, 28);
        refresh.setBounds (566, 23, 82, 49);
        for (int ch = 0; ch < 8; ++ch)
            locks[static_cast<size_t> (ch)].setBounds (378 + ch * ((getWidth() - 408) / 8), 106, (getWidth() - 408) / 8 - 8, 48);
        if (randomSection || inspectorBounds.isEmpty())
            return;
        const int x = inspectorBounds.getX();
        constexpr int inputHeight = 42;
        constexpr int captionHeight = 24;
        nudge.setBounds (x + 20, 32, 94, captionHeight + inputHeight);
        velocity.setBounds (x + 130, 32, 76, captionHeight + inputHeight);
        probability.setBounds (x + 222, 32, 92, captionHeight + inputHeight);
        roll.setBounds (x + 330, 32, 56, captionHeight + inputHeight);
        remove.setBounds (inspectorBounds.getRight() - 96, 56, 76, inputHeight);
        cycle.setBounds (x + 402, 52, juce::jlimit (0, cycle.preferredWidth (inputHeight + 8), remove.getX() - x - 418), inputHeight + 8);
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
        cycleLength.setEnabled (active);
        cycleLength.setValue (active ? controller.cell (selected.front()).cycle : 1);
        cycleLength.mixed = active && std::any_of (selected.begin(), selected.end(), [&] (auto loc) { return controller.cell (loc).cycle != controller.cell (selected.front()).cycle; });
        const float inspectorAlpha = active ? 1.0f : 0.25f;
        cycleLength.setAlpha (inspectorAlpha);
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
            else
            {
                text (g, "Cycle beats", { 400, 8, 160, 18 }, 11, Palette::text, true, juce::Justification::centredLeft);
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
            text (g, "Cycle", cycle.getBounds().withY (32).withHeight (18).toFloat(), 12, labelColour, true, juce::Justification::centredLeft);
            text (g, "Delete", remove.getBounds().withY (32).withHeight (18).toFloat(), 12, labelColour, true, juce::Justification::centredLeft);
        }
        else
        {
            surface (g, { 360, 10, static_cast<float> (getWidth() - 372), 160 });
            text (g, "Random Mode", { 378, 20, 164, 20 }, 10, Palette::text, true, juce::Justification::centredLeft);
            text (g, "Channel Lock", { 378, 82, 300, 20 }, 10, Palette::text, true, juce::Justification::centredLeft);
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
                                                                                                                                       : s % 4 == 0                                                                   ? Palette::app
                                                                                                                                                                                                                      : Palette::surface;
                g.setColour (colour);
                g.fillRoundedRectangle (60 + static_cast<float> (s) * cw, 8 + static_cast<float> (row) * 18, cw - 3, 15, 3);
            }
        }
        if (randomSection)
        {
            const float width = static_cast<float> (getWidth() - 64);
            surface (g, { 24, 190, width + 16, static_cast<float> (getHeight() - 214) });
            text (g, "KSH / ofsound", { 48, 212, width, 44 }, 32, Palette::text, true, juce::Justification::centredLeft);
            text (g, "Rhythm, with a little mischief.", { 48, 268, width, 28 }, 18, Palette::accent, false, juce::Justification::centredLeft);
            text (g, "Made by ofsound. Built with JUCE.", { 48, 314, width, 24 }, 13, Palette::muted, false, juce::Justification::centredLeft);
            text (g, "You found the random laboratory. Try a mode, set the length, lock a channel. Leave the rest to chance.", { 48, 354, width, 28 }, 13, Palette::text, false, juce::Justification::centredLeft);
            text (g, "Double-click KSH again or press Escape to return.", { 48, static_cast<float> (getHeight() - 62), width, 24 }, 12, Palette::muted, false, juce::Justification::centredLeft);
            return;
        }
        paintWordmark (g);
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
