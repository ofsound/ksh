#include "KshPatternGrid.h"
namespace ksh::ui
{
    namespace
    {
        constexpr float velocityEdgeHitRadius = 6.0f;

        juce::String accessibleDescription (const Cell& value)
        {
            return juce::String (value.enabled ? "On" : "Off") + ", velocity " + juce::String (value.velocity) + ", probability " + juce::String (value.probability) + ", roll " + juce::String (value.roll) + ", cycle " + juce::String (value.cycle);
        }
    }

    PatternGrid::PatternGrid (EditorController& c) : controller (c)
    {
        setWantsKeyboardFocus (true);
        setTitle ("Pattern grid");
        setDescription ("Select cells with Shift-click or a long press, drag to paint or change values; Option copies selected cells.");
        setTooltip ("Click to toggle cells; drag across cells to paint or vertically to change values. Shift/Select or a long press selects cells; Option-drag copies selected cells. Drag an enabled cell's left edge to draw a velocity line.");
        for (int ch = 0; ch < 8; ++ch)
            for (int step = 0; step < 32; ++step)
            {
                const Location location { ch, step };
                auto& cell = accessibleCells[static_cast<size_t> (location.key())];
                addAndMakeVisible (cell);
                cell.setTitle ("Channel " + juce::String (ch + 1) + ", step " + juce::String (step + 1));
                cell.setDescription (accessibleDescription (displayedCells[static_cast<size_t> (location.key())]));
                cell.onClick = [this, location] { controller.begin ("Toggle cell"); auto value = controller.cell (location); value.enabled = ! value.enabled; controller.editCell (location, value); controller.commit(); controller.notify(); };
                cell.navigate = [this, location] (const juce::KeyPress& key) {
                    auto targetCell = location;
                    if (key == juce::KeyPress::leftKey)
                        targetCell.step = wrap (location.step - 1, layout.steps);
                    else if (key == juce::KeyPress::rightKey)
                        targetCell.step = wrap (location.step + 1, layout.steps);
                    else if (key == juce::KeyPress::upKey)
                        targetCell.channel = wrap (location.channel - 1, layout.channels);
                    else if (key == juce::KeyPress::downKey)
                        targetCell.channel = wrap (location.channel + 1, layout.channels);
                    else
                        return false;
                    accessibleCells[static_cast<size_t> (targetCell.key())].grabKeyboardFocus();
                    return true;
                };
            }
        for (int ch = 0; ch < 8; ++ch)
        {
            auto& r = rows[static_cast<size_t> (ch)];
            for (auto* b : { &r.clear, &r.reverse, &r.ping, &r.mute, &r.left, &r.right })
                addAndMakeVisible (*b);
            const std::array<std::pair<juce::TextButton*, const char*>, 6> icons { { { &r.clear, "clear" }, { &r.reverse, "forward" }, { &r.ping, "ping" }, { &r.mute, "power" }, { &r.left, "previous" }, { &r.right, "next" } } };
            for (auto [button, icon] : icons)
            {
                button->getProperties().set ("icon", icon);
                button->setTitle (juce::String (icon) + " channel " + juce::String (ch + 1));
            }
            addAndMakeVisible (r.label);
            addAndMakeVisible (r.note);
            addChildComponent (r.more);
            r.more.setTitle ("Settings channel " + juce::String (ch + 1));
            r.more.setTooltip ("Open this channel's note, loop, and playback settings");
            r.more.onClick = [this, ch] { if (channelSettings) channelSettings (ch); };
            r.label.setEditable (false, true);
            r.label.setFont (font (22, true));
            r.label.setColour (juce::Label::textColourId, Palette::accent);
            r.label.setColour (juce::Label::backgroundWhenEditingColourId, Palette::app);
            r.label.onTextChange = [this, ch, &r] { controller.command ("channel_label", { ch + 1, r.label.getText().toStdString() }, "Rename channel"); };
            r.label.setTooltip ("Click to audition; double-click to rename channel");
            r.label.audition = [this, ch] { (void) controller.processor.triggerRowFromUi (ch); };
            r.clear.setTooltip ("Double-click to clear row; right-click mute to reset row");
            r.clear.onClick = [this, ch, &r] { if (r.clear.getClickingTogglesState()) return; if (juce::Time::getMillisecondCounter() - static_cast<juce::uint32> (static_cast<int> (r.clear.getProperties()["lastClick"])) < 300) controller.clearRow (ch, false); r.clear.getProperties().set ("lastClick", static_cast<int> (juce::Time::getMillisecondCounter())); };
            r.note.setTitle ("MIDI note channel " + juce::String (ch + 1));
            r.note.minimum = 0;
            r.note.maximum = 127;
            r.note.resetValue = 36 + ch;
            r.note.setTooltip (juce::String::fromUTF8 ("Drag to change note • click to switch note names • double-click to type"));
            r.note.format = [this] (int note) { return controller.noteNames ? juce::MidiMessage::getMidiNoteName (note, true, true, 4) : juce::String (note); };
            r.note.onBegin = [this] { controller.begin ("Change channel note"); };
            r.note.onChange = [this, ch] (int v) { controller.command ("channel_note", { ch + 1, v }); };
            r.note.onEnd = [this] { controller.commit(); };
            r.note.onClick = [this] { controller.noteNames = ! controller.noteNames; sync(); };
            r.reverse.setTooltip ("Toggle forward / reverse");
            r.ping.setTooltip ("Toggle ping-pong");
            auto direction = [this, ch] (bool flipReverse) {
                const auto mode = controller.state.playback.channels[static_cast<size_t> (ch)].playbackMode;
                bool rev = mode == PlaybackMode::reverse || mode == PlaybackMode::reverse_ping_pong;
                bool ping = mode == PlaybackMode::ping_pong || mode == PlaybackMode::reverse_ping_pong;
                if (flipReverse)
                    rev = !rev;
                else
                    ping = !ping;
                controller.command ("channel_playback_mode", { ch + 1, rev ? (ping ? "reverse_ping_pong" : "reverse") : (ping ? "ping_pong" : "normal") }, "Playback direction");
            };
            r.reverse.onClick = [direction] { direction (true); };
            r.ping.onClick = [direction] { direction (false); };
            r.mute.setTooltip (juce::String::fromUTF8 ("Mute channel • Shift-click solo • right-click reset row"));
            r.mute.onClick = [this, ch] { auto mods = juce::ModifierKeys::getCurrentModifiersRealtime(); if (mods.isPopupMenu()) controller.clearRow (ch, true); else controller.toggleMute (ch, controller.useSelectionModifier (mods)); };
            r.mute.pressed = [this, ch] (const juce::MouseEvent& e) {
                if (controller.source() < 0)
                    return;
                if (e.mods.isPopupMenu() || e.getNumberOfClicks() == 2)
                {
                    controller.clearRow (ch, true);
                    return;
                }
                if (controller.useSelectionModifier (e.mods))
                {
                    controller.toggleMute (ch, true);
                    return;
                }
                mutePainting = true;
                muteValue = !controller.state.playback.sourceChannelMutes[static_cast<size_t> (controller.source())][static_cast<size_t> (ch)];
                controller.begin ("Paint channel mutes");
                controller.command ("source_channel_mute", { controller.source() + 1, ch + 1, muteValue });
            };
            r.mute.dragged = [this] (const juce::MouseEvent& e) {
                if (!mutePainting)
                    return;
                const int row = static_cast<int> (std::floor ((e.getEventRelativeTo (this).position.y - layout.cellsY()) / layout.rowHeight()));
                if (row >= 0 && row < layout.channels && controller.state.playback.sourceChannelMutes[static_cast<size_t> (controller.source())][static_cast<size_t> (row)] != muteValue)
                    controller.command ("source_channel_mute", { controller.source() + 1, row + 1, muteValue });
            };
            r.mute.released = [this] (const juce::MouseEvent&) { if (mutePainting) { mutePainting = false; controller.commit(); } };
            r.left.onClick = [this, ch] { controller.shift (ch, -1); };
            r.right.onClick = [this, ch] { controller.shift (ch, 1); };
            r.left.setTooltip ("Shift row left within loop");
            r.right.setTooltip ("Shift row right within loop");
        }
    }
    Layer PatternGrid::displayLayer() const
    {
        if (drag != Drag::none && drag != Drag::marquee && drag != Drag::move && drag != Drag::copy)
            return dragLayer;
        return modifiers.isAltDown() && controller.selected.none() ? Layer::chance : controller.layer;
    }
    bool PatternGrid::isCopyMode() const
    {
        return controller.selected.any() && (copySelection || modifiers.isAltDown());
    }
    void PatternGrid::modifierKeysChanged (const juce::ModifierKeys& m)
    {
        if (modifiers != m)
        {
            const auto previousLayer = displayLayer();
            modifiers = m;
            if (drag == Drag::move || drag == Drag::copy)
                drag = isCopyMode() ? Drag::copy : Drag::move;
            if (hoverCell.valid() && !velocityHintAt (hoverPosition).valid())
                clearHoverMarker();
            repaint();
            if (displayLayer() != previousLayer && layerChanged)
                layerChanged();
        }
        setMouseCursor (isCopyMode() ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::NormalCursor);
    }
    Location PatternGrid::velocityHintAt (juce::Point<float> point) const
    {
        if (controller.source() < 0 || displayLayer() != Layer::velocity || controller.selected.any()
            || controller.shiftDown (modifiers) || modifiers.isAnyMouseButtonDown())
            return {};
        return velocityEdgeAt (point);
    }
    Location PatternGrid::velocityEdgeAt (juce::Point<float> point) const
    {
        const int boundaryStep = layout.firstStep + jsRound ((point.x - layout.gridLeft()) / layout.cellWidth());
        const float edgeX = layout.gridLeft() + static_cast<float> (boundaryStep - layout.firstStep) * layout.cellWidth();
        const auto location = layout.at ({ edgeX + 0.01f, point.y });
        if (!location.valid() || std::abs (point.x - edgeX) >= velocityEdgeHitRadius)
            return {};
        const auto loop = controller.loop (location.channel);
        if (location.step < loop.loopStart || location.step >= loop.loopStart + loop.loopLength)
            return {};
        const bool rightActive = controller.cell (location).enabled;
        const bool leftActive = location.step > loop.loopStart && controller.cell ({ location.channel, location.step - 1 }).enabled;
        return leftActive || rightActive ? location : Location {};
    }
    void PatternGrid::clearHoverMarker()
    {
        if (hoverCell.valid() && drag == Drag::none)
            stopTimer();
        hoverCell = {};
        if (hoverMarkerVisible)
        {
            hoverMarkerVisible = false;
            repaint();
        }
    }
    void PatternGrid::mouseMove (const juce::MouseEvent& e)
    {
        modifierKeysChanged (e.mods);
        if (drag != Drag::none)
        {
            clearHoverMarker();
            return;
        }
        const auto nextHoverCell = velocityHintAt (e.position);
        hoverPosition = e.position;
        if (nextHoverCell.valid() && nextHoverCell == hoverCell)
        {
            if (hoverMarkerVisible)
                repaint();
            return;
        }
        clearHoverMarker();
        hoverCell = nextHoverCell;
        if (hoverCell.valid())
            startTimer (75);
    }
    void PatternGrid::mouseExit (const juce::MouseEvent&) { clearHoverMarker(); }
    void PatternGrid::sync()
    {
        setMouseCursor (isCopyMode() ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::NormalCursor);
        const auto& p = controller.state.playback;
        layout.channels = p.channelCount;
        layout.steps = p.stepCount;
        if (hoverCell.valid() && velocityHintAt (hoverPosition) != hoverCell)
            clearHoverMarker();
        const bool fullPaint = displayedFirstStep != layout.firstStep || displayedSteps != layout.steps || displayedLayer != displayLayer() || displayedColors != controller.drumColors
                               || displayedSelection.any() != controller.selected.any();
        displayedSteps = layout.steps;
        displayedFirstStep = layout.firstStep;
        displayedLayer = displayLayer();
        displayedColors = controller.drumColors;
        for (int ch = 0; ch < 8; ++ch)
        {
            auto& r = rows[static_cast<size_t> (ch)];
            const auto& channel = p.channels[static_cast<size_t> (ch)];
            for (auto* b : { &r.clear, &r.reverse, &r.ping, &r.mute, &r.left, &r.right })
            {
                b->setVisible (ch < p.channelCount && (!layout.compact || b == &r.mute));
                b->setEnabled (controller.source() >= 0);
            }
            r.label.setVisible (ch < p.channelCount);
            r.note.setVisible (ch < p.channelCount && !layout.compact);
            r.more.setVisible (ch < p.channelCount && layout.compact);
            r.label.setFont (font (layout.compact ? 13.0f : 22.0f, true));
            if (!r.label.isBeingEdited())
                r.label.setText (channel.label, juce::dontSendNotification);
            r.note.setValue (channel.note);
            if (displayedNames != controller.noteNames)
                r.note.repaint();
            const auto rev = channel.playbackMode == PlaybackMode::reverse || channel.playbackMode == PlaybackMode::reverse_ping_pong;
            const auto ping = channel.playbackMode == PlaybackMode::ping_pong || channel.playbackMode == PlaybackMode::reverse_ping_pong;
            r.reverse.getProperties().set ("icon", rev ? "reverse" : "forward");
            r.reverse.setButtonText (rev ? juce::String::fromUTF8 ("←") : juce::String::fromUTF8 ("→"));
            r.ping.setToggleState (ping, juce::dontSendNotification);
            const auto range = controller.loop (ch);
            const auto index = static_cast<size_t> (ch);
            const bool muted = controller.source() >= 0 && p.sourceChannelMutes[static_cast<size_t> (controller.source())][index];
            if (displayedLoopStart[index] != range.loopStart || displayedLoopLength[index] != range.loopLength || displayedMutes[index] != muted)
                repaint (juce::roundToInt (layout.gridLeft()) - 16, juce::roundToInt (layout.cell ({ ch, 0 }).getY()) - 2, juce::roundToInt (layout.gridWidth()) + 34, juce::roundToInt (layout.rowHeight()) + 4);
            displayedLoopStart[index] = range.loopStart;
            displayedLoopLength[index] = range.loopLength;
            displayedMutes[index] = muted;
            r.mute.setToggleState (controller.source() >= 0 && !p.sourceChannelMutes[static_cast<size_t> (controller.source())][static_cast<size_t> (ch)], juce::dontSendNotification);
        }
        for (int ch = 0; ch < 8; ++ch)
            for (int step = 0; step < 32; ++step)
            {
                const Location location { ch, step };
                auto& accessible = accessibleCells[static_cast<size_t> (location.key())];
                const auto& value = controller.cell (location);
                const auto index = static_cast<size_t> (location.key());
                if (displayedCells[index] != value || displayedSelection[index] != controller.selected[index])
                    repaint (layout.cell (location).expanded (3).getSmallestIntegerContainer());
                if (displayedCells[index] != value)
                    accessible.setDescription (accessibleDescription (value));
                displayedCells[index] = value;
                accessible.setVisible (ch < layout.channels && layout.stepVisible (step));
                accessible.setEnabled (controller.source() >= 0);
                accessible.setToggleState (value.enabled, juce::dontSendNotification);
            }
        displayedSelection = controller.selected;
        displayedNames = controller.noteNames;
        if (laidOut != std::pair { layout, getLocalBounds() })
            resized();
        if (fullPaint)
            repaint();
    }
    void PatternGrid::resized()
    {
        laidOut = { layout, getLocalBounds() };
        for (int ch = 0; ch < 8; ++ch)
            for (int step = 0; step < 32; ++step)
                accessibleCells[static_cast<size_t> (ch * 32 + step)].setBounds (layout.cell ({ ch, step }).toNearestInt());
        const int arrowsX = getWidth() - Layout::outerGutter - 80;
        for (int ch = 0; ch < 8; ++ch)
        {
            auto& r = rows[static_cast<size_t> (ch)];
            const int y = juce::roundToInt (layout.cell ({ ch, 0 }).getCentreY()) - 16;
            if (layout.compact)
            {
                const int rowY = juce::roundToInt (layout.cell ({ ch, 0 }).getY());
                const int height = juce::roundToInt (layout.cellHeight());
                r.label.setBounds (2, rowY, 62, height);
                r.mute.setBounds (65, rowY, 31, height);
                r.more.setBounds (98, rowY, 30, height);
                continue;
            }
            r.clear.setBounds (arrowsX + 60, y + 4, 20, 24);
            r.label.setBounds (Layout::outerGutter, y - 2, 76, 36);
            r.note.setBounds (101, y, 44, 32);
            r.reverse.setBounds (163, y, 34, 32);
            r.ping.setBounds (215, y, 28, 32);
            r.mute.setBounds (261, y, 24, 32);
            r.left.setBounds (arrowsX, y, 26, 32);
            r.right.setBounds (arrowsX + 26, y, 26, 32);
        }
    }
    void PatternGrid::paint (juce::Graphics& g)
    {
        const auto gridBackdrop = Palette::shell.interpolatedWith (Palette::app, 0.28f);
        const auto loopTint = Palette::accent.withAlpha (0.08f);
        const auto loopBackdrop = gridBackdrop.overlaidWith (loopTint);
        g.fillAll (gridBackdrop);
        const auto layer = displayLayer();
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const bool hasSelection = controller.selected.any();
        for (int step = layout.firstStep; step < layout.firstStep + layout.visibleSteps(); ++step)
        {
            auto r = layout.cell ({ 0, step });
            r.setY (layout.cellsY() - (layout.compact ? 18.0f : 32.0f));
            r.setHeight (layout.compact ? 16.0f : 22.0f);
            text (g, juce::String (step + 1), r, 10, step + 1 == controller.playingStep ? Palette::accent : Palette::muted);
        }
        for (int ch = 0; ch < layout.channels; ++ch)
        {
            const auto range = controller.loop (ch);
            auto first = layout.cell ({ ch, range.loopStart });
            auto brace = first.withWidth (static_cast<float> (range.loopLength) * layout.cellWidth()).expanded (layout.compact ? 0.0f : 16.0f, 1);
            if (layout.compact)
                brace = brace.getIntersection ({ layout.gridLeft(), brace.getY(), layout.gridWidth(), brace.getHeight() });
            g.setColour (loopTint);
            g.fillRoundedRectangle (brace, 6);
            const auto braceArea = brace.toNearestInt();
            for (int step = layout.firstStep; step < layout.firstStep + layout.visibleSteps(); ++step)
            {
                const Location pos { ch, step };
                const auto cellArea = layout.cell (pos).expanded (3).getSmallestIntegerContainer();
                if (!g.clipRegionIntersects (cellArea))
                    continue;
                const auto& c = controller.cell (pos);
                auto r = layout.cell (pos).reduced (0.8f);
                const bool selected = controller.selected.test (static_cast<size_t> (pos.key()));
                const bool dimmed = hasSelection && !selected;
                const bool groupStart = step % 4 == 0;
                const bool inLoop = step >= range.loopStart && step < range.loopStart + range.loopLength;
                auto colour = Palette::channel (ch, controller.drumColors);
                if (groupStart)
                    colour = colour.interpolatedWith (Palette::app, 0.12f);
                if (controller.source() >= 0 && controller.state.playback.sourceChannelMutes[static_cast<size_t> (controller.source())][static_cast<size_t> (ch)])
                    colour = colour.interpolatedWith (Palette::surface, 0.58f);
                if (dimmed)
                    colour = colour.withMultipliedSaturation (0.6f);
                const bool flash = controller.sourceFlashes[static_cast<size_t> (ch)][static_cast<size_t> (step)] > now;
                const auto valueColour = flash ? Palette::app.interpolatedWith (Palette::text, 0.78f) : Palette::app;
                const auto dark = colour.interpolatedWith (Palette::app, 0.48f);
                const auto light = colour.brighter (0.18f);
                const auto fillBottom = colour.interpolatedWith (Palette::app, 0.18f);
                // Out-of-loop and dimmed cells are faded toward the backdrop. Blending every colour
                // against an opaque backdrop matches a transparency layer without the offscreen pass.
                const float opacity = (inLoop ? 1.0f : 0.33f) * (dimmed ? 0.16f : 1.0f);
                const auto paintCell = [&] (juce::Colour backdrop)
                {
                    const auto tone = [opacity, backdrop] (juce::Colour value)
                    {
                        return opacity >= 1.0f ? value : backdrop.interpolatedWith (value.withAlpha (1.0f), opacity).withAlpha (value.getFloatAlpha());
                    };
                    surface (g, r, false, false, tone);
                    if (groupStart && !c.enabled)
                    {
                        g.setColour (tone (juce::Colours::black.withAlpha (0.22f)));
                        g.fillRoundedRectangle (r.reduced (0.5f), 5);
                    }
                    if (!c.enabled)
                        return;
                    juce::ColourGradient fill (tone (light), r.getTopLeft(), tone (fillBottom), r.getBottomLeft(), false);
                    if (layer == Layer::velocity)
                    {
                        // The unlit share of the cell becomes a hard-edged leading band of the same gradient.
                        const double shade = 1.0 - c.velocity / 127.0;
                        const double edge = juce::jmin (1.0, shade + 0.5 / juce::jmax (1.0, static_cast<double> (r.getHeight())));
                        if (shade > 0.0)
                        {
                            fill.clearColours();
                            fill.addColour (0.0, tone (dark));
                            fill.addColour (shade, tone (dark));
                            fill.addColour (edge, tone (light.interpolatedWith (fillBottom, static_cast<float> (edge))));
                            fill.addColour (1.0, tone (fillBottom));
                        }
                    }
                    g.setGradientFill (fill);
                    g.fillRoundedRectangle (r, 5);
                    // Roll subdivisions stay visible beneath every layer's value treatment.
                    if (c.roll > 1)
                    {
                        juce::Graphics::ScopedSaveState rollClip (g);
                        juce::Path clip;
                        clip.addRoundedRectangle (r, 5);
                        g.reduceClipRegion (clip);
                        const float width = r.getWidth() / static_cast<float> (c.roll);
                        for (int part = 0; part < c.roll; ++part)
                        {
                            const float x = r.getX() + static_cast<float> (part) * width;
                            if (part % 2 == 1)
                            {
                                g.setColour (tone (Palette::app.withAlpha (0.14f)));
                                g.fillRect (x, r.getY(), width, r.getHeight());
                            }
                            if (part > 0)
                            {
                                g.setColour (tone (Palette::text.withAlpha (0.18f)));
                                g.fillRect (x, r.getY(), 0.75f, r.getHeight());
                            }
                        }
                    }
                    g.setColour (tone (Palette::text.withAlpha (0.22f)));
                    g.drawRoundedRectangle (r.reduced (0.5f), 5, 1);
                    // Lift the smaller probability label so its glyph top matches velocity.
                    const float valueSize = juce::jmin (18.0f, r.getWidth() * 0.38f, r.getHeight() * 0.5f);
                    const float labelSize = layer == Layer::chance ? juce::jmax (1.0f, valueSize - 3.0f) : valueSize;
                    const auto labelBounds = layer == Layer::chance ? r.translated (0.0f, -1.0f) : r;
                    text (g, juce::String (cellValue (c, layer)) + (layer == Layer::chance ? "%" : ""), labelBounds, labelSize, tone (valueColour));
                    if (layer == Layer::chance)
                    {
                        const int bits = juce::jmin (c.cycle, 30);
                        const auto strip = r.withY (r.getY() + r.getHeight() * 0.8f - 3.0f).withHeight (r.getHeight() * 0.14f + 3.0f).reduced (3, 0);
                        for (int i = 0; i < bits; ++i)
                        {
                            g.setColour (tone ((c.cycleMask & (1 << i)) != 0 ? light : Palette::app));
                            g.fillRect (strip.getX() + static_cast<float> (i) * strip.getWidth() / static_cast<float> (bits), strip.getY(), juce::jmax (1.0f, strip.getWidth() / static_cast<float> (bits) - 2.0f), strip.getHeight());
                        }
                    }
                };
                if (inLoop || opacity >= 1.0f || !braceArea.intersects (cellArea))
                {
                    paintCell (inLoop ? loopBackdrop : gridBackdrop);
                    continue;
                }
                // Loop handles extend under the neighbouring cells, so those sit on two backdrops.
                {
                    juce::Graphics::ScopedSaveState outside (g);
                    g.excludeClipRegion (braceArea);
                    paintCell (gridBackdrop);
                }
                {
                    juce::Graphics::ScopedSaveState inside (g);
                    g.reduceClipRegion (braceArea);
                    paintCell (loopBackdrop);
                }
            }
            // Paint selection over the completed row so neighboring cells cannot
            // cover its outer halo. Keep the cell interior free of inset borders.
            for (int step = layout.firstStep; step < layout.firstStep + layout.visibleSteps(); ++step)
            {
                const Location pos { ch, step };
                if (!controller.selected.test (static_cast<size_t> (pos.key()))
                    || !g.clipRegionIntersects (layout.cell (pos).expanded (3).getSmallestIntegerContainer()))
                    continue;
                const auto frame = layout.cell (pos).reduced (0.15f);
                g.setColour (Palette::accent.withAlpha (0.16f));
                g.drawRoundedRectangle (frame, 5.65f, 4.0f);
                g.setColour (Palette::text.withAlpha (0.14f));
                g.drawRoundedRectangle (frame, 5.65f, 2.5f);
                g.setColour (Palette::text.withAlpha (0.95f));
                g.drawRoundedRectangle (frame, 5.65f, 1.3f);
            }
            // Keep the loop boundary and handles above cells outside the loop.
            g.setColour (Palette::accent.withAlpha (0.42f));
            g.drawRoundedRectangle (brace, 6, 1);
            if (!layout.compact)
            {
                for (const float x : { brace.getX() + 8, brace.getRight() - 8 })
                    for (int dot = -2; dot <= 2; ++dot)
                    {
                        g.setColour (Palette::muted);
                        g.fillEllipse (x - 0.6f, brace.getCentreY() + static_cast<float> (dot) * 4, 1.2f, 1.2f);
                    }
            }
        }
        const auto paintMarker = [&g] (juce::Point<float> point)
        {
            const juce::Rectangle<float> marker (point.x - 5, point.y - 5, 10, 10);
            g.setColour (Palette::app);
            g.fillEllipse (marker.expanded (1));
            g.setColour (Palette::accent);
            g.drawEllipse (marker, 2);
        };
        if (hoverMarkerVisible && velocityHintAt (hoverPosition) == hoverCell)
            paintMarker ({ layout.cell (hoverCell).getX(), hoverPosition.y });
        if (drag == Drag::velocityLine && anchor.valid())
        {
            const auto row = layout.cell (anchor);
            const int endBoundary = juce::jlimit (0, layout.steps, jsRound ((current.x - layout.gridLeft()) / layout.cellWidth()));
            const auto start = juce::Point<float> (layout.gridLeft() + static_cast<float> (boundary) * layout.cellWidth(),
                row.getBottom() - static_cast<float> (lineVelocity - 1) / 126.0f * row.getHeight());
            const auto end = juce::Point<float> (layout.gridLeft() + static_cast<float> (endBoundary) * layout.cellWidth(),
                juce::jlimit (row.getY(), row.getBottom(), current.y));
            for (const auto point : { start, end })
                paintMarker (point);
        }
        if (drag == Drag::marquee)
        {
            auto r = juce::Rectangle<float> (origin, current);
            g.setColour (Palette::accent.withAlpha (0.1f));
            g.fillRect (r);
            g.setColour (Palette::accent);
            g.drawRect (r, 1);
        }
        if ((drag == Drag::move || drag == Drag::copy) && target.valid())
            for (auto p : controller.selection())
            {
                Location d { wrap (target.channel + p.channel - anchor.channel, layout.channels), wrap (target.step + p.step - anchor.step, layout.steps) };
                g.setColour (Palette::accent.withAlpha (0.3f));
                g.fillRoundedRectangle (layout.cell (d).reduced (2), 4);
            }
    }
    void PatternGrid::updatePlayback()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        for (int ch = 0; ch < layout.channels; ++ch)
            for (int s = 0; s < layout.steps; ++s)
            {
                const bool flash = controller.sourceFlashes[static_cast<size_t> (ch)][static_cast<size_t> (s)] > now;
                auto& last = lastFlash[static_cast<size_t> (ch)][static_cast<size_t> (s)];
                if (last != flash)
                {
                    last = flash;
                    repaint (layout.cell ({ ch, s }).expanded (3).getSmallestIntegerContainer());
                }
            }
        if (previousStep != controller.playingStep)
        {
            previousStep = controller.playingStep;
            repaint (0, 0, getWidth(), juce::roundToInt (layout.cellsY()));
        }
    }
    void PatternGrid::mouseDown (const juce::MouseEvent& e)
    {
        stopTimer();
        clearHoverMarker();
        if (controller.source() < 0 || e.mods.isPopupMenu())
            return;
        if (isShowing())
            grabKeyboardFocus();
        modifiers = e.mods;
        origin = current = e.position;
        anchor = target = layout.at (e.position);
        dragLayer = displayLayer();
        initialSelection = controller.selected;
        additive = controller.useSelectionModifier (e.mods);
        const int row = static_cast<int> (std::floor ((e.position.y - layout.cellsY()) / layout.rowHeight()));
        if (!layout.compact && row >= 0 && row < layout.channels)
        {
            const auto loop = controller.loop (row);
            const auto first = layout.cell ({ row, loop.loopStart });
            const float end = first.getX() + static_cast<float> (loop.loopLength) * layout.cellWidth();
            if (std::abs (e.position.x - (first.getX() - 8)) < 8 || std::abs (e.position.x - (end + 8)) < 8)
            {
                anchor = { row, loop.loopStart };
                initialLoop = loop;
                drag = e.position.x < first.getX() ? Drag::loopStart : Drag::loopEnd;
                controller.begin ("Change loop range");
                return;
            }
        }
        const bool rightBoundary = !layout.compact && row >= 0 && row < layout.channels
                                   && std::abs (e.position.x - (layout.gridLeft() + layout.steps * layout.cellWidth())) < velocityEdgeHitRadius
                                   && controller.cell ({ row, layout.steps - 1 }).enabled;
        const auto edge = !layout.compact && dragLayer == Layer::velocity ? velocityEdgeAt (e.position) : Location {};
        if (edge.valid())
            anchor = edge;
        if (rightBoundary && dragLayer == Layer::velocity)
            anchor = { row, layout.steps - 1 };
        selectedAtStart = anchor.valid() && controller.selected.test (static_cast<size_t> (anchor.key()));
        if (initialSelection.any() && !additive && !selectedAtStart)
        {
            drag = Drag::none;
            controller.selected.reset();
            controller.notify();
            repaint();
            return;
        }
        if (!anchor.valid() || (additive && !selectedAtStart))
        {
            drag = Drag::marquee;
            updateSelection();
            return;
        }
        const auto loop = controller.loop (anchor.channel);
        if (anchor.step < loop.loopStart || anchor.step >= loop.loopStart + loop.loopLength)
            return;
        original = controller.cell (anchor);
        valueStep = anchor.step;
        valueVelocity = velocityAtY (e.position.y);
        originals.clear();
        if (selectedAtStart)
            for (auto p : controller.selection())
                originals.emplace_back (p, controller.cell (p));
        else
            originals.emplace_back (anchor, original);
        if (!layout.compact && dragLayer == Layer::velocity && !additive && !selectedAtStart && (rightBoundary || edge.valid()))
        {
            drag = Drag::velocityLine;
            boundary = rightBoundary ? layout.steps : anchor.step;
            lineVelocity = velocityAtY (e.position.y);
            originals.clear();
            for (int s = 0; s < layout.steps; ++s)
                originals.emplace_back (Location { anchor.channel, s }, controller.cell ({ anchor.channel, s }));
            controller.begin ("Draw velocity line");
            repaint();
            return;
        }
        drag = Drag::pending;
        if (e.mods.isLeftButtonDown() && !additive && !isCopyMode())
            startTimer (500);
    }
    void PatternGrid::timerCallback()
    {
        stopTimer();
        if (drag == Drag::none && hoverCell.valid() && velocityHintAt (hoverPosition) == hoverCell)
        {
            hoverMarkerVisible = true;
            repaint();
            return;
        }
        if (drag != Drag::pending || !anchor.valid() || controller.source() < 0)
            return;
        // Use the same additive selection toggle as Shift-click, without editing the cell.
        additive = true;
        current = origin;
        drag = Drag::none;
        updateSelection();
    }
    void PatternGrid::updateSelection()
    {
        controller.selected = additive ? initialSelection : Selection {};
        auto rectangle = juce::Rectangle<float> (origin, current);
        if (origin.getDistanceFrom (current) < 5 && anchor.valid())
            controller.selected.flip (static_cast<size_t> (anchor.key()));
        else
            for (int ch = 0; ch < layout.channels; ++ch)
                for (int s = 0; s < layout.steps; ++s)
                    if (rectangle.intersects (layout.cell ({ ch, s })))
                        controller.selected.set (static_cast<size_t> (ch * 32 + s));
        controller.notify();
        repaint();
    }
    int PatternGrid::velocityAtY (float y) const
    {
        const auto r = layout.cell (anchor);
        return juce::jlimit (1, 127, jsRound (1 + (1 - (y - r.getY()) / r.getHeight()) * 126));
    }
    void PatternGrid::mouseDrag (const juce::MouseEvent& e)
    {
        current = e.position;
        modifiers = e.mods;
        if (origin.getDistanceFrom (current) >= 4.0f)
            stopTimer();
        if (drag == Drag::none)
            return;
        if (drag == Drag::marquee)
        {
            updateSelection();
            return;
        }
        if (drag == Drag::loopStart || drag == Drag::loopEnd)
        {
            const int delta = jsRound ((current.x - origin.x) / layout.cellWidth());
            int start = initialLoop.loopStart, end = start + initialLoop.loopLength - 1;
            if (drag == Drag::loopStart)
                start = juce::jlimit (0, end, start + delta);
            else
                end = juce::jlimit (start, layout.steps - 1, end + delta);
            controller.command ("channel_loop_length", { anchor.channel + 1, end - start + 1, start + 1 });
            return;
        }
        target = { juce::jlimit (0, layout.channels - 1, static_cast<int> (std::floor ((current.y - layout.cellsY()) / layout.rowHeight()))), juce::jlimit (layout.firstStep, layout.firstStep + layout.visibleSteps() - 1, layout.firstStep + static_cast<int> (std::floor ((current.x - layout.gridLeft()) / layout.cellWidth()))) };
        if (drag == Drag::pending)
        {
            if (origin.getDistanceFrom (current) < (selectedAtStart ? 5.0f : 4.0f))
                return;
            if (selectedAtStart && isCopyMode())
                drag = Drag::copy;
            else if (dragLayer == Layer::chance)
                drag = Drag::value;
            else if (selectedAtStart)
                drag = controller.useSelectionModifier (e.mods) ? Drag::value : Drag::move;
            else
                drag = std::abs (current.x - origin.x) > std::abs (current.y - origin.y) ? Drag::paint : Drag::value;
            controller.begin ("Edit cells");
        }
        if (drag == Drag::move || drag == Drag::copy)
        {
            drag = isCopyMode() ? Drag::copy : Drag::move;
            repaint();
            return;
        }
        if (drag == Drag::paint)
        {
            const auto loop = controller.loop (anchor.channel);
            for (int s = juce::jmax (loop.loopStart, juce::jmin (anchor.step, target.step)); s <= juce::jmin (loop.loopStart + loop.loopLength - 1, juce::jmax (anchor.step, target.step)); ++s)
            {
                auto c = original.enabled ? controller.cell ({ anchor.channel, s }) : original;
                c.enabled = !original.enabled;
                controller.editCell ({ anchor.channel, s }, c);
            }
        }
        else if (drag == Drag::value)
        {
            const auto sensitivity = 2.0f;
            const int offset = dragOffset (origin.y - current.y, sensitivity);
            if (dragLayer == Layer::velocity && !selectedAtStart)
            {
                const auto loop = controller.loop (anchor.channel);
                const int step = juce::jlimit (loop.loopStart, loop.loopStart + loop.loopLength - 1, target.step);
                const int velocity = velocityAtY (current.y);
                // Join successive mouse positions so quick sweeps do not skip cells.
                for (int s = juce::jmin (valueStep, step); s <= juce::jmax (valueStep, step); ++s)
                {
                    const float fraction = step == valueStep ? 1.0f : static_cast<float> (std::abs (s - valueStep)) / static_cast<float> (std::abs (step - valueStep));
                    auto next = controller.cell ({ anchor.channel, s });
                    next.velocity = jsRound (static_cast<float> (valueVelocity) + fraction * static_cast<float> (velocity - valueVelocity));
                    next.enabled = true;
                    controller.editCell ({ anchor.channel, s }, next);
                }
                valueStep = step;
                valueVelocity = velocity;
            }
            else
                for (const auto& [p, c] : originals)
                {
                    auto next = withValue (c, dragLayer, cellValue (c, dragLayer) + offset);
                    if (!selectedAtStart)
                        next.enabled = true;
                    controller.editCell (p, next);
                }
        }
        else if (drag == Drag::velocityLine)
        {
            const int endBoundary = juce::jlimit (0, layout.steps, jsRound ((current.x - layout.gridLeft()) / layout.cellWidth()));
            const int velocity = velocityAtY (current.y);
            for (const auto& [p, c] : originals)
                controller.editCell (p, c);
            if (endBoundary != boundary)
            {
                int start = endBoundary > boundary ? boundary : boundary - 1;
                int end = endBoundary > boundary ? endBoundary - 1 : endBoundary;
                for (int s = juce::jmin (start, end); s <= juce::jmax (start, end); ++s)
                {
                    auto c = controller.cell ({ anchor.channel, s });
                    const float fraction = static_cast<float> (std::abs (s - start)) / static_cast<float> (juce::jmax (1, std::abs (end - start)));
                    c.velocity = jsRound (static_cast<float> (lineVelocity) + fraction * static_cast<float> (velocity - lineVelocity));
                    controller.editCell ({ anchor.channel, s }, c);
                }
            }
            // The endpoints can move even when no cell value changes.
            repaint();
        }
        controller.notify();
    }
    void PatternGrid::mouseUp (const juce::MouseEvent& e)
    {
        stopTimer();
        modifiers = e.mods;
        if (drag == Drag::move || drag == Drag::copy)
            drag = isCopyMode() ? Drag::copy : Drag::move;
        if (drag == Drag::pending && anchor.valid() && !(selectedAtStart && isCopyMode()))
        {
            if (selectedAtStart && additive)
            {
                controller.selected.reset (static_cast<size_t> (anchor.key()));
            }
            else if (dragLayer == Layer::velocity)
            {
                controller.begin ("Toggle cell");
                auto c = controller.cell (anchor);
                c.enabled = !c.enabled;
                controller.editCell (anchor, c);
            }
        }
        if ((drag == Drag::move || drag == Drag::copy) && target.valid())
            controller.moveSelection (anchor, target, drag == Drag::copy);
        drag = Drag::none;
        controller.commit();
        controller.notify();
        repaint();
    }
    void PatternGrid::cancelGesture()
    {
        stopTimer();
        clearHoverMarker();
        drag = Drag::none;
        mutePainting = false;
        controller.cancel();
        repaint();
    }
}
