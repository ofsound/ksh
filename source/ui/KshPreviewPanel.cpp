#include "KshPreviewPanel.h"
namespace ksh::ui
{
    namespace
    {
        void paintWordmark (juce::Graphics& g)
        {
            const juce::Graphics::ScopedSaveState savedState (g);
            g.addTransform (juce::AffineTransform::scale (0.8f).translated (-3.2f, 16.4f));
            // Broad, hand-shaped letters with rounded corners and a short solid extrusion.
            static const auto letters = []
            {
                std::array<juce::Path, 3> paths;
                auto& k = paths[0];
                k.startNewSubPath (16, 12);
                k.lineTo (16, 86);
                k.startNewSubPath (64, 10);
                k.lineTo (18, 52);
                k.lineTo (68, 86);
                juce::Path kFace;
                juce::PathStrokeType (25, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (kFace, k);
                k = kFace;

                auto& s = paths[1];
                s.startNewSubPath (65, 17);
                s.cubicTo (40, 1, 9, 9, 14, 33);
                s.cubicTo (17, 49, 67, 44, 66, 66);
                s.cubicTo (65, 92, 30, 99, 10, 79);
                juce::Path sFace;
                juce::PathStrokeType (24, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (sFace, s);
                s = sFace;

                auto& h = paths[2];
                h.addRoundedRectangle (3, 0, 25, 98, 7);
                h.addRoundedRectangle (54, 0, 25, 98, 7);
                h.addRoundedRectangle (20, 36, 42, 25, 5);
                return paths;
            }();
            text (g, "OFSOUND", { 24, 19, 170, 20 }, 13, Palette::text, true, juce::Justification::centredLeft);
            const std::array<juce::Colour, 3> colours { Palette::accent, Palette::text, Palette::info };
            const std::array<float, 3> angles { -0.045f, 0.035f, -0.025f };
            for (size_t i = 0; i < letters.size(); ++i)
            {
                const auto transform = juce::AffineTransform::rotation (angles[i], 40, 49).translated (24.0f + static_cast<float> (i) * 92.0f, 45);
                for (int depth = 5; depth > 0; --depth)
                {
                    g.setColour (colours[i].darker (0.65f));
                    g.fillPath (letters[i], transform.translated (static_cast<float> (depth), static_cast<float> (depth)));
                }
                g.setColour (colours[i]);
                g.fillPath (letters[i], transform);
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
        remove.setTooltip ("Delete selected cells");
        remove.onClick = [this] { controller.deleteSelection(); };
        cycle.locations = [this] { return controller.selection (true); };
        cycle.inlineLayout = !randomSection;
        for (auto* control : { &velocity, &probability, &roll })
        {
            control->valueFontSize = 22;
            control->captionFontSize = 12;
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
        velocity.setBounds (x + 130, 38, 76, 80);
        probability.setBounds (x + 222, 38, 92, 80);
        roll.setBounds (x + 330, 38, 56, 80);
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
        for (auto* control : { &velocity, &probability, &roll })
            control->setEnabled (active);
        remove.setEnabled (active);
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
            text (g, juce::String (controller.selection (true).size()) + " Cells", { static_cast<float> (inspectorBounds.getX() + 20), 56, 100, 62 }, 22, Palette::text, true, juce::Justification::centredLeft);
            text (g, "Cycle", cycle.getBounds().withY (38).withHeight (18).toFloat(), 12, Palette::text, true, juce::Justification::centredLeft);
            text (g, "Delete", remove.getBounds().withY (38).withHeight (18).toFloat(), 12, Palette::text, true, juce::Justification::centredLeft);
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
