#include "KshPreviewPanel.h"
namespace ksh::ui
{
    namespace
    {
        void paintWordmark (juce::Graphics& g)
        {
            const juce::Graphics::ScopedSaveState savedState (g);
            g.addTransform (juce::AffineTransform::scale (0.8f).translated (-3.2f, 16.4f));
            // Chiseled silhouettes keep the mark readable at small sizes without a font dependency.
            static const auto artwork = []
            {
                struct Artwork
                {
                    std::array<juce::Path, 3> faces, highlights, cuts;
                } result;
                const auto polygon = [] (juce::Path& path, std::initializer_list<juce::Point<float>> points)
                {
                    auto point = points.begin();
                    path.startNewSubPath (*point++);
                    for (; point != points.end(); ++point)
                        path.lineTo (*point);
                    path.closeSubPath();
                };
                polygon (result.faces[0], { { 5, 4 }, { 30, 0 }, { 28, 34 }, { 56, 4 }, { 83, 8 }, { 50, 45 }, { 80, 81 }, { 55, 98 }, { 28, 62 }, { 24, 96 }, { 0, 91 }, { 6, 46 }, { 1, 38 } });
                polygon (result.faces[1], { { 80, 3 }, { 72, 26 }, { 38, 22 }, { 25, 30 }, { 36, 39 }, { 62, 41 }, { 78, 57 }, { 72, 84 }, { 50, 99 }, { 7, 91 }, { 0, 69 }, { 39, 78 }, { 52, 70 }, { 44, 62 }, { 17, 57 }, { 2, 38 }, { 9, 14 }, { 36, 1 }, { 58, 7 }, { 63, 0 } });
                polygon (result.faces[2], { { 7, 3 }, { 31, 0 }, { 27, 35 }, { 53, 30 }, { 58, 1 }, { 82, 7 }, { 69, 96 }, { 44, 100 }, { 49, 58 }, { 24, 64 }, { 19, 96 }, { 0, 88 } });
                polygon (result.highlights[0], { { 9, 9 }, { 24, 6 }, { 20, 31 }, { 18, 13 } });
                polygon (result.highlights[0], { { 58, 11 }, { 72, 12 }, { 43, 39 } });
                polygon (result.highlights[1], { { 13, 18 }, { 36, 7 }, { 60, 13 }, { 36, 12 } });
                polygon (result.highlights[1], { { 8, 77 }, { 38, 85 }, { 49, 81 }, { 39, 90 }, { 12, 86 } });
                polygon (result.highlights[2], { { 11, 8 }, { 24, 6 }, { 17, 31 }, { 18, 12 } });
                polygon (result.highlights[2], { { 62, 7 }, { 74, 11 }, { 65, 36 }, { 67, 14 } });
                polygon (result.cuts[0], { { 0, 67 }, { 14, 73 }, { 0, 73 } });
                polygon (result.cuts[0], { { 66, 68 }, { 53, 68 }, { 70, 75 } });
                polygon (result.cuts[1], { { 72, 18 }, { 58, 17 }, { 74, 13 } });
                polygon (result.cuts[1], { { 7, 43 }, { 21, 48 }, { 11, 51 } });
                polygon (result.cuts[2], { { 1, 73 }, { 13, 77 }, { 0, 80 } });
                polygon (result.cuts[2], { { 75, 61 }, { 62, 58 }, { 75, 54 } });
                return result;
            }();
            text (g, "OFSOUND", { 24, 19, 170, 20 }, 13, Palette::text, true, juce::Justification::centredLeft);
            const std::array<juce::Colour, 3> colours { Palette::accent, Palette::text, Palette::info };
            const std::array<float, 3> angles { -0.11f, 0.07f, -0.04f };
            for (size_t i = 0; i < artwork.faces.size(); ++i)
            {
                const auto transform = juce::AffineTransform::rotation (angles[i], 40, 49).translated (24.0f + static_cast<float> (i) * 92.0f, 45);
                for (int depth = 5; depth > 0; --depth)
                {
                    g.setColour (colours[i].darker (0.65f));
                    g.fillPath (artwork.faces[i], transform.translated (static_cast<float> (depth), static_cast<float> (depth)));
                }
                g.setColour (Palette::app);
                g.strokePath (artwork.faces[i], juce::PathStrokeType (3.5f, juce::PathStrokeType::mitered), transform);
                g.setColour (colours[i]);
                g.fillPath (artwork.faces[i], transform);
                g.setColour (colours[i].interpolatedWith (Palette::text, 0.48f));
                g.fillPath (artwork.highlights[i], transform);
                g.setColour (Palette::app);
                g.fillPath (artwork.cuts[i], transform);
            }
        }
    }
    PreviewPanel::PreviewPanel (EditorController& c, bool randomSectionIn) : controller (c), randomSection (randomSectionIn), cycle (c)
    {
        addAndMakeVisible (mode);
        addAndMakeVisible (refresh);
        addAndMakeVisible (velocity);
        addAndMakeVisible (probability);
        addAndMakeVisible (roll);
        addAndMakeVisible (remove);
        addAndMakeVisible (cycle);
        mode.onClick = [this] { const auto m = controller.state.playback.generationMode; controller.command ("mode", { m == GenerationMode::staticSource ? "stack" : m == GenerationMode::stack ? "per_channel" : "static" }, "Generation mode"); };
        refresh.setTitle ("Generation refresh length");
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
            lock.format = [] (int v) { return v < 0 ? juce::String ("OFF") : "P" + juce::String (v + 1); };
            lock.onBegin = [this] { controller.begin ("Channel lock"); };
            lock.onChange = [this, ch] (int v) { controller.command ("channel_lock", v < 0 ? nlohmann::json::array ({ ch + 1, "random" }) : nlohmann::json::array ({ ch + 1, v + 1 })); };
            lock.onEnd = [this] { controller.commit(); };
        }
        velocity.setTitle ("Selected velocity offset");
        probability.setTitle ("Selected probability offset");
        roll.setTitle ("Selected roll");
        velocity.caption = "Velocity";
        probability.caption = "Probability";
        roll.caption = "Roll";
        velocity.minimum = -127;
        velocity.maximum = 127;
        probability.minimum = -100;
        probability.maximum = 100;
        for (auto* control : { &velocity, &probability })
            control->format = [] (int v) { return v == 0 ? juce::String::fromUTF8 ("↕") : juce::String (v); };
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
        for (auto* control : { &velocity, &probability, &roll })
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
        remove.getProperties().set ("icon", "clear");
        remove.setTooltip ("Delete selected cells");
        remove.onClick = [this] { controller.deleteSelection(); };
        cycle.locations = [this] { return controller.selection (true); };
        cycle.inlineLayout = !randomSection;
        for (auto* control : { &velocity, &probability, &roll })
        {
            control->valueFontSize = 22;
            control->captionFontSize = 12;
            control->captionGap = 6;
        }
        mode.setVisible (randomSection);
        refresh.setVisible (randomSection);
        for (auto& lock : locks)
            lock.setVisible (randomSection);
        for (auto* control : { &velocity, &probability, &roll })
            control->setVisible (!randomSection);
        remove.setVisible (!randomSection);
        cycle.setVisible (!randomSection);
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
    void PreviewPanel::resized()
    {
        mode.setBounds (378, 44, 164, 28);
        refresh.setBounds (566, 23, 82, 49);
        for (int ch = 0; ch < 8; ++ch)
            locks[static_cast<size_t> (ch)].setBounds (378 + ch * ((getWidth() - 408) / 8), 106, (getWidth() - 408) / 8 - 8, 48);
        if (randomSection || inspectorBounds.isEmpty())
            return;
        const int x = inspectorBounds.getX();
        velocity.setBounds (x + 130, 32, 76, 86);
        probability.setBounds (x + 222, 32, 92, 86);
        roll.setBounds (x + 330, 32, 56, 86);
        remove.setBounds (inspectorBounds.getRight() - 96, 56, 76, 62);
        cycle.setBounds (x + 402, 56, remove.getX() - x - 418, 62);
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
        const float inspectorAlpha = active ? 1.0f : 0.25f;
        for (auto* control : { &velocity, &probability, &roll })
        {
            control->setEnabled (active);
            control->setAlpha (inspectorAlpha);
        }
        remove.setEnabled (active);
        remove.setAlpha (inspectorAlpha);
        cycle.setAlpha (inspectorAlpha);
        if (active)
        {
            const int value = controller.cell (selected.front()).roll;
            roll.setValue (value);
            roll.mixed = std::any_of (selected.begin(), selected.end(), [&] (auto loc) { return controller.cell (loc).roll != value; });
        }
        else
            roll.mixed = true;
        cycle.sync();
        repaint();
    }
    void PreviewPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::app);
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
            if (selectedCount > 0)
                text (g, juce::String (selectedCount) + " Cells", { static_cast<float> (inspectorBounds.getX() + 20), 56, 100, 62 }, 22, labelColour, true, juce::Justification::centredLeft);
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
            text (g, state.channels[static_cast<size_t> (ch)].label, { 8, 8 + static_cast<float> (ch) * 18, 48, 15 }, 9, Palette::muted, false, juce::Justification::centredLeft);
            for (int s = 0; s < state.stepCount; ++s)
            {
                const auto colour = controller.generatedFlashes[static_cast<size_t> (ch)][static_cast<size_t> (s)] > now ? Palette::text : state.generated[static_cast<size_t> (ch)][static_cast<size_t> (s)].enabled ? Palette::info
                                                                                                                                       : s % 4 == 0                                                                   ? Palette::app
                                                                                                                                                                                                                      : Palette::surface;
                g.setColour (colour);
                g.fillRoundedRectangle (60 + static_cast<float> (s) * cw, 8 + static_cast<float> (ch) * 18, cw - 3, 15, 3);
            }
        }
        if (randomSection)
            return;
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
            repaint (0, 0, 350, 170);
    }
}
