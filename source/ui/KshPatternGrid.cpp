#include "KshPatternGrid.h"
#include "../engine/KshControllerCurve.h"
namespace ksh::ui
{
    namespace
    {
        constexpr float velocityEdgeHitRadius = 9.0f;
        constexpr float touchVelocityEdgeHitRadius = 13.0f;
        constexpr float velocityAnchorPulseRadius = 54.0f;
        constexpr double velocityAnchorPulseMs = 660.0;
        constexpr int selectHoldMs = 800;
        constexpr int presetHoldMs = 1350;
        constexpr int tabletVelocityHoldMs = 400;
        constexpr int tabletSelectHoldMs = 550;
        constexpr int tabletPresetHoldMs = 950;
        constexpr int phonePresetHoldMs = 1650;
        constexpr float presetPressSlop = 4.0f;
        constexpr float touchPresetPressSlop = 12.0f;
        constexpr float chanceCycleHeightIncrease = 0.05f;

        struct RhythmPreset
        {
            const char* label;
            double beats; // Zero means a single step at the current grid division.
            int split;
        };
        constexpr std::array rhythmPresets {
            RhythmPreset { "16th triplet", 0.5, 3 },
            RhythmPreset { "8th triplet", 1.0, 3 },
            RhythmPreset { "Quarter triplet", 2.0, 3 },
            RhythmPreset { "2-hit roll", 0.0, 2 },
            RhythmPreset { "3-hit roll", 0.0, 3 },
            RhythmPreset { "4-hit roll", 0.0, 4 }
        };
        int spanForPreset (const RhythmPreset& preset, double beatsPerStep)
        {
            return preset.beats == 0.0 ? 1 : gridSpanForBeats (preset.beats, beatsPerStep);
        }

        void paintInsetEdge (juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            g.setColour (Palette::app.withAlpha (0.22f));
            g.drawRoundedRectangle (bounds.reduced (0.3f), 1.5f, 0.6f);
            g.setColour (Palette::text.withAlpha (0.12f));
            g.drawHorizontalLine (juce::roundToInt (bounds.getBottom() - 1.0f), bounds.getX() + 1.5f, bounds.getRight() - 1.5f);
        }

        void paintCycleStrip (juce::Graphics& g, juce::Rectangle<float> strip, int length, int mask, juce::Colour ink, juce::Colour trackTint)
        {
            const int bits = juce::jmin (length, 30);
            const float stride = strip.getWidth() / static_cast<float> (bits);
            for (int i = 0; i < bits; ++i)
            {
                // Split the two-pixel gap between both sides to centre the strip.
                const juce::Rectangle<float> bit (strip.getX() + 1.0f + static_cast<float> (i) * stride, strip.getY(),
                                                  juce::jmax (1.0f, stride - 2.0f), strip.getHeight());
                g.setColour ((mask & (1 << i)) != 0 ? ink : trackTint);
                g.fillRoundedRectangle (bit, 2);
                paintInsetEdge (g, bit);
            }
        }

        juce::Rectangle<float> cycleStripBounds (juce::Rectangle<float> body)
        {
            return body.withY (body.getY() + body.getHeight() * (0.8f - chanceCycleHeightIncrease) - 3.0f)
                       .withHeight (body.getHeight() * (0.14f + chanceCycleHeightIncrease) + 3.0f).reduced (3, 0);
        }

        juce::Rectangle<float> splitSegment (juce::Rectangle<float> body, int count, int part)
        {
            const float width = body.getWidth() / static_cast<float> (count);
            return body.withX (body.getX() + static_cast<float> (part) * width).withWidth (width);
        }

        juce::Rectangle<float> probabilityTrack (juce::Rectangle<float> body, bool split)
        {
            const float inset = split ? juce::jmin (4.0f, body.getWidth() * 0.20f) : 4.0f;
            return body.reduced (inset, 3).withBottom (cycleStripBounds (body).getY() - 3.0f);
        }

        juce::Rectangle<int> stepPopupBounds (juce::Rectangle<int> step, juce::Rectangle<int> available, int width, int height)
        {
            // The component already reserves space around its edge for the glow.
            const int below = step.getBottom();
            const int above = step.getY() - height;
            const bool fitsBelow = below + height <= available.getBottom();
            const bool fitsAbove = above >= available.getY();
            if (!fitsBelow && !fitsAbove)
            {
                const int right = step.getRight();
                const int left = step.getX() - width;
                // Prefer the left side so there is room to extend the step to the right.
                const bool fitsLeft = left >= available.getX();
                if (fitsLeft || right + width <= available.getRight())
                    return juce::Rectangle<int> (fitsLeft ? left : right, step.getCentreY() - height / 2, width, height).constrainedWithin (available);
            }
            const int y = fitsBelow || !fitsAbove ? below : above;
            return juce::Rectangle<int> (step.getCentreX() - width / 2, y, width, height).constrainedWithin (available);
        }

        juce::String accessibleDescription (const Cell& value, bool cc)
        {
            return juce::String (value.enabled ? "On" : "Off") + (cc ? ", CC value " : ", velocity ")
                + juce::String (cc ? value.controllerValue() : value.velocity) + ", probability " + juce::String (value.probability) + ", split " + juce::String (value.roll) + ", cycle " + juce::String (value.cycle) + ", nudge " + juce::String (value.nudge) + "%" + (value.span > 1 ? ", span " + juce::String (value.span) + " steps" : juce::String());
        }
    }

    class PatternGrid::PresetMenu final : public juce::Component
    {
    public:
        explicit PresetMenu (PatternGrid& grid) : owner (grid)
        {
            setTitle ("Step presets");
            setAlwaysOnTop (true);
            addMouseListener (this, true);
            for (const auto& preset : rhythmPresets)
            {
                auto* item = items.add (new PresetButton (preset.label));
                addAndMakeVisible (*item);
                item->onClick = [this, preset] { choosePreset (preset); };
            }
            glide.setTitle ("Glide to next");
            glide.setToggleable (true);
            glide.onClick = [this]
            {
                auto& editor = owner.controller;
                editor.refresh();
                if (!isVisible() || !targetsAreCurrent() || !canEditGlide() || editor.editing())
                    return;
                editor.toggleCcGlide (targets);
                if (closeAfterChoice)
                    close();
                else
                    refresh();
            };
        }

        bool isTrackingPress() const { return trackingPress; }
        bool needsOutsidePress() const { return dismissOnPress && isVisible(); }

        void open (PresetPress next, bool touchInput)
        {
            if (isVisible())
                repaintAnchor();
            press = next;
            const auto& editor = owner.controller;
            targets = editor.cell (press.location).enabled && editor.selected.test (static_cast<size_t> (press.location.key()))
                ? editor.selection (true) : std::vector<Location> {};
            selectionAtOpen.reset();
            if (targets.size() > 1)
                selectionAtOpen = editor.selected;
            else
                targets = { press.location };
            dismissOnPress = touchInput || owner.layout.touchRows;
            closeAfterChoice = !touchInput;
            trackingPress = true;
            // Keep the original anchor when the edit reshapes the hit, including swing offsets.
            const auto slot = owner.layout.cell (press.location);
            anchorOffset = (owner.cellBounds (press.location).getX() - slot.getX()) / slot.getWidth();
            anchorSpan = owner.displayedSpan (press.location);
            auto* container = owner.getParentComponent();
            if (container == nullptr)
                container = &owner;
            container->addChildComponent (*this);
            const int targetSize = owner.layout.touchRows && !owner.layout.compactOnPhone ? Layout::touchTarget : 52;
            if (canEditGlide())
                addAndMakeVisible (glide);
            else
                removeChildComponent (&glide);
            const int menuRows = rowsPerColumn + (hasGlideSection() ? 1 : 0);
            const int frameHeight = fixedHeight + (hasGlideSection() ? headingHeight + sectionGap : 0);
            rowHeight = juce::jlimit (1, targetSize, (container->getHeight() - frameHeight) / menuRows);
            place (*container, 2 * columnWidth + columnGap + 2 * contentInset, frameHeight + menuRows * rowHeight);
            resized();
            setVisible (true);
            toFront (false);
            refresh();
            repaintAnchor();
        }

        void close()
        {
            trackingPress = false;
            if (isVisible())
                repaintAnchor();
            setVisible (false);
        }

        void paintAnchor (juce::Graphics& g)
        {
            if (!isVisible())
                return;

            const auto frame = owner.layout.cellArtwork (owner.cellBounds (press.location)).reduced (0.7f);
            const juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (owner.getLocalBounds().toFloat().withX (owner.layout.gridLeft()).withWidth (owner.layout.gridWidth()).getSmallestIntegerContainer());
            juce::Path outline;
            outline.addRoundedRectangle (frame, 4.5f);
            anchorGlow.setColor (accent.brighter (0.2f).withAlpha (0.82f));
            anchorGlow.render (g, outline, juce::PathStrokeType (2.5f));
            g.setColour (accent.interpolatedWith (Palette::text, 0.22f));
            g.strokePath (outline, juce::PathStrokeType (1.5f));
        }

        void track (juce::Point<int> position)
        {
            const auto* highlighted = itemAt (position);
            const auto highlight = [highlighted] (PresetButton& item)
            {
                if (const bool active = &item == highlighted; item.dragHighlighted != active)
                {
                    item.dragHighlighted = active;
                    item.repaint();
                }
            };
            for (auto* item : items)
                highlight (*item);
            highlight (glide);
        }

        void leaveAnchor (juce::Point<int> position)
        {
            if (isVisible() && !dismissOnPress && !trackingPress
                && !owner.getLocalArea (this, panelBounds()).getUnion (owner.cellBounds (press.location).getSmallestIntegerContainer()).contains (position))
                close();
        }

        void release (juce::Point<float> position)
        {
            trackingPress = false;
            track ({ -1, -1 });
            if (press.position.getDistanceFrom (position) < presetPressSlop)
                return;
            if (auto* item = itemAt (getLocalPoint (&owner, position).roundToInt()))
                item->onClick();
            else if (!dismissOnPress)
                close();
        }

        void refresh()
        {
            if (!isVisible())
                return;
            if (!targetsAreCurrent())
            {
                close();
                return;
            }
            if (auto* container = getParentComponent())
                place (*container, getWidth(), getHeight());
            const auto colour = Palette::channel (press.location.channel);
            if (std::exchange (accent, colour) != colour)
            {
                repaint();
                repaintAnchor();
            }
            glide.setEnabled (canEditGlide());
            glide.setAccent (colour);
            glide.setToggleState (glide.isEnabled() && std::ranges::all_of (targets, [this] (Location p)
                { return !owner.controller.isControllerRow (p.channel) || owner.controller.glidesToNext (p); }), juce::dontSendNotification);
            for (int i = 0; i < items.size(); ++i)
            {
                auto* item = items[i];
                const auto& preset = rhythmPresets[static_cast<size_t> (i)];
                const int length = spanForPreset (preset, owner.controller.state.playback.beatsPerStep);
                const bool enabled = allowedSpan (length);
                item->setAccent (colour);
                item->setEnabled (enabled);
                item->setToggleState (enabled && std::ranges::all_of (targets, [&] (Location location)
                {
                    const auto& hit = owner.controller.cell (location);
                    return hit.enabled && hit.span == length && hit.roll == preset.split;
                }), juce::dontSendNotification);
            }
        }

        void mouseExit (const juce::MouseEvent& event) override
        {
            const auto position = event.getEventRelativeTo (this).getPosition();
            if (isVisible() && !dismissOnPress && !trackingPress && event.source.canHover() && !hitTest (position.x, position.y))
                close();
        }

        bool hitTest (int x, int y) override
        {
            return panelBounds().contains (x, y) || panelOutline.contains (static_cast<float> (x), static_cast<float> (y));
        }

        void resized() override
        {
            const int presetsY = presetHeadingY() + headingHeight;
            for (int i = 0; i < items.size(); ++i)
                items[i]->setBounds (columnX (i / rowsPerColumn), presetsY + (i % rowsPerColumn) * rowHeight, columnWidth, rowHeight);
            if (hasGlideSection())
                glide.setBounds (contentInset, contentInset + headingHeight, 2 * columnWidth + columnGap, rowHeight);
        }

        void paint (juce::Graphics& g) override
        {
            paintPopupPanel (g, panelOutline, accent, panelGlow);
            const auto heading = juce::Rectangle<int> (contentInset + 8, contentInset, columnWidth - 8, headingHeight);
            const auto ink = Palette::text.interpolatedWith (accent, 0.35f);
            if (hasGlideSection())
            {
                text (g, "Glide to Next", heading.toFloat(), 18, ink, true, juce::Justification::centredLeft);
                g.setColour (accent.withAlpha (0.28f));
                g.drawHorizontalLine (presetHeadingY() - sectionGap / 2, static_cast<float> (glide.getX()),
                                      static_cast<float> (glide.getRight()));
            }
            text (g, "Step Presets", heading.withY (presetHeadingY()).toFloat(), 18, ink, true, juce::Justification::centredLeft);
        }

    private:
        void place (const juce::Component& container, int width, int height)
        {
            const auto step = stepBounds (container);
            setBounds (stepPopupBounds (step, container.getLocalBounds(), width, height));
            const auto targetBounds = step.translated (-getX(), -getY()).toFloat();
            auto outline = popupPanelOutline (panelBounds().toFloat().reduced (0.5f), panelRadius, targetBounds);
            if (outline != panelOutline)
            {
                panelOutline = std::move (outline);
                repaint();
            }
        }

        void repaintAnchor() const
        {
            owner.repaint (owner.cellPaintBounds (press.location).getSmallestIntegerContainer());
        }

        juce::Rectangle<int> stepBounds (const juce::Component& container) const
        {
            const auto slot = owner.layout.cell (press.location);
            const auto anchorBounds = slot.translated (anchorOffset * slot.getWidth(), 0.0f)
                                          .withWidth (slot.getWidth() * static_cast<float> (anchorSpan));
            return container.getLocalArea (&owner, anchorBounds.getSmallestIntegerContainer())
                .getIntersection (container.getLocalArea (&owner, owner.getLocalBounds()));
        }

        class PresetButton final : public juce::TextButton
        {
        public:
            explicit PresetButton (const juce::String& label) : juce::TextButton (label) {}
            void setAccent (juce::Colour colour)
            {
                if (std::exchange (accent, colour) != colour)
                    repaint();
            }
            void paintButton (juce::Graphics& g, bool hovered, bool down) override
            {
                const bool selected = getToggleState();
                const bool highlight = isEnabled() && (hovered || down || dragHighlighted);
                const auto bounds = getLocalBounds().toFloat().reduced (0.5f, 2.0f);
                const auto face = highlight ? Palette::controls.raised : Palette::controls.shell;
                g.setColour (face.interpolatedWith (accent, selected ? 0.18f : 0.04f).withAlpha (isEnabled() ? 1.0f : 0.45f));
                g.fillRoundedRectangle (bounds, 5.0f);
                const bool outlined = selected || highlight || hasKeyboardFocus (false);
                g.setColour ((outlined ? accent : Palette::controls.border).withAlpha (isEnabled() ? 0.92f : 0.42f));
                g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);
                const auto ink = !isEnabled() ? Palette::text.withMultipliedAlpha (0.48f) : selected ? accent : Palette::text;
                ksh::ui::text (g, getButtonText(), bounds.withTrimmedLeft (10.0f).withTrimmedRight (selected ? 22.0f : 6.0f),
                               18, ink, selected, juce::Justification::centredLeft);
                if (selected)
                {
                    const float x = bounds.getRight() - 17.0f, y = bounds.getCentreY();
                    juce::Path tick;
                    tick.startNewSubPath (x, y);
                    tick.lineTo (x + 3.0f, y + 3.0f);
                    tick.lineTo (x + 9.0f, y - 4.0f);
                    g.setColour (accent);
                    g.strokePath (tick, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }
            bool dragHighlighted = false;

        private:
            juce::Colour accent;
        };

        void choosePreset (const RhythmPreset& preset)
        {
            auto& editor = owner.controller;
            editor.refresh();
            const int length = spanForPreset (preset, editor.state.playback.beatsPerStep);
            const auto canApply = [&] { return isVisible() && targetsAreCurrent() && allowedSpan (length); };
            if (editor.editing() || !canApply())
                return;

            const auto wouldChange = [&] (Location location)
            {
                const auto& saved = editor.cell (location);
                const auto cell = saved.enabled || editor.isControllerRow (location.channel) ? saved : Cell {};
                return !cell.enabled || cell.span != length || cell.roll != preset.split;
            };
            if (std::ranges::none_of (targets, wouldChange))
            {
                if (closeAfterChoice)
                    close();
                return;
            }

            editor.begin (preset.beats == 0.0 ? "Set roll preset" : "Set triplet preset");
            if (canApply())
                for (const auto location : targets)
                {
                    if (!wouldChange (location))
                        continue;
                    auto cell = editor.cell (location);
                    if (!cell.enabled && !editor.isControllerRow (location.channel))
                        cell = {};
                    cell.enabled = true;
                    cell.span = length;
                    cell.roll = preset.split;
                    editor.editCell (location, cell);
                }
            editor.commit();
            if (closeAfterChoice)
                close();
            else
                refresh();
        }

        bool targetsAreCurrent() const
        {
            const auto& editor = owner.controller;
            return owner.canSetPreset (press) && (!selectionAtOpen
                || (editor.selected == *selectionAtOpen && editor.selection (true) == targets));
        }

        bool allowedSpan (int length) const
        {
            return length == 1 || std::ranges::all_of (targets, [&] (Location location) { return owner.controller.canFitSpan (location, length); });
        }

        bool canEditGlide() const
        {
            return owner.controller.isControllerRow (press.location.channel) && owner.controller.cell (press.location).enabled;
        }

        PresetButton* itemAt (juce::Point<int> position)
        {
            if (hasGlideSection() && glide.isEnabled() && glide.getBounds().contains (position))
                return &glide;
            for (auto* item : items)
                if (item->isVisible() && item->isEnabled() && item->getBounds().contains (position))
                    return item;
            return nullptr;
        }

        juce::Rectangle<int> panelBounds() const { return getLocalBounds().reduced (glowInset); }
        int columnX (int column) const { return contentInset + column * (columnWidth + columnGap); }
        bool hasGlideSection() const { return glide.getParentComponent() == this; }
        int presetHeadingY() const { return contentInset + (hasGlideSection() ? headingHeight + rowHeight + sectionGap : 0); }
        static constexpr int glowInset = 12, contentInset = glowInset + 14;
        static constexpr float panelRadius = 7.0f;
        static constexpr int columnWidth = 160, columnGap = 8, headingHeight = 32, sectionGap = 8;
        static constexpr int rowsPerColumn = 3;
        static constexpr int fixedHeight = 2 * contentInset + headingHeight;
        int rowHeight = 0;
        PatternGrid& owner;
        juce::OwnedArray<PresetButton> items;
        PresetButton glide { "Glide" };
        std::vector<Location> targets;
        std::optional<Selection> selectionAtOpen;
        PresetPress press {};
        juce::Colour accent;
        juce::Path panelOutline;
        melatonin::DropShadow panelGlow { juce::Colours::transparentBlack, 10 };
        melatonin::DropShadow anchorGlow { juce::Colours::transparentBlack, 6, {}, 1 };
        float anchorOffset = 0.0f;
        int anchorSpan = 1;
        bool trackingPress = false, dismissOnPress = false, closeAfterChoice = false;
    };

    PatternGrid::~PatternGrid() = default;

    bool PatternGrid::hasActiveGesture() const
    {
        return drag != Drag::none || velocityTap.has_value() || presetPress.has_value() || cyclePress.has_value() || splitPress.has_value() || rowSwipe.has_value()
            || (presetMenu != nullptr && presetMenu->isVisible());
    }

    bool PatternGrid::handleCellPopupMouseDown (const juce::Component* clicked)
    {
        const auto clickedInside = [clicked] (const juce::Component* popup)
        {
            return clicked == popup || (clicked != nullptr && popup->isParentOf (clicked));
        };
        if (presetMenu == nullptr || !presetMenu->isVisible())
            return false;
        if (clickedInside (presetMenu.get()))
            return true;
        // Grid presses are handled before the editor's listener sees them.
        if (clicked != this)
            presetMenu->close();
        return false;
    }

    PatternGrid::PatternGrid (EditorController& c) : controller (c)
    {
        setWantsKeyboardFocus (true);
        setTitle ("Pattern grid");
        setDescription ("Use the arrow keys to move between steps and Return to toggle a step. Select with Shift-click or a long press.");
        setTooltip ("Drag an active step edge for a velocity line. Double-click or double-tap the edge to draw velocity.");
        for (int ch = 0; ch < Constants::maxChannels; ++ch)
            for (int step = 0; step < Constants::maxSteps; ++step)
            {
                const Location location { ch, step };
                auto& cell = accessibleCells[static_cast<size_t> (location.key())];
                addAndMakeVisible (cell);
                cell.setTitle ("Channel " + juce::String (ch + 1) + ", step " + juce::String (step + 1));
                cell.setDescription (accessibleDescription (displayedCells[static_cast<size_t> (location.key())], false));
                cell.onClick = [this, location] { toggleCell (location); };
                cell.navigate = [this, location] (const juce::KeyPress& key) { return handleCellKey (location, key); };
            }
        for (int ch = 0; ch < 8; ++ch)
        {
            auto& r = rows[static_cast<size_t> (ch)];
            const std::array<std::pair<juce::TextButton*, const char*>, 4> icons { { { &r.clear, "clear" }, { &r.reverse, "forward" }, { &r.ping, "ping" }, { &r.mute, "power" } } };
            for (auto [button, icon] : icons)
            {
                addAndMakeVisible (*button);
                button->getProperties().set ("icon", icon);
                button->setTitle (juce::String (icon) + " channel " + juce::String (ch + 1));
            }
            addAndMakeVisible (r.label);
            addAndMakeVisible (r.note);
            addChildComponent (r.more);
            r.more.setTitle ("Settings channel " + juce::String (ch + 1));
            r.more.onClick = [this, ch] { if (channelSettings) channelSettings (ch); };
            r.label.setTitle ("Play track " + juce::String (ch + 1));
            r.label.setButtonText (juce::String (ch + 1));
            r.label.getProperties().set ("rowNumber", true);
            r.label.setTooltip ("Triggers this row. Records when armed.");
            r.label.onClick = [this, ch, &r]
            {
                if (controller.isControllerRow (ch))
                {
                    if (layout.compact && channelSettings)
                        channelSettings (ch);
                    else if (noteChooserRequested)
                        noteChooserRequested (ch, r.note);
                }
                else if (rowTriggered)
                    rowTriggered (ch);
            };
            r.label.showColourPicker = [this, ch]
            {
                if (!layout.compactOnPhone && trackColourRequested)
                    trackColourRequested (ch, rows[static_cast<size_t> (ch)].label);
            };
            r.clear.getProperties().set ("rowClear", true);
            r.clear.setTooltip ("Double-click to clear row");
            r.clear.onClick = [this, ch, &r]
            {
                if (r.clearFlash.confirm())
                    controller.clearRow (ch);
            };
            r.note.setTitle ("MIDI note channel " + juce::String (ch + 1));
            r.note.insetRowArtwork = true;
            r.note.format = [] (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, 4); };
            r.note.onChoose = [this, ch, &r] { if (noteChooserRequested) noteChooserRequested (ch, r.note); };
            r.mute.onClick = [this, ch] { if (!controller.exitSolo()) controller.toggleMute (ch); };
            r.reverse.onClick = [this, ch] { toggleDirection (ch, true); };
            r.ping.onClick = [this, ch] { toggleDirection (ch, false); };
            r.reverse.setTooltip ("Click to toggle Reverse. Drag across rows to set a range and drag back to shrink it.");
            r.ping.setTooltip ("Click to toggle Ping-Pong. Drag across rows to set a range and drag back to shrink it.");
            const std::array controls {
                std::pair { &r.mute, RowControl::power },
                std::pair { &r.reverse, RowControl::reverse },
                std::pair { &r.ping, RowControl::pingPong }
            };
            for (auto [button, control] : controls)
            {
                button->pressed = [this, ch, control] (const juce::MouseEvent& event)
                {
                    beginRowSwipe (ch, control, event);
                };
                button->dragged = [this, ch, control] (const juce::MouseEvent& event)
                {
                    if (rowSwipeMatches (ch, control, event))
                        updateRowSwipe (event);
                };
                button->released = [this, ch, control, button] (const juce::MouseEvent& event)
                {
                    if (rowSwipeMatches (ch, control, event))
                        endRowSwipe (event, *button);
                };
            }
        }
    }

    void PatternGrid::toggleDirection (int channel, bool flipReverse)
    {
        const auto mode = controller.state.playback.channels[static_cast<size_t> (channel)].playbackMode;
        setDirection (channel, flipReverse, mode != PlaybackMode::reverse_ping_pong
            && mode != (flipReverse ? PlaybackMode::reverse : PlaybackMode::ping_pong));
    }

    void PatternGrid::setDirection (int channel, bool flipReverse, bool enabled)
    {
        const auto mode = controller.state.playback.channels[static_cast<size_t> (channel)].playbackMode;
        const bool reverse = flipReverse ? enabled : mode == PlaybackMode::reverse || mode == PlaybackMode::reverse_ping_pong;
        const bool ping = !flipReverse ? enabled : mode == PlaybackMode::ping_pong || mode == PlaybackMode::reverse_ping_pong;
        const auto next = reverse ? (ping ? PlaybackMode::reverse_ping_pong : PlaybackMode::reverse)
                                  : (ping ? PlaybackMode::ping_pong : PlaybackMode::normal);
        if (next != mode)
            controller.command ("channel_playback_mode", { channel + 1, playbackModeToString (next) }, "Playback direction");
    }

    void PatternGrid::beginRowSwipe (int channel, RowControl control, const juce::MouseEvent& event)
    {
        if (!event.mods.isLeftButtonDown() || event.mods.isPopupMenu() || controller.source() < 0 || rowSwipe)
            return;

        const bool power = control == RowControl::power;
        const bool extendSolo = power && controller.keyboardModifiersEnabled && event.mods.isShiftDown();
        rowSwipe = RowSwipe { event.source, channel, controller.source(), controller.state.documentEpoch, control, extendSolo };
        if (!power)
            return;
        const auto press = ++soloPress;
        if (layout.compactOnPhone || extendSolo)
            return;

        const juce::Component::SafePointer<PatternGrid> safe (this);
        juce::Timer::callAfterDelay (soloHoldMs, [safe, press]
        {
            if (safe != nullptr && safe->soloPress == press)
                safe->holdRowSolo();
        });
    }

    bool PatternGrid::rowSwipeMatches (int channel, RowControl control, const juce::MouseEvent& event) const
    {
        return rowSwipe && rowSwipe->channel == channel && rowSwipe->control == control && rowSwipe->pointer == event.source;
    }

    bool PatternGrid::rowSwipeCurrent (const RowSwipe& swipe) const
    {
        return swipe.source == controller.source() && swipe.epoch == controller.processor.getUiBridge().getDocumentEpoch();
    }

    void PatternGrid::holdRowSolo()
    {
        if (rowSwipe && rowSwipe->control == RowControl::power && rowSwipe->clickPending && rowSwipeCurrent (*rowSwipe))
        {
            rowSwipe->clickPending = false;
            controller.toggleSolo (rowSwipe->channel);
        }
    }

    bool PatternGrid::rowControlValue (const RowSwipe& swipe, int channel) const
    {
        const auto index = static_cast<size_t> (channel);
        const auto& playback = controller.state.playback;
        if (swipe.control == RowControl::power)
            return playback.sourceChannelMutes[static_cast<size_t> (swipe.source)][index];

        const auto mode = playback.channels[index].playbackMode;
        return mode == PlaybackMode::reverse_ping_pong
            || mode == (swipe.control == RowControl::reverse ? PlaybackMode::reverse : PlaybackMode::ping_pong);
    }

    void PatternGrid::updateRowSwipe (const juce::MouseEvent& event)
    {
        if (!rowSwipeCurrent (*rowSwipe)
            || (rowSwipe->endRow < 0 && (!rowSwipe->clickPending || event.getDistanceFromDragStart() < 4)))
            return;

        const int channel = rowSwipe->channel;
        const auto control = rowSwipe->control;
        const bool power = control == RowControl::power;
        if (power)
            ++soloPress;

        const float y = event.getEventRelativeTo (this).position.y;
        const int first = layout.compact ? layout.firstRow : 0;
        const int last = layout.compact ? juce::jmin (layout.channels, first + layout.compactVisibleRows) - 1 : layout.channels - 1;
        const int anchorRow = layout.displayRow (channel);
        const int hitChannel = layout.channelAtY (y);
        const int endRow = hitChannel >= 0 ? layout.displayRow (hitChannel) : y < layout.cellsY() ? first : last;
        const int previousEnd = rowSwipe->endRow;
        if (previousEnd == endRow)
            return;

        if (previousEnd < 0)
        {
            if (endRow == anchorRow)
                return;
            rowSwipe->clickPending = false;
            if (power && (rowSwipe->extendSolo || controller.soloedChannels().any()))
                return;
            controller.begin (power ? "Set row power" : control == RowControl::reverse ? "Set row reverse" : "Set row ping-pong");
            if (!rowSwipe || !rowSwipeCurrent (*rowSwipe))
                return;
            for (int ch = 0; ch < layout.channels; ++ch)
                rowSwipe->initialValues[static_cast<size_t> (ch)] = rowControlValue (*rowSwipe, ch);
        }
        rowSwipe->endRow = endRow;

        const auto inRange = [anchorRow] (int row, int end)
        {
            return end >= 0 && row >= juce::jmin (anchorRow, end) && row <= juce::jmax (anchorRow, end);
        };
        const bool targetValue = !rowSwipe->initialValues[static_cast<size_t> (channel)];
        for (int row = first; row <= last && rowSwipe && rowSwipeCurrent (*rowSwipe); ++row)
        {
            const bool inside = inRange (row, endRow);
            if (inside == inRange (row, previousEnd))
                continue;
            const int rowChannel = layout.channelForRow (row);
            const bool initial = rowSwipe->initialValues[static_cast<size_t> (rowChannel)];
            if (!inside && initial == targetValue)
                continue;

            const bool value = inside ? targetValue : initial;
            if (rowControlValue (*rowSwipe, rowChannel) == value)
                continue;
            if (power)
                controller.command ("source_channel_mute", { rowSwipe->source + 1, rowChannel + 1, value });
            else
                setDirection (rowChannel, control == RowControl::reverse, value);
        }
    }

    void PatternGrid::endRowSwipe (const juce::MouseEvent& event, const RowPlaybackButton& button)
    {
        updateRowSwipe (event);
        const auto swipe = std::exchange (rowSwipe, std::nullopt);
        if (!swipe)
            return;
        if (swipe->endRow >= 0)
        {
            controller.commit();
            return;
        }
        if (!swipe->clickPending || !rowSwipeCurrent (*swipe) || !button.getLocalBounds().contains (event.getPosition()))
            return;
        if (swipe->extendSolo)
            controller.toggleSolo (swipe->channel, true);
        else if (swipe->control == RowControl::power && !layout.compactOnPhone
                 && event.getDistanceFromDragStart() < 4 && event.getLengthOfMousePress() >= soloHoldMs)
            controller.toggleSolo (swipe->channel);
        else
            button.onClick();
    }

    juce::Rectangle<float> PatternGrid::loopHandleBounds (int channel, bool end) const
    {
        const auto loop = controller.loop (channel);
        const auto first = layout.cell ({ channel, loop.loopStart });
        const float edge = velocityEdgeX ({ channel, loop.loopStart + (end ? loop.loopLength : 0) });
        const float x = edge + (end ? Layout::rowVelocityGutter : -Layout::loopEdgeSpace);
        return { x, first.getCentreY() - Layout::touchTarget * 0.5f, Layout::loopGripWidth, Layout::touchTarget };
    }
    std::optional<bool> PatternGrid::loopHandleAt (juce::Point<float> point) const
    {
        const int row = layout.channelAtY (point.y);
        if (layout.compact || row < 0 || controller.source() < 0 || overviewLayer (displayLayer()))
            return {};
        if (loopHandleBounds (row, false).contains (point))
            return false;
        if (loopHandleBounds (row, true).contains (point))
            return true;
        return {};
    }

    void PatternGrid::updateCursor()
    {
        using Cursor = juce::MouseCursor;
        switch (drag)
        {
            case Drag::copy:         setMouseCursor (Cursor::CopyingCursor); return;
            case Drag::move:         setMouseCursor (Cursor::UpDownLeftRightResizeCursor); return;
            case Drag::value:        setMouseCursor (Cursor::UpDownResizeCursor); return;
            case Drag::paint:        setMouseCursor (Cursor::NormalCursor); return;
            case Drag::marquee:
            case Drag::velocityLine: setMouseCursor (Cursor::CrosshairCursor); return;
            case Drag::velocityDraw:
                setMouseCursor (pointerInGrid && layout.at (hoverPosition).channel == anchor.channel
                    ? Cursor::CrosshairCursor : Cursor::NormalCursor);
                return;
            case Drag::loopStart:
            case Drag::loopEnd:      setMouseCursor (horizontalResizeCursor()); return;
            case Drag::pending:
            case Drag::none: break;
        }

        if (pointerInGrid && loopHandleAt (hoverPosition))
        {
            setMouseCursor (horizontalResizeCursor());
            return;
        }

        if (pointerInGrid && controller.source() >= 0 && !overviewLayer (displayLayer()) && isCopyMode())
        {
            const auto cell = cellAt (hoverPosition);
            if (cell.valid() && controller.selected.test (static_cast<size_t> (cell.key())))
            {
                setMouseCursor (Cursor::CopyingCursor);
                return;
            }
        }

        setMouseCursor (Cursor::NormalCursor);
    }

    void PatternGrid::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        if (layout.compact && layout.compactVisibleRows < layout.channels && scrollRows && std::abs (wheel.deltaY) > 0.0001f)
            scrollRows (wheel.deltaY < 0 ? 1 : -1);
    }
    juce::Rectangle<float> PatternGrid::cellBounds (Location location) const
    {
        const auto& cell = controller.cell (location);
        const auto bounds = layout.cell (location);
        if (cell.enabled)
            return bounds.withX (hitPosition (location).x).withWidth (bounds.getWidth() * static_cast<float> (displayedSpan (location)));
        return location.valid() && controller.selected.test (static_cast<size_t> (location.key())) ? emptyStepBounds (location) : bounds;
    }
    int PatternGrid::displayedSpan (Location location) const
    {
        const auto& cell = controller.cell (location);
        return cell.enabled && cell.span > 1 && controller.canFitSpan (location, cell.span) ? cell.span : 1;
    }
    Location PatternGrid::coveringSpan (Location location) const
    {
        const auto loop = controller.loop (location.channel);
        for (int step = location.step - 1; step >= loop.loopStart; --step)
        {
            const Location previous { location.channel, step };
            if (controller.cell (previous).enabled)
                return step + displayedSpan (previous) > location.step ? previous : Location {};
        }
        return {};
    }
    int PatternGrid::playbackPositionForCell (Location location) const
    {
        const auto loop = controller.loop (location.channel);
        const int offset = location.step - loop.loopStart;
        if (offset < 0 || offset >= loop.loopLength)
            return location.step;
        const auto mode = controller.state.playback.channels[static_cast<size_t> (location.channel)].playbackMode;
        const int length = juce::jmax (1, loop.loopLength);
        // Keep the editing geometry fixed to the first pass through this channel.
        // Later visits can have a different swing phase for odd loops or ping-pong.
        switch (mode)
        {
            case PlaybackMode::normal: return offset;
            case PlaybackMode::reverse: return length - 1 - offset;
            case PlaybackMode::ping_pong:
                return offset;
            case PlaybackMode::reverse_ping_pong:
                return length - 1 - offset;
        }
        return location.step;
    }
    juce::Point<float> PatternGrid::hitPosition (Location location, int rollIndex) const
    {
        const auto& cell = controller.cell (location);
        const int roll = juce::jlimit (1, Constants::maxRoll, controller.splitCount (location));
        const double part = static_cast<double> (juce::jlimit (0, roll - 1, rollIndex)) / static_cast<double> (roll);
        const int span = displayedSpan (location);
        const double offset = static_cast<double> (cell.nudge) / 100.0 + part * span + (span == 1 ? displayedSwingDelay (location, part) : 0.0);
        return layout.cell (location).getTopLeft().translated (static_cast<float> (offset) * layout.cellWidth(), 0);
    }
    double PatternGrid::displayedSwingDelay (Location location, double part) const
    {
        const auto& playback = controller.state.playback;
        return layout.showSwing ? swingDelayStepsForPosition (playbackPositionForCell (location) + part,
                                                             playback.swing, playback.swingSubdivisionIndex) : 0.0;
    }
    juce::Rectangle<float> PatternGrid::cellPaintBounds (Location location) const
    {
        return cellBounds (location).expanded (selectionGlowExtent);
    }

    juce::Path PatternGrid::makeControllerCurve (int channel) const
    {
        const auto& playback = controller.state.playback;
        const auto loop = controller.loop (channel);
        const auto mode = playback.channels[static_cast<size_t> (channel)].playbackMode;
        const bool reverse = mode == PlaybackMode::reverse || mode == PlaybackMode::reverse_ping_pong;
        const double beatsPerStep = playback.beatsPerStep;
        struct Point
        {
            double beat, endBeat;
            int value, endValue, intervals, order;
        };
        std::vector<Point> points;
        std::optional<Point> outgoing;
        bool previousGlides = false;

        // The editor shows the first pass through the row. Visit the adjoining
        // passes too so ramps that cross the edge retain their actual phase.
        for (int pass = -1; pass <= 1; ++pass)
        {
            const bool backwards = reverse != (pass != 0 && playbackModeIsPingPong (mode));
            for (int offset = 0; offset < loop.loopLength; ++offset)
            {
                const int step = pass * loop.loopLength + offset;
                Location location { channel, loop.loopStart + (backwards ? loop.loopLength - 1 - offset : offset) };
                if (backwards)
                {
                    if (controller.cell (location).enabled && displayedSpan (location) > 1)
                        continue;
                    if (const auto spanStart = coveringSpan (location); spanStart.valid()
                        && spanStart.step + displayedSpan (spanStart) - 1 == location.step)
                        location = spanStart;
                }
                auto cell = controller.cell (location);
                if (!cell.enabled)
                    continue;
                cell.span = displayedSpan (location);
                for (int part = 0; part < cell.roll; ++part)
                {
                    const auto beat = (step + splitOffsetInSteps (cell, part, step, playback.swing,
                                                                 playback.swingSubdivisionIndex)) * beatsPerStep;
                    const auto value = cell.roll == 1 ? cell.controllerValue() : cell.splitControllerValue (part);
                    const bool guaranteed = cell.controllerPointUnconditional (part);
                    if (outgoing)
                    {
                        if (previousGlides && guaranteed && beat > outgoing->beat)
                        {
                            outgoing->endBeat = beat;
                            outgoing->endValue = value;
                            outgoing->intervals = controllerGlideIntervals (beat - outgoing->beat, outgoing->value, value);
                        }
                        points.push_back (*outgoing);
                        outgoing.reset();
                    }
                    const auto& gate = cell.splitGates[static_cast<size_t> (part)];
                    if (cell.probability > 0 && cell.cycleMask != 0 && (cell.roll == 1 || (gate.probability() > 0 && gate.cycleMask() != 0)))
                        outgoing = Point { beat, beat, value, value, 0, static_cast<int> (points.size()) };
                    previousGlides = cell.ccGlide != 0 && guaranteed;
                }
            }
        }
        if (outgoing)
            points.push_back (*outgoing);
        if (points.empty())
            return {};

        // A nudge can change the order of two onsets. Playback sends the new
        // point at that time and stops any preceding ramp.
        std::sort (points.begin(), points.end(), [] (const auto& a, const auto& b)
        {
            return std::tie (a.beat, a.order) < std::tie (b.beat, b.order);
        });
        juce::Path path;
        auto value = points.front().value;
        const auto loopEnd = loop.loopLength * beatsPerStep;
        const auto changeAt = [&] (double beat, int next)
        {
            // Fold the opening change into the initial value and stop the final
            // hold at the loop edge so neither edge shows the wraparound jump.
            if (beat >= loopEnd)
                return;
            if (beat > 0.0)
            {
                if (path.isEmpty())
                    path.startNewSubPath (0.0f, static_cast<float> (value));
                const auto x = static_cast<float> (beat);
                path.lineTo (x, static_cast<float> (value));
                path.lineTo (x, static_cast<float> (next));
            }
            value = next;
        };
        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto& point = points[i];
            changeAt (point.beat, point.value);
            for (int update = 1; update < point.intervals; ++update)
            {
                const auto fraction = static_cast<double> (update) / point.intervals;
                const auto beat = point.beat + (point.endBeat - point.beat) * fraction;
                if (i + 1 < points.size() && beat >= points[i + 1].beat)
                    break;
                changeAt (beat, controllerGlideValue (point.value, point.endValue, fraction));
            }
        }
        if (path.isEmpty())
            path.startNewSubPath (0.0f, static_cast<float> (value));
        path.lineTo (static_cast<float> (loopEnd), static_cast<float> (value));
        return path;
    }
    juce::Rectangle<float> PatternGrid::emptyStepBounds (Location location) const
    {
        return layout.cell (location).translated (static_cast<float> (displayedSwingDelay (location)) * layout.cellWidth(), 0.0f);
    }
    void PatternGrid::clearTouchStepPreview()
    {
        if (const auto location = std::exchange (touchStepPreview, {}); location.valid())
            repaint (emptyStepBounds (location).expanded (3.0f).getSmallestIntegerContainer());
    }
    void PatternGrid::cancelTouchStepPress()
    {
        if (touchStepPreview.valid())
        {
            stopTimer();
            drag = Drag::none;
        }
        cancelPresetHold();
    }
    Location PatternGrid::cellAt (juce::Point<float> point) const
    {
        // Match painted hits first, then swung empty selections, then the beat slot.
        const int channel = layout.channelAtY (point.y);
        if (channel >= 0 && point.x >= layout.gridLeft() && point.x < layout.gridLeft() + layout.gridWidth())
        {
            const auto loop = controller.loop (channel);
            Location emptySelection;
            const bool matchEmptySelection = layout.showSwing && controller.state.playback.swing > 0;
            for (int step = juce::jmin (loop.loopStart + loop.loopLength, layout.firstStep + layout.visibleSteps()) - 1; step >= loop.loopStart; --step)
            {
                const Location location { channel, step };
                if (controller.cell (location).enabled)
                {
                    if ((layout.stepVisible (step) || displayedSpan (location) > 1) && cellBounds (location).contains (point))
                        return location;
                }
                else if (matchEmptySelection && !emptySelection.valid() && layout.stepVisible (step)
                         && controller.selected.test (static_cast<size_t> (location.key()))
                         && cellBounds (location).contains (point) && !coveringSpan (location).valid())
                    emptySelection = location;
            }
            if (emptySelection.valid())
                return emptySelection;
        }
        const auto slot = layout.at (point);
        const auto owner = slot.valid() ? coveringSpan (slot) : Location {};
        return owner.valid() ? owner : slot;
    }
    Layer PatternGrid::displayLayer() const
    {
        if (drag != Drag::none && drag != Drag::marquee && drag != Drag::move && drag != Drag::copy)
            return dragLayer;
        return controller.inspectorHoverLayer.value_or (controller.chanceHeld
            || (controller.optionDown (modifiers) && controller.selected.none()) ? Layer::chance : controller.layer);
    }
    bool PatternGrid::isCopyMode() const
    {
        return controller.selected.any() && (copySelection || controller.optionDown (modifiers));
    }
    void PatternGrid::modifierKeysChanged (const juce::ModifierKeys& m)
    {
        if (modifiers != m)
        {
            const auto previousLayer = displayLayer();
            modifiers = m;
            if (drag == Drag::move || drag == Drag::copy)
                drag = isCopyMode() ? Drag::copy : Drag::move;
            if (hoverEdge && velocityHintAt (hoverPosition) != hoverEdge)
                clearHoverMarker();
            repaint();
            if (displayLayer() != previousLayer && layerChanged)
                layerChanged();
        }
        updateCursor();
    }
    std::optional<PatternGrid::VelocityEdge> PatternGrid::velocityHintAt (juce::Point<float> point) const
    {
        if (controller.source() < 0 || displayLayer() != Layer::velocity || controller.selected.any()
            || controller.shiftDown (modifiers) || modifiers.isAnyMouseButtonDown() || loopHandleAt (point).has_value())
            return {};
        return velocityEdgeAt (point);
    }
    juce::Point<float> PatternGrid::hoverMarkerPoint() const
    {
        return { velocityEdgeX (*hoverEdge), hoverPosition.y };
    }
    std::optional<PatternGrid::VelocityEdge> PatternGrid::velocityEdgeAt (juce::Point<float> point, bool touch) const
    {
        const int channel = layout.channelAtY (point.y);
        if (layout.compact || channel < 0)
            return {};
        const auto loop = controller.loop (channel);
        int hits = 0;
        for (int step = loop.loopStart; step < loop.loopStart + loop.loopLength && hits < 2; ++step)
        {
            const auto& cell = controller.cell ({ channel, step });
            if (cell.enabled)
                hits += controller.splitCount ({ channel, step });
        }
        if (hits < 2)
            return {};
        const auto row = layout.cell ({ channel, layout.firstStep });
        if (point.y < row.getY() || point.y >= row.getBottom())
            return {};
        const float fraction = layout.cellWidth() < 36.0f ? 0.38f : touch ? 0.30f : 0.25f;
        const float radius = juce::jmin (touch ? touchVelocityEdgeHitRadius : velocityEdgeHitRadius, layout.cellWidth() * fraction);
        return nearestVelocityEdge (channel, point.x, true, radius, touch);
    }
    float PatternGrid::velocityEdgeX (VelocityEdge edge) const
    {
        const auto loop = controller.loop (edge.channel);
        const int loopEnd = loop.loopStart + loop.loopLength;
        const Location right { edge.channel, edge.step };
        if (edge.split > 0)
            return splitSegment (layout.cellArtwork (cellBounds (right)), controller.cell (right).roll, edge.split).getX();
        if (right.step >= loop.loopStart && right.step < loopEnd && controller.cell (right).enabled)
        {
            const float x = cellBounds (right).getX();
            return right.step == loop.loopStart ? juce::jlimit (layout.gridLeft(), layout.gridLeft() + layout.gridWidth(), x) : x;
        }
        if (edge.step > loop.loopStart && edge.step <= loopEnd)
        {
            const Location left { edge.channel, edge.step - 1 };
            const auto previous = controller.cell (left).enabled ? left : coveringSpan (left);
            if (previous.valid())
            {
                const float x = cellBounds (previous).getRight();
                return edge.step == loopEnd ? juce::jlimit (layout.gridLeft(), layout.gridLeft() + layout.gridWidth(), x) : x;
            }
        }
        return layout.cell (right).getX();
    }
    bool PatternGrid::isVelocityGutter (VelocityEdge edge, float x) const
    {
        if (edge.split > 0)
            return false;
        const auto loop = controller.loop (edge.channel);
        if (edge.step == loop.loopStart)
            return x < velocityEdgeX (edge);
        if (edge.step == loop.loopStart + loop.loopLength)
            return x > velocityEdgeX (edge);
        return false;
    }
    std::optional<PatternGrid::VelocityEdge> PatternGrid::nearestVelocityEdge (int channel, float x, bool start, float distance, bool touch) const
    {
        const auto loop = controller.loop (channel);
        const int loopEnd = loop.loopStart + loop.loopLength;
        const int first = layout.firstStep;
        const int last = first + layout.visibleSteps();
        const float gridRight = layout.gridLeft() + layout.gridWidth();
        std::optional<VelocityEdge> nearest;
        const auto consider = [&] (VelocityEdge edge, float grabRadius)
        {
            const float edgeX = velocityEdgeX (edge);
            const float delta = std::abs (x - edgeX);
            if (edgeX >= layout.gridLeft() && edgeX <= gridRight && delta < distance && delta < grabRadius)
            {
                nearest = edge;
                distance = delta;
            }
        };
        // Later steps paint over earlier steps when two shifted edges coincide.
        for (int step = last; step >= juce::jmin (first, loop.loopStart); --step)
        {
            const Location location { channel, step };
            const bool inLoop = step >= loop.loopStart && step < loopEnd;
            const bool active = inLoop && controller.cell (location).enabled;
            if (step >= first && (!inLoop || !coveringSpan (location).valid()))
            {
                const Location left { channel, step - 1 };
                Location previous;
                if (start && !active && step > loop.loopStart && step <= loopEnd)
                    previous = controller.cell (left).enabled ? left : coveringSpan (left);
                if (!start || active || previous.valid() || isVelocityGutter ({ channel, step }, x))
                {
                    float radius = distance;
                    const bool loopBoundary = step == loop.loopStart || step == loopEnd;
                    if (start && touch && layout.touchRows && loopBoundary && !isVelocityGutter ({ channel, step }, x))
                        radius = juce::jmin (radius, layout.cellWidth() * 0.32f);
                    const auto hit = active ? location : previous;
                    if (start && hit.valid() && controller.splitCount (hit) > 1)
                    {
                        const auto raw = cellBounds (hit);
                        const auto body = layout.cellArtwork (raw);
                        const float width = body.getWidth() / static_cast<float> (controller.splitCount (hit));
                        radius = juce::jmin (radius, body.getX() - raw.getX() + width * (touch ? 0.3f : 0.25f));
                    }
                    consider ({ channel, step }, radius);
                }
            }
            // On a tablet, a hold at a split divider selects the clip. Dividers still snap line ends.
            if (active && !(start && touch && layout.touchRows && !layout.compact))
            {
                const auto roll = controller.splitCount (location);
                if (roll < 2)
                    continue;
                const auto segment = splitSegment (layout.cellArtwork (cellBounds (location)), roll, 0);
                const float radius = start ? segment.getWidth() * (touch ? 0.3f : 0.25f) : distance;
                for (int split = roll - 1; split > 0; --split)
                    consider ({ channel, step, split }, radius);
            }
        }
        return nearest;
    }
    PatternGrid::VelocityEdge PatternGrid::velocityLineEnd() const
    {
        if (std::abs (current.x - origin.x) < 4.0f)
            return lineStart;
        return nearestVelocityEdge (anchor.channel, current.x, false, std::numeric_limits<float>::infinity()).value_or (lineStart);
    }
    void PatternGrid::clearHoverMarker()
    {
        hoverMarkerTimer.stopTimer();
        if (std::exchange (hoverEdge, std::nullopt))
        {
            repaint();
        }
    }
    void PatternGrid::mouseEnter (const juce::MouseEvent& e) { mouseMove (e); }
    void PatternGrid::mouseMove (const juce::MouseEvent& e)
    {
        if (!e.source.canHover() || (gesturePointer && *gesturePointer != e.source))
            return;
        if (drag == Drag::velocityDraw)
        {
            pointerInGrid = true;
            hoverPosition = e.position;
            updateCursor();
            return;
        }
        if (presetMenu != nullptr)
            presetMenu->leaveAnchor (e.getPosition());
        const auto previousRow = pointerInGrid ? layout.channelAtY (hoverPosition.y) : -1;
        const auto previousHandle = pointerInGrid ? loopHandleAt (hoverPosition) : std::nullopt;
        pointerInGrid = true;
        hoverPosition = e.position;
        modifierKeysChanged (e.mods);
        const auto handle = loopHandleAt (hoverPosition);
        if (handle != previousHandle || (handle && previousRow != layout.channelAtY (hoverPosition.y)))
            repaint();
        if (drag != Drag::none)
        {
            clearHoverMarker();
            return;
        }
        const auto nextHoverEdge = velocityHintAt (e.position);
        if (nextHoverEdge && nextHoverEdge == hoverEdge)
        {
            repaint();
            return;
        }
        clearHoverMarker();
        hoverEdge = nextHoverEdge;
        if (hoverEdge)
        {
            hoverMarkerStarted = juce::Time::getMillisecondCounterHiRes();
            hoverMarkerTimer.startTimerHz (30);
        }
    }
    void PatternGrid::mouseExit (const juce::MouseEvent& e)
    {
        if (!e.source.canHover() || (gesturePointer && *gesturePointer != e.source))
            return;
        if (drag == Drag::velocityDraw)
            velocityDrawPoint.reset();
        if (presetMenu != nullptr)
            presetMenu->leaveAnchor (e.getPosition());
        if (pointerInGrid && loopHandleAt (hoverPosition))
            repaint();
        pointerInGrid = false;
        updateCursor();
        clearHoverMarker();
    }
    void PatternGrid::sync()
    {
        const auto& p = controller.state.playback;
        const std::pair swing { p.swing, p.swingSubdivisionIndex };
        const bool swingChanged = displayedSwing != swing;
        const bool controllerTimingChanged = !juce::approximatelyEqual (controllerCurveRate, p.beatsPerStep);
        controllerCurveRate = p.beatsPerStep;
        layout.channels = p.channelCount;
        layout.steps = p.stepCount;
        const bool patternChanged = displayedSource != controller.source() || displayedEpoch != controller.state.documentEpoch;
        const bool layoutChanged = laidOut.has_value() && laidOut->first != layout;
        if (patternChanged || layoutChanged)
        {
            updateVelocityAnchorPulse (true);
            velocityTapTimer.stopTimer();
            velocityTap.reset();
            cancelTouchStepPress();
            cyclePress.reset();
            if (const auto split = std::exchange (splitPress, std::nullopt); split && split->scrubbing)
                controller.cancel();
        }
        if (drag == Drag::velocityDraw && (patternChanged || layoutChanged || !controller.editing()))
            finishVelocityDrawing();
        if (presetMenu != nullptr)
            presetMenu->refresh();
        if (patternChanged)
        {
            lastHandleClick.reset();
            displayedSource = controller.source();
            displayedEpoch = controller.state.documentEpoch;
            rowSwipe.reset();
            for (auto& row : rows)
                row.clearFlash.cancel();
        }
        if (hoverEdge && velocityHintAt (hoverPosition) != hoverEdge)
            clearHoverMarker();
        const auto splitCycle = controller.selectedSplitCycle();
        // Quarter-note triplets land on a beat every three steps, spanning two beats.
        const int beatStride = Layout::beatAccentStride (p.beatsPerStep);
        bool fullPaint = displayedFirstStep != layout.firstStep || displayedSteps != layout.steps || displayedLayer != displayLayer() || displayedColors != controller.drumColors
                               || displayedGraphicalValues != controller.graphicalCellValues || displayedBeatStride != beatStride
                               || displayedSelection.any() != controller.selected.any() || displayedSplitCycle != splitCycle
                               || !laidOut.has_value() || laidOut->first != layout || swingChanged;
        displayedSplitCycle = splitCycle;
        displayedSwing = swing;
        displayedBeatStride = beatStride;
        displayedSteps = layout.steps;
        displayedFirstStep = layout.firstStep;
        displayedLayer = displayLayer();
        displayedColors = controller.drumColors;
        displayedGraphicalValues = controller.graphicalCellValues;
        const auto solo = controller.soloedChannels();
        for (int ch = 0; ch < 8; ++ch)
        {
            auto& r = rows[static_cast<size_t> (ch)];
            const auto& channel = p.channels[static_cast<size_t> (ch)];
            const bool visible = layout.channelVisible (ch);
            for (auto* button : std::array<juce::TextButton*, 4> { &r.clear, &r.reverse, &r.ping, &r.mute })
            {
                button->setVisible (visible && !(layout.phoneRowControls && (button == &r.reverse || button == &r.ping)));
                button->setEnabled (controller.source() >= 0);
            }
            r.label.setVisible (visible);
            r.label.setEnabled (p.deviceActive || channel.controller);
            r.label.allowColourPickerHold = layout.touchRows && !layout.compactOnPhone;
            r.label.setTooltip (channel.controller ? "Edit CC row settings."
                : layout.compactOnPhone ? "Triggers this row. Records when armed."
                                                        : "Tap to trigger this row. Right-click or hold to choose its color.");
            r.label.setColour (juce::TextButton::textColourOffId, Palette::channel (ch, controller.drumColors));
            r.note.setVisible (visible && !layout.compact);
            r.more.setVisible (visible && layout.compact);
            r.note.setValue (channel.controller ? channel.ccNumber : channel.note);
            const int ccMode = channel.controller ? 1 + static_cast<int> (channel.playbackMode) : 0;
            if (displayedCcModes[static_cast<size_t> (ch)] != ccMode)
            {
                displayedCcModes[static_cast<size_t> (ch)] = ccMode;
                r.note.setStackedCcLabel (channel.controller);
                r.note.setTitle ((channel.controller ? "MIDI CC row " : "MIDI note channel ") + juce::String (ch + 1));
                r.label.setTitle ((channel.controller ? "Edit CC track " : "Play track ") + juce::String (ch + 1));
                r.note.repaint();
                fullPaint = true;
            }
            r.more.setButtonText (channel.controller ? "CC" : "...");
            if (displayedNames != controller.noteNames)
                r.note.repaint();
            const auto rev = channel.playbackMode == PlaybackMode::reverse || channel.playbackMode == PlaybackMode::reverse_ping_pong;
            const auto ping = channel.playbackMode == PlaybackMode::ping_pong || channel.playbackMode == PlaybackMode::reverse_ping_pong;
            r.reverse.getProperties().set ("icon", rev ? "reverse" : "forward");
            r.reverse.setButtonText (rev ? juce::String::fromUTF8 ("←") : juce::String::fromUTF8 ("→"));
            r.reverse.setToggleState (rev, juce::dontSendNotification);
            r.ping.setToggleState (ping, juce::dontSendNotification);
            const auto range = controller.loop (ch);
            const auto index = static_cast<size_t> (ch);
            const bool muted = controller.source() >= 0 && p.sourceChannelMutes[static_cast<size_t> (controller.source())][index];
            const auto loopEdges = layout.compact ? std::pair<float, float> {} : std::pair {
                velocityEdgeX ({ ch, range.loopStart }), velocityEdgeX ({ ch, range.loopStart + range.loopLength }) };
            if (displayedLoopStart[index] != range.loopStart || displayedLoopLength[index] != range.loopLength
                || displayedLoopEdges[index] != loopEdges || displayedMutes[index] != muted)
                repaint (layout.cell ({ ch, 0 }).withX (layout.gridLeft()).withWidth (layout.gridWidth())
                             .expanded (Layout::loopEdgeSpace + 2.0f, selectionGlowExtent).getSmallestIntegerContainer());
            displayedLoopStart[index] = range.loopStart;
            displayedLoopLength[index] = range.loopLength;
            displayedLoopEdges[index] = loopEdges;
            displayedMutes[index] = muted;
            r.mute.setToggleState (controller.source() >= 0 && !muted, juce::dontSendNotification);
            if (r.mute.getProperties().set ("soloActive", solo.test (index)))
            {
                r.mute.repaint();
            }
            const juce::String muteTip = solo.any() ? "Click to exit solo." : "Click to toggle this row. Drag to set a range and drag back to shrink it.";
            r.mute.setTooltip (layout.compactOnPhone ? muteTip : muteTip + " Hold to solo."
                + (controller.keyboardModifiersEnabled ? " Shift-click to add or remove a row from the solo group." : ""));
        }
        for (int ch = 0; ch < Constants::maxChannels; ++ch)
        {
            auto& curve = controllerCurves[static_cast<size_t> (ch)];
            const auto range = controller.loop (ch);
            const auto& channel = p.channels[static_cast<size_t> (ch)];
            const int mode = channel.controller && controller.source() >= 0 ? 1 + static_cast<int> (channel.playbackMode) : 0;
            bool curveChanged = curve.loop != range || curve.mode != mode || patternChanged || swingChanged || controllerTimingChanged;
            for (int step = 0; step < Constants::maxSteps; ++step)
            {
                const Location location { ch, step };
                auto& accessible = accessibleCells[static_cast<size_t> (location.key())];
                const auto& value = controller.cell (location);
                const auto index = static_cast<size_t> (location.key());
                const auto paintBounds = cellPaintBounds (location);
                if (displayedCells[index] != value || displayedSelection[index] != controller.selected[index] || swingChanged || displayedPaintBounds[index] != paintBounds)
                {
                    repaint (layout.cell (location).getUnion (displayedPaintBounds[index]).getUnion (paintBounds).getSmallestIntegerContainer());
                    accessible.setBounds (cellBounds (location).toNearestInt());
                }
                displayedPaintBounds[index] = paintBounds;
                const int ccMode = displayedCcModes[static_cast<size_t> (ch)];
                if (displayedCells[index] != value || fullPaint)
                    accessible.setDescription (accessibleDescription (value, ccMode != 0));
                if (mode != 0 && displayedCells[index] != value)
                    curveChanged = true;
                displayedCells[index] = value;
                accessible.setVisible (layout.channelVisible (ch) && !coveringSpan (location).valid()
                    && step < layout.firstStep + layout.visibleSteps() && step + displayedSpan (location) > layout.firstStep);
                accessible.setEnabled (controller.source() >= 0 && step >= range.loopStart && step < range.loopStart + range.loopLength);
                accessible.setToggleState (value.enabled, juce::dontSendNotification);
            }
            if (curveChanged && (mode != 0 || curve.mode != 0))
            {
                curve = { mode != 0 ? makeControllerCurve (ch) : juce::Path {}, range, mode };
                repaint (layout.cell ({ ch, layout.firstStep }).withWidth (layout.gridWidth()).expanded (2.0f).getSmallestIntegerContainer());
            }
        }
        updateCursor();
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
        for (int ch = 0; ch < Constants::maxChannels; ++ch)
            for (int step = 0; step < Constants::maxSteps; ++step)
            {
                const Location location { ch, step };
                accessibleCells[static_cast<size_t> (location.key())].setBounds (cellBounds (location).toNearestInt());
            }
        for (int ch = 0; ch < 8; ++ch)
        {
            auto& r = rows[static_cast<size_t> (ch)];
            const int gridRight = juce::roundToInt (layout.gridLeft() + layout.gridWidth());
            const auto cell = layout.cell ({ ch, 0 });
            if (layout.compact)
            {
                const int y = juce::roundToInt (cell.getY());
                const int height = juce::roundToInt (layout.rowHeight());
                if (layout.phoneRowControls)
                {
                    const int side = juce::jmin (32, height - 2);
                    const int buttonY = y + (height - side) / 2;
                    r.label.setBounds (0, y, 32, height);
                    r.more.setBounds (34, buttonY, side, side);
                    r.mute.setBounds (68, buttonY, side, side);
                }
                else
                {
                    r.label.setBounds (0, y, 30, height);
                    r.more.setBounds (32, y, 16, height);
                    r.reverse.setBounds (50, y, 16, height);
                    r.ping.setBounds (68, y, 16, height);
                    r.mute.setBounds (86, y, 16, height);
                }
                r.clear.setBounds (gridRight + 4, y, 28, height);
                continue;
            }
            const int height = juce::roundToInt (layout.rowHeight());
            const int y = juce::roundToInt (cell.getCentreY()) - height / 2;
            r.label.setBounds (0, y, 48, height);
            constexpr int controlsLeft = 50;
            constexpr int gap = 2;
            const int controlsRight = juce::roundToInt (layout.gridLeft()) - Layout::loopEdgeSpace - 10;
            juce::Rectangle<int> controls (controlsLeft, y, controlsRight - controlsLeft, height);
            const std::array<juce::Component*, 4> buttons { &r.note, &r.reverse, &r.ping, &r.mute };
            for (size_t i = 0; i < buttons.size(); ++i)
            {
                const int remaining = static_cast<int> (buttons.size() - i);
                buttons[i]->setBounds (controls.removeFromLeft ((controls.getWidth() - (remaining - 1) * gap) / remaining));
                controls.removeFromLeft (gap);
            }
            r.note.valueFontSize = 16;
            r.clear.setBounds (gridRight + Layout::loopEdgeSpace, y, Layout::rowClearWidth, height);
        }
    }
    void PatternGrid::prepareProbabilityLabels (float size)
    {
        if (std::abs (probabilityFontSize - size) < 0.001f)
            return;
        probabilityFontSize = size;
        const auto labelFont = font (size, true);
        const float digitWidth = juce::GlyphArrangement::getStringWidth (labelFont, "0");
        const float percentWidth = juce::GlyphArrangement::getStringWidth (labelFont, "%");
        probabilityBaseline = (labelFont.getAscent() - labelFont.getDescent()) * 0.5f;
        // Gantari has proportional numerals and no tabular-number feature.
        // Cache equal-width digit slots so matching digit counts keep % aligned.
        const auto prepare = [&] (ProbabilityLabel& label, const juce::String& number)
        {
            label.width = static_cast<float> (number.length()) * digitWidth + percentWidth;
            label.glyphs.clear();
            label.glyphs.addLineOfText (labelFont, number + "%", 0.0f, 0.0f);
            for (int i = 0; i < label.glyphs.getNumGlyphs(); ++i)
            {
                auto& glyph = label.glyphs.getGlyph (i);
                const float slot = static_cast<float> (i) * digitWidth;
                const float inset = i < number.length() ? (digitWidth - glyph.getBounds().getWidth()) * 0.5f : 0.0f;
                glyph.moveBy (slot + inset - glyph.getLeft(), 0.0f);
            }
        };
        for (int value = 0; value <= 100; ++value)
            prepare (probabilityLabels[static_cast<size_t> (value)], juce::String (value));
        for (int value = -100; value <= 100; ++value)
            prepare (nudgeLabels[static_cast<size_t> (value + 100)], (value > 0 ? "+" : "") + juce::String (value));
    }
    void PatternGrid::paint (juce::Graphics& g)
    {
        static constexpr Cell newStep { .enabled = true };
        const auto gridBackdrop = gridBackdropColour();
        const auto loopTint = Palette::accent.withAlpha (0.05f);
        g.fillAll (gridBackdrop);
        const auto layer = visibleGridLayer (displayLayer());
        const float headingY = layout.cellsY() - 18.0f;
        if (layout.compact && overviewLayer (layer))
        {
            const auto name = layer == Layer::nudge ? "Nudge" : "Split";
            const float y = layout.phoneRowControls ? headingY : layout.cellsY() - 32.0f;
            text (g, juce::String (name) + " / overview", { 16.0f, y, layout.gridLeft() - 24.0f, layout.phoneRowControls ? 16.0f : 22.0f }, 12, Palette::accent, true, juce::Justification::centredLeft);
        }
        const auto referenceCell = layout.cellArtwork (layout.cell ({ 0, layout.firstStep }));
        const float valueSize = juce::jmin (18.0f, referenceCell.getWidth() * 0.38f, referenceCell.getHeight() * 0.5f);
        const bool hasSelection = controller.selected.any();
        const bool movingSelection = drag == Drag::move && target.valid();
        constexpr float moveSourceOpacity = 0.12f;
        const auto selectedSplit = layer == Layer::chance ? controller.selectedSplitCycle() : std::nullopt;
        const bool graphicalValues = controller.graphicalCellValues;
        const auto chanceTrackTint = Palette::shadeBackground (juce::Colour (0xff101212)).withAlpha (0.92f);
        if (!graphicalValues && (layer == Layer::chance || layer == Layer::nudge))
            prepareProbabilityLabels (valueSize * 0.78f);
        const auto paintCycle = [&] (juce::Rectangle<float> strip, int cycle, int mask, juce::Colour foreground, bool split)
        {
            if (split && strip.getWidth() < 2.0f * static_cast<float> (cycle))
            {
                const float active = static_cast<float> (std::popcount (static_cast<unsigned int> (mask))) / static_cast<float> (cycle);
                g.setColour (chanceTrackTint.interpolatedWith (foreground, active));
                g.fillRect (strip);
            }
            else
                paintCycleStrip (g, strip, cycle, mask, foreground, chanceTrackTint);
        };
        const auto paintChance = [&] (juce::Rectangle<float> r, int probability, int cycle, int mask, juce::Colour foreground, float pulse, bool split)
        {
            probability = juce::jlimit (0, 100, probability);
            auto strip = cycleStripBounds (r);
            if (split && r.getWidth() < 24.0f)
                strip = strip.withX (r.getX() + r.getWidth() * 0.15f).withWidth (r.getWidth() * 0.70f);
            const auto& label = probabilityLabels[static_cast<size_t> (probability)];
            if (graphicalValues || (split && label.width + 4.0f > r.getWidth()))
            {
                const auto track = probabilityTrack (r, split);
                if (!track.isEmpty())
                {
                    const float level = static_cast<float> (probability) / 100.0f;
                    const float fillY = track.getBottom() - track.getHeight() * level;
                    g.setColour (chanceTrackTint);
                    g.fillRoundedRectangle (track, 2);
                    const juce::Graphics::ScopedSaveState faderClip (g);
                    juce::Path clip;
                    clip.addRoundedRectangle (track, 2);
                    g.reduceClipRegion (clip);
                    g.setColour (foreground);
                    if (level > 0.0f)
                        g.fillRect (track.withTop (fillY));
                    g.setColour (Palette::text.withAlpha (0.3f + 0.7f * pulse));
                    const float handleY = juce::jlimit (track.getY(), juce::jmax (track.getY(), track.getBottom() - 1.0f), fillY - 0.5f);
                    g.fillRect (track.getX(), handleY, track.getWidth(), 1.0f);
                    paintInsetEdge (g, track);
                }
            }
            else
            {
                g.setColour (Palette::app);
                label.glyphs.draw (g, juce::AffineTransform::translation (r.getCentreX() - label.width * 0.5f,
                                         r.getCentreY() - r.getHeight() * chanceCycleHeightIncrease * 0.5f - 3.0f + probabilityBaseline));
            }
            paintCycle (strip, cycle, mask, foreground, split);
        };
        const auto paintHit = [&] (const Cell& cell, juce::Rectangle<float> r, juce::Colour colour, int span, float pulse, float slotX, bool cc)
        {
            const auto& c = cell;
            const int baseValue = cellValue (c, Layer::velocity, cc);
            const bool ccValues = cc && layer == Layer::velocity;
            if (ccValues)
                colour = colour.interpolatedWith (Palette::app, 0.60f);
            const auto valueColour = ccValues ? Palette::text : Palette::app;
            const auto light = colour.brighter (0.1f);
            paintGridHitFill (g, r, colour, layer == Layer::velocity && (!cc || graphicalValues) ? baseValue : 127);
            const bool spanned = span > 1;
            if (c.roll > 1 && (spanned || !overviewLayer (layer) || layer == Layer::roll))
            {
                juce::Graphics::ScopedSaveState rollClip (g);
                juce::Path clip;
                clip.addRoundedRectangle (r, 5);
                g.reduceClipRegion (clip);
                const bool useSlot = spanned && layer != Layer::chance && layer != Layer::velocity;
                const float rollStart = useSlot ? slotX : r.getX();
                const float rollWidth = useSlot ? layout.cellWidth() * static_cast<float> (span) : r.getWidth();
                const auto rollBoundary = [=] (int part)
                {
                    return rollStart + rollWidth * static_cast<float> (part) / static_cast<float> (c.roll);
                };
                for (int part = 0; part < c.roll; ++part)
                {
                    const float x = part == 0 ? r.getX() : rollBoundary (part);
                    const float end = part + 1 == c.roll ? r.getRight() : rollBoundary (part + 1);
                    const int value = splitValue (c, part, cc);
                    if (layer == Layer::velocity && (!cc || graphicalValues) && value != baseValue)
                        paintGridHitFill (g, r.withX (x).withWidth (end - x), colour, value, 0.0f);
                    if (part % 2 == 1)
                    {
                        g.setColour (Palette::app.withAlpha (0.14f));
                        if (end > x)
                            g.fillRect (x, r.getY(), end - x, r.getHeight());
                    }
                    if (part > 0)
                    {
                        g.setColour (Palette::text.withAlpha (spanned ? 0.35f : 0.18f));
                        g.fillRect (x - 0.375f, r.getY(), 0.75f, r.getHeight());
                    }
                }
            }
            // Each emitted hit retriggers a fast, colour-preserving pulse over the entire body.
            if (pulse > 0.0f)
            {
                const auto flash = colour.brighter (0.5f);
                const auto backgroundFlash = flash.withAlpha (0.9775f * pulse * pulse);
                g.setColour (backgroundFlash);
                if (layer == Layer::velocity && graphicalValues)
                {
                    // Let the unlit background lead the flash; the velocity fill follows more gently.
                    const auto foregroundFlash = flash.withAlpha (0.4f * pulse * pulse);
                    const double shade = 1.0 - baseValue / 127.0;
                    const double edge = juce::jmin (1.0, shade + 0.5 / juce::jmax (1.0, static_cast<double> (r.getHeight())));
                    juce::ColourGradient velocityFlash (foregroundFlash, r.getTopLeft(), foregroundFlash, r.getBottomLeft(), false);
                    if (shade > 0.0)
                    {
                        velocityFlash.clearColours();
                        velocityFlash.addColour (0.0, backgroundFlash);
                        velocityFlash.addColour (shade, backgroundFlash);
                        velocityFlash.addColour (edge, foregroundFlash);
                        velocityFlash.addColour (1.0, foregroundFlash);
                    }
                    g.setGradientFill (velocityFlash);
                }
                g.fillRoundedRectangle (r, 5);
            }
            paintGridHitEdge (g, r, colour);
            if (layer == Layer::chance)
            {
                // Keep the hit visible above the inset graphics without flashing the labels.
                const auto foregroundPulse = 0.9f * pulse * pulse;
                const auto foreground = light.interpolatedWith (Palette::text, foregroundPulse);
                if (c.roll == 1)
                    paintChance (r, c.probability, c.cycle, c.cycleMask, foreground, foregroundPulse, false);
                else
                    for (int part = 0; part < c.roll; ++part)
                    {
                        const auto& gate = c.splitGates[static_cast<size_t> (part)];
                        paintChance (splitSegment (r, c.roll, part), gate.probability(), gate.cycle(), gate.cycleMask(), foreground, foregroundPulse, true);
                    }
            }
            else if (layer == Layer::roll)
            {
                if (!graphicalValues)
                    text (g, juce::String (c.roll), r, valueSize, valueColour);
            }
            else if (layer == Layer::nudge)
            {
                if (!graphicalValues)
                {
                    const auto& label = nudgeLabels[static_cast<size_t> (juce::jlimit (-100, 100, c.nudge) + 100)];
                    g.setColour (valueColour);
                    label.glyphs.draw (g, juce::AffineTransform::translation (r.getCentreX() - label.width * 0.5f,
                                             r.getCentreY() - 3.0f + probabilityBaseline));
                }
                const auto bar = r.withY (r.getY() + r.getHeight() * 0.8f - 3.0f).withHeight (r.getHeight() * 0.14f + 3.0f).reduced (4, 0);
                g.setColour (valueColour.withMultipliedAlpha (0.18f));
                g.fillRoundedRectangle (bar, 1);
                const float end = bar.getCentreX() + bar.getWidth() * 0.5f * static_cast<float> (c.nudge) / 100.0f;
                g.setColour (valueColour.withMultipliedAlpha (0.8f));
                g.fillRect (juce::jmin (bar.getCentreX(), end), bar.getY(), juce::jmax (1.0f, std::abs (end - bar.getCentreX())), bar.getHeight());
                g.fillRect (bar.getCentreX() - 0.5f, bar.getY(), 1.0f, bar.getHeight());
            }
            else if (!graphicalValues)
            {
                for (int part = 0; part < c.roll; ++part)
                {
                    const auto segment = splitSegment (r, c.roll, part);
                    const float size = valueSize * 0.9f;
                    if (c.roll == 1 || segment.getWidth() >= 2.0f * size)
                        text (g, juce::String (c.roll == 1 ? baseValue : splitValue (c, part, cc)), segment.translated (0, 2), size, valueColour, true);
                }
            }
        };
        const auto paintSelection = [&] (int ch, LoopRange range, bool muted)
        {
            selectionGlow.setColor (Palette::channel (ch, controller.drumColors).brighter (0.3f).withAlpha (muted ? 0.20f : 0.92f), 0);
            for (int step = range.loopStart; step < juce::jmin (range.loopStart + range.loopLength, layout.firstStep + layout.visibleSteps()); ++step)
            {
                const Location pos { ch, step };
                if ((step < layout.firstStep && displayedSpan (pos) == 1)
                    || !controller.selected.test (static_cast<size_t> (pos.key()))
                    || !g.clipRegionIntersects (cellPaintBounds (pos).getSmallestIntegerContainer()))
                    continue;
                auto frame = layout.cellArtwork (cellBounds (pos));
                if (selectedSplit && selectedSplit->location == pos)
                    frame = splitSegment (frame, controller.cell (pos).roll, selectedSplit->index);
                frame = frame.expanded (0.65f);
                const float corner = juce::jmin (5.65f, frame.getWidth() * 0.5f);
                const juce::Graphics::ScopedSaveState selectionClip (g);
                g.reduceClipRegion (juce::Rectangle<float> (layout.gridLeft(), frame.getY() - selectionGlowExtent, layout.gridWidth(), frame.getHeight() + selectionGlowExtent * 2).getSmallestIntegerContainer());
                juce::Path outline;
                outline.addRoundedRectangle (frame, corner);
                {
                    // Keep the cell interior clear while drawing the outer halo.
                    const juce::Graphics::ScopedSaveState glowClip (g);
                    juce::Path exterior;
                    exterior.setUsingNonZeroWinding (false);
                    exterior.addRectangle (frame.expanded (selectionGlowExtent));
                    exterior.addRoundedRectangle (frame.reduced (0.6f), corner - 0.6f);
                    g.reduceClipRegion (exterior);
                    selectionGlow.render (g, outline);
                }
                g.setColour (Palette::text.withAlpha (muted ? 0.40f : 0.9f));
                g.drawRoundedRectangle (frame, corner, 1.0f);
            }
        };
        const auto now = juce::Time::getMillisecondCounterHiRes();
        for (int step = layout.firstStep; step < layout.firstStep + layout.visibleSteps(); ++step)
        {
            auto r = layout.cell ({ 0, step });
            r.setY (headingY);
            r.setHeight (16.0f);
            text (g, juce::String (step + 1), r, layout.compact ? 10.0f : 12.0f, step + 1 == controller.playingStep ? Palette::accent : Palette::muted);
        }
        for (int ch = 0; ch < layout.channels; ++ch)
        {
            const auto range = controller.loop (ch);
            if (!layout.channelVisible (ch))
                continue;
            const int source = controller.source();
            const auto& channel = controller.state.playback.channels[static_cast<size_t> (ch)];
            const bool muted = source >= 0 && controller.state.playback.sourceChannelMutes[static_cast<size_t> (source)][static_cast<size_t> (ch)];
            bool ghostRow = false;
            if (movingSelection)
                for (int step = range.loopStart; step < juce::jmin (range.loopStart + range.loopLength, layout.firstStep + layout.visibleSteps()) && !ghostRow; ++step)
                    ghostRow = controller.selected.test (static_cast<size_t> (Location { ch, step }.key()));
            auto first = layout.cell ({ ch, range.loopStart });
            auto brace = first.withWidth (static_cast<float> (range.loopLength) * layout.cellWidth()).expanded (0, 1);
            std::array<juce::Rectangle<float>, 2> grips;
            if (layout.compact)
                brace = brace.getIntersection ({ layout.gridLeft(), brace.getY(), layout.gridWidth(), brace.getHeight() });
            else
            {
                grips = { loopHandleBounds (ch, false), loopHandleBounds (ch, true) };
                brace.setLeft (grips.front().getX());
                brace.setRight (grips.back().getRight());
                brace = brace.withSizeKeepingCentre (brace.getWidth(), layout.rowHeight());
            }
            g.setColour (loopTint);
            g.fillRoundedRectangle (brace, 6);
            // Lay down all beat slots before drawing shifted hits over them.
            for (int pass = 0; pass < (ghostRow ? 3 : 2); ++pass)
            {
                const juce::Graphics::ScopedSaveState rowClip (g);
                const bool drawHits = pass != 0;
                const bool ghostPass = ghostRow && pass == 1;
                const bool fade = ghostPass || (drawHits && muted);
                if (fade)
                {
                    const auto hitArea = brace.withX (layout.gridLeft()).withWidth (layout.gridWidth())
                                             .expanded (0.0f, ghostPass ? selectionGlowExtent + 3.0f : 3.0f);
                    if (!g.reduceClipRegion (hitArea.getSmallestIntegerContainer()))
                        continue;
                    // Fade the artwork and selection halo together at the source.
                    g.beginTransparencyLayer ((ghostPass ? moveSourceOpacity : 1.0f) * (muted ? 0.16f : 1.0f));
                }
                for (int step = drawHits ? range.loopStart : layout.firstStep; step < layout.firstStep + layout.visibleSteps(); ++step)
                {
                    if (step < range.loopStart || step >= range.loopStart + range.loopLength)
                        continue;
                    const Location pos { ch, step };
                    const auto& cell = controller.cell (pos);
                    const bool selected = controller.selected.test (static_cast<size_t> (pos.key()));
                    const bool preview = pos == touchStepPreview && !cell.enabled;
                    if (drawHits && ((!cell.enabled && !preview) || (step < layout.firstStep && displayedSpan (pos) == 1)
                                     || (ghostRow && selected != ghostPass)))
                        continue;
                    const auto slot = !drawHits ? layout.cell (pos) : preview ? emptyStepBounds (pos) : cellBounds (pos);
                    const auto cellArea = slot.expanded (3).getSmallestIntegerContainer();
                    if (!g.clipRegionIntersects (cellArea))
                        continue;
                    const auto& c = preview ? newStep : cell;
                    const auto r = layout.cellArtwork (slot);
                    const juce::Graphics::ScopedSaveState cellClip (g);
                    g.reduceClipRegion (juce::Rectangle<float> (layout.gridLeft(), r.getY() - 2, layout.gridWidth(), r.getHeight() + 4).getSmallestIntegerContainer());
                    auto colour = Palette::channel (ch, controller.drumColors);
                    if (c.enabled && hasSelection && !selected)
                        colour = colour.withMultipliedSaturation (0.65f).interpolatedWith (Palette::app, 0.6f);
                    const auto remaining = controller.sourceFlashes[static_cast<size_t> (ch)][static_cast<size_t> (step)] - now;
                    const auto pulse = muted ? 0.0f : static_cast<float> (juce::jlimit (0.0, 1.0, remaining / EditorController::sourceFlashDurationMs));
                    if (drawHits)
                        paintHit (c, r, colour, displayedSpan (pos), pulse, slot.getX(), channel.controller);
                    else
                        paintGridSlot (g, r, gridBackdrop, step % displayedBeatStride == 0);
                }
                if (ghostPass)
                    paintSelection (ch, range, muted);
                if (fade)
                    g.endTransparencyLayer();
            }
            if (channel.controller && layer == Layer::velocity && source >= 0)
            {
                const auto& curve = controllerCurves[static_cast<size_t> (ch)].path;
                const juce::Graphics::ScopedSaveState ccClip (g);
                const auto loopBounds = first.withWidth (static_cast<float> (range.loopLength) * layout.cellWidth());
                const auto clip = loopBounds.getIntersection (loopBounds.withX (layout.gridLeft()).withWidth (layout.gridWidth()));
                if (!curve.isEmpty() && g.reduceClipRegion (clip.getSmallestIntegerContainer()))
                {
                    const auto body = layout.cellArtwork (first).reduced (0.0f, 3.0f);
                    const bool reverse = channel.playbackMode == PlaybackMode::reverse || channel.playbackMode == PlaybackMode::reverse_ping_pong;
                    const auto pixelsPerBeat = layout.cellWidth() / static_cast<float> (controller.state.playback.beatsPerStep);
                    const auto transform = juce::AffineTransform::scale (reverse ? -pixelsPerBeat : pixelsPerBeat, -body.getHeight() / 127.0f)
                        .translated (reverse ? loopBounds.getRight() : loopBounds.getX(), body.getBottom());
                    g.setColour (Palette::channel (ch, controller.drumColors).withAlpha (muted ? 0.25f : 1.0f));
                    g.strokePath (curve, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved), transform);
                }
            }
            if (!ghostRow && hasSelection)
                paintSelection (ch, range, muted);
            // Keep the loop boundary and handles above the cells.
            const bool drawingVelocity = drag == Drag::velocityDraw && anchor.channel == ch;
            g.setColour (drawingVelocity ? Palette::channel (ch, controller.drumColors).withAlpha (0.9f)
                                        : Palette::accent.withAlpha (0.42f));
            const auto outline = drawingVelocity ? brace.withTop (first.getY() + 0.75f).withBottom (first.getBottom() - 0.75f) : brace;
            g.drawRoundedRectangle (outline, 6, drawingVelocity ? 1.5f : 1.0f);
            if (!layout.compact)
            {
                for (const bool end : { false, true })
                {
                    const auto bounds = grips[static_cast<size_t> (end)];
                    const auto handleDrag = end ? Drag::loopEnd : Drag::loopStart;
                    const bool highlighted = (drag == handleDrag && anchor.channel == ch)
                        || (drag == Drag::none && pointerInGrid && layout.channelAtY (hoverPosition.y) == ch
                            && loopHandleAt (hoverPosition) == end);
                    paintLengthGrip (g, bounds.withSizeKeepingCentre (bounds.getWidth(), brace.getHeight() - 6),
                                     highlighted ? Palette::accent : Palette::text);
                }
            }
        }
        const auto paintBrightRing = [&g] (juce::Rectangle<float> bounds, float width, float opacity)
        {
            const auto white = juce::Colours::white;
            const auto tint = Palette::accent.interpolatedWith (white, 0.45f);
            g.setGradientFill ({ white.withAlpha (opacity), bounds.getTopLeft(), tint.withAlpha (opacity), bounds.getBottomRight(), false });
            g.drawEllipse (bounds, width);
        };
        const auto paintMarker = [&g, &paintBrightRing] (juce::Point<float> point)
        {
            const auto bounds = juce::Rectangle<float> (10.0f, 10.0f).withCentre (point);
            g.setColour (Palette::app);
            g.fillEllipse (bounds.expanded (1.5f));
            paintBrightRing (bounds, 2.25f, 1.0f);
        };
        if (drag == Drag::none && hoverEdge && velocityHintAt (hoverPosition) == hoverEdge)
        {
            constexpr double hoverDelayMs = 65.0, ripplePeriodMs = 550.0;
            const double elapsed = juce::Time::getMillisecondCounterHiRes() - hoverMarkerStarted - hoverDelayMs;
            if (elapsed >= 0.0)
            {
                const auto point = hoverMarkerPoint();
                const float phase = static_cast<float> (std::fmod (elapsed, ripplePeriodMs) / ripplePeriodMs);
                const float radius = 8.5f + phase * 5.0f;
                const auto ripple = juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (point);
                g.setColour (Palette::accent.withAlpha (0.36f * (1.0f - phase)));
                g.drawEllipse (ripple, 4.0f);
                paintBrightRing (ripple, 1.75f, 0.94f * (1.0f - phase));
                paintMarker (point);
            }
        }
        if (drag == Drag::velocityLine && anchor.valid())
        {
            if (velocityAnchorPulseTimer.isTimerRunning())
            {
                const auto elapsed = juce::Time::getMillisecondCounterHiRes() - velocityAnchorPulseStarted;
                const float phase = juce::jmin (1.0f, static_cast<float> (elapsed / velocityAnchorPulseMs));
                const float remaining = 1.0f - phase;
                const float radius = juce::jmap (1.0f - remaining * remaining, 19.0f, velocityAnchorPulseRadius);
                const auto ring = juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (origin.withX (velocityEdgeX (lineStart)));
                for (const auto& [width, alpha] : { std::pair { 13.0f, 0.15f }, { 6.0f, 0.30f } })
                {
                    g.setColour (Palette::accent.withAlpha (alpha * remaining));
                    g.drawEllipse (ring, width);
                }
                paintBrightRing (ring, 2.5f, remaining);
            }
            const auto row = layout.cell (anchor);
            const int minimum = controller.isControllerRow (anchor.channel) ? 0 : 1;
            const auto start = juce::Point<float> (velocityEdgeX (lineStart),
                row.getBottom() - static_cast<float> (lineVelocity - minimum) / static_cast<float> (127 - minimum) * row.getHeight());
            const auto end = juce::Point<float> (velocityEdgeX (velocityLineEnd()),
                juce::jlimit (row.getY(), row.getBottom(), current.y));
            paintMarker (start);
            if (std::abs (end.x - start.x) >= 1.0f)
                paintMarker (end);
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
        {
            const juce::Graphics::ScopedSaveState previewClip (g);
            g.reduceClipRegion (juce::Rectangle<float> (layout.gridLeft(), 0.0f, layout.gridWidth(), static_cast<float> (getHeight())).getSmallestIntegerContainer());
            for (auto p : controller.selection())
            {
                const Location destination { wrap (target.channel + p.channel - anchor.channel, layout.channels),
                                             wrap (target.step + p.step - anchor.step, layout.steps) };
                if (!layout.channelVisible (destination.channel) || !layout.stepVisible (destination.step))
                    continue;

                const auto& cell = controller.cell (p);
                auto slot = cell.enabled ? layout.cell (destination) : emptyStepBounds (destination);
                const bool cc = controller.isControllerRow (destination.channel);
                const auto span = displayedSpan (p);
                if (cell.enabled)
                {
                    const auto offset = static_cast<float> (cell.nudge / 100.0 + (span == 1 ? displayedSwingDelay (destination) : 0.0));
                    slot.translate (offset * layout.cellWidth(), 0.0f);
                    slot.setWidth (slot.getWidth() * static_cast<float> (span));
                }
                const auto body = layout.cellArtwork (slot);
                if (cell.enabled)
                    paintHit (cell, body, Palette::channel (destination.channel, controller.drumColors), span, 0.0f, slot.getX(), cc);
                else
                    paintGridSlot (g, body, gridBackdrop, destination.step % displayedBeatStride == 0);

                g.setColour (Palette::text.withAlpha (cell.enabled ? 0.9f : 0.38f));
                g.drawRoundedRectangle (body.reduced (0.5f), 5, 1);
            }
        }
        if (presetMenu != nullptr)
            presetMenu->paintAnchor (g);
    }
    void PatternGrid::paintOverChildren (juce::Graphics& g)
    {
        if (drag != Drag::velocityDraw)
            return;

        const auto row = layout.cell (anchor);
        const auto area = getLocalBounds().toFloat().withTop (layout.cellsY());
        g.setColour (Palette::shell.withAlpha (0.78f));
        g.fillRect (area.withBottom (row.getY()));
        g.fillRect (area.withTop (row.getBottom()));
    }

    void PatternGrid::visibilityChanged()
    {
        if (!isVisible())
        {
            clearHoverMarker();
            updateVelocityAnchorPulse (true);
            cancelTouchStepPress();
            cyclePress.reset();
            velocityTapTimer.stopTimer();
            velocityTap.reset();
            finishVelocityDrawing();
        }
    }

    void PatternGrid::focusLost (FocusChangeType)
    {
        cancelTouchStepPress();
        updateVelocityAnchorPulse (true);
        finishVelocityTap();
        finishVelocityDrawing();
    }

    bool PatternGrid::keyPressed (const juce::KeyPress& key)
    {
        if (!key.isKeyCode (juce::KeyPress::escapeKey))
            finishVelocityTap();
        return handleCellKey (keyboardCell, key);
    }

    bool PatternGrid::handleCellKey (Location location, const juce::KeyPress& key)
    {
        if (controller.source() < 0 || hasActiveGesture() || controller.editing())
            return false;
        if (key.getModifiers().isAnyModifierKeyDown())
            return false;

        const int horizontal = key.isKeyCode (juce::KeyPress::leftKey) ? -1 : key.isKeyCode (juce::KeyPress::rightKey) ? 1 : 0;
        const int vertical = key.isKeyCode (juce::KeyPress::upKey) ? -1 : key.isKeyCode (juce::KeyPress::downKey) ? 1 : 0;
        const bool toggle = key.isKeyCode (juce::KeyPress::returnKey);
        if (!toggle && horizontal == 0 && vertical == 0)
            return false;

        if (!layout.channelVisible (location.channel) || !layout.stepVisible (location.step))
            location = { layout.channelForRow (layout.firstRow), layout.firstStep };
        if (vertical != 0)
            location.channel = layout.channelForRow (wrap (layout.displayRow (location.channel) + vertical, layout.channels));
        const auto loop = controller.loop (location.channel);
        location.step = juce::jlimit (loop.loopStart, loop.loopStart + loop.loopLength - 1, location.step);
        if (const auto owner = coveringSpan (location); owner.valid())
            location = owner;
        if (horizontal != 0)
        {
            const int offset = horizontal > 0 ? displayedSpan (location) : -1;
            location.step = loop.loopStart + wrap (location.step - loop.loopStart + offset, loop.loopLength);
            if (const auto owner = coveringSpan (location); owner.valid())
                location = owner;
        }

        if ((!layout.channelVisible (location.channel) || !layout.stepVisible (location.step)) && revealKeyboardCell)
            revealKeyboardCell (location);
        keyboardCell = location;
        auto& cell = accessibleCells[static_cast<size_t> (location.key())];
        if (cell.isShowing())
            cell.grabKeyboardFocus();
        if (toggle)
            toggleCell (location);
        return true;
    }

    void PatternGrid::toggleCell (Location location)
    {
        const auto loop = controller.loop (location.channel);
        if (controller.source() < 0 || controller.editing() || location.step < loop.loopStart
            || location.step >= loop.loopStart + loop.loopLength || coveringSpan (location).valid())
            return;
        toggleCellsFromClick (location, false);
        controller.commit();
        controller.notify();
    }

    void PatternGrid::toggleCellsFromClick (Location location, bool wholeSelection)
    {
        controller.begin (wholeSelection ? "Fill or clear selection" : "Toggle cell");
        const auto cells = wholeSelection ? controller.selection() : std::vector<Location> { location };
        const bool enable = std::any_of (cells.begin(), cells.end(), [this] (Location p) { return !controller.cell (p).enabled; });
        for (auto p : cells)
        {
            if (controller.cell (p).enabled == enable || (enable && coveringSpan (p).valid()))
                continue;
            auto cell = controller.isControllerRow (p.channel) ? controller.cell (p) : Cell {};
            cell.enabled = enable;
            controller.editCell (p, cell);
        }
    }

    void PatternGrid::refreshAccent()
    {
        for (size_t ch = 0; ch < rows.size(); ++ch)
            rows[ch].label.setColour (juce::TextButton::textColourOffId, Palette::channel (static_cast<int> (ch), controller.drumColors));
        if (presetMenu != nullptr)
            presetMenu->refresh();
        repaint();
    }
    void PatternGrid::updatePlayback()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        for (int ch = 0; ch < layout.channels; ++ch)
            for (int s = 0; s < layout.steps; ++s)
            {
                const bool flash = controller.sourceFlashes[static_cast<size_t> (ch)][static_cast<size_t> (s)] > now;
                auto& last = lastFlash[static_cast<size_t> (ch)][static_cast<size_t> (s)];
                if (flash || last)
                    repaint (cellPaintBounds ({ ch, s }).getSmallestIntegerContainer());
                last = flash;
            }
        if (previousStep != controller.playingStep)
        {
            previousStep = controller.playingStep;
            repaint (0, 0, getWidth(), juce::roundToInt (layout.cellsY()));
        }
    }
    void PatternGrid::mouseDown (const juce::MouseEvent& e)
    {
        if (gesturePointer && *gesturePointer != e.source)
            return;
        gesturePointer = e.source;

        const bool popup = e.mods.isPopupMenu();
        const bool touch = !e.source.isMouse();
        const bool tabletTouch = touch && layout.touchRows && !layout.compactOnPhone;
        if (drag == Drag::velocityDraw)
        {
            mouseMove (e);
            const bool touchExit = touch && velocityDrawTouchTap && e.getNumberOfClicks() >= 2;
            if (!e.mods.isLeftButtonDown() || popup || touchExit || !velocityDrawingBounds().contains (e.position))
                finishVelocityDrawing();
            else
            {
                velocityDrawTouchTap = touch;
                velocityDrawPoint = e.position;
            }
            return;
        }

        stopTimer();
        updateVelocityAnchorPulse (true);
        pendingVelocityEdge.reset();
        cancelPresetHold();
        cyclePress.reset();
        if (presetMenu != nullptr)
        {
            const bool dismissOnly = presetMenu->needsOutsidePress();
            presetMenu->close();
            if (dismissOnly)
                return;
        }
        clearHoverMarker();
        modifiers = e.mods;
        if (velocityTap && velocityTap->touch == touch
            && controller.source() == displayedSource && displayedEpoch == controller.processor.getUiBridge().getDocumentEpoch()
            && e.mods.isLeftButtonDown() && !popup && displayLayer() == Layer::velocity
            && controller.selected.none() && !controller.useSelectionModifier (e.mods)
            && !loopHandleAt (e.position) && layout.channelAtY (e.position.y) == velocityTap->edge.channel
            && e.position.getDistanceFrom (velocityTap->position) <= (touch ? 18.0f : 14.0f)
            && (e.mouseDownTime - velocityTap->time).inMilliseconds() <= juce::MouseEvent::getDoubleClickTimeout())
        {
            const auto edge = velocityTap->edge;
            velocityTapTimer.stopTimer();
            velocityTap.reset();
            if (isShowing())
                grabKeyboardFocus();
            beginVelocityDrawing (edge);
            velocityDrawPoint = e.position;
            mouseMove (e);
            return;
        }
        finishVelocityTap();
        if (controller.source() < 0 || (!e.mods.isLeftButtonDown() && !popup))
        {
            lastHandleClick.reset();
            return;
        }
        if (popup)
        {
            lastHandleClick.reset();
            if (layout.compactOnPhone)
                return;
            const PresetPress press { cellAt (e.position), e.position, controller.source(), controller.state.documentEpoch, true };
            if (canSetPreset (press) && !loopHandleAt (e.position))
                showCellPresets (press, false);
            return;
        }
        if (overviewLayer (displayLayer()))
        {
            lastHandleClick.reset();
            return;
        }
        if (isShowing())
            grabKeyboardFocus();
        origin = current = e.position;
        anchor = target = cellAt (e.position);
        if (anchor.valid())
            keyboardCell = anchor;
        if (controller.chanceHeld && anchor.valid())
            controller.chanceUsed = true;
        dragLayer = displayLayer();
        initialSelection = controller.selected;
        additive = controller.useSelectionModifier (e.mods);
        selectedAtStart = anchor.valid() && initialSelection.test (static_cast<size_t> (anchor.key()));
        const bool selectMode = controller.selectLatched || controller.selectHeld;
        const int row = layout.channelAtY (e.position.y);
        if (touch)
            pointerInGrid = false;
        const auto handle = loopHandleAt (e.position);
        const PresetPress press { anchor, origin, controller.source(), controller.state.documentEpoch };
        if (touch && !handle)
        {
            auto heldPress = press;
            heldPress.allowEmpty = true;
            if (canSetPreset (heldPress))
            {
                presetPress = heldPress;
                const int delay = tabletTouch ? tabletPresetHoldMs : layout.compactOnPhone ? phonePresetHoldMs : presetHoldMs;
                presetHoldDeadline = juce::Time::getMillisecondCounterHiRes() + delay;
                presetHoldTimer.startTimer (delay);
            }
        }
        if (dragLayer == Layer::chance && !handle && canClickCycleStrip (e.mods)
            && (!additive || selectedAtStart || controller.splitCount (anchor) > 1)
            && canSetPreset (press) && cycleStripContains (anchor, origin))
        {
            lastHandleClick.reset();
            cyclePress = press;
            if (!selectedAtStart)
                return;
        }
        const int selectionDelay = tabletTouch ? tabletSelectHoldMs : selectHoldMs;
        if (dragLayer == Layer::chance && !handle && !additive && initialSelection.none() && !isCopyMode()
            && !cyclePress && anchor.valid() && beginSplitPress (press, selectionDelay))
            return;
        if (handle)
        {
            initialLoop = controller.loop (row);
            anchor = { row, initialLoop.loopStart };
            drag = *handle ? Drag::loopEnd : Drag::loopStart;
            updateCursor();
            controller.begin ("Change loop range");
            repaint();
            return;
        }
        lastHandleClick.reset();
        const bool velocityEdges = dragLayer == Layer::velocity && initialSelection.none() && !additive;
        if (velocityEdges && (pendingVelocityEdge = velocityEdgeAt (e.position, touch)))
        {
            // A single click toggles the cell after the double-click window.
            const auto loop = controller.loop (row);
            if (isVelocityGutter (*pendingVelocityEdge, origin.x))
            {
                anchor = {};
                cancelPresetHold();
            }
            else if (anchor.step < loop.loopStart || anchor.step >= loop.loopStart + loop.loopLength)
                anchor = {};
            selectedAtStart = false;
            drag = Drag::pending;
            if (pendingVelocityEdge->split > 0)
                startTimer (selectionDelay);
            else if (touch)
                startTimer (tabletTouch ? tabletVelocityHoldMs : selectHoldMs);
            if (!tabletTouch || !anchor.valid())
                return;
        }
        if (velocityEdges && !pendingVelocityEdge && !isCopyMode() && beginSplitPress (press, selectionDelay))
            return;
        if (initialSelection.any() && !additive && !selectedAtStart)
        {
            drag = Drag::none;
            controller.selected.reset();
            if (anchor.valid() && controller.cell (anchor).enabled)
                controller.selected.set (static_cast<size_t> (anchor.key()));
            controller.notify();
            repaint();
            return;
        }
        if (!anchor.valid() && !additive)
            return;

        const bool dragSelectedBlank = selectedAtStart && (selectMode || isCopyMode());
        if (!anchor.valid() || (additive && (!selectedAtStart || (!controller.cell (anchor).enabled && !dragSelectedBlank))))
        {
            drag = Drag::marquee;
            updateCursor();
            if (!anchor.valid() || controller.cell (anchor).enabled)
                updateSelection();
            return;
        }
        const auto loop = controller.loop (anchor.channel);
        if (anchor.step < loop.loopStart || anchor.step >= loop.loopStart + loop.loopLength)
            return;
        original = controller.cell (anchor);
        if (!original.enabled)
            original = Cell {};
        valueStep = anchor.step;
        valueVelocity = cellValue (original, Layer::velocity, controller.isControllerRow (anchor.channel));
        valueX = origin.x;
        originals.clear();
        if (selectedAtStart)
            for (auto p : controller.selection())
                originals.emplace_back (p, controller.cell (p));
        else
            originals.emplace_back (anchor, original);
        drag = Drag::pending;
        if (e.mods.isLeftButtonDown() && !additive && !isCopyMode() && !cyclePress && !pendingVelocityEdge)
        {
            startTimer (selectionDelay);
            if (layout.touchRows && presetPress && !original.enabled && !controller.editing())
            {
                // Show the step while the tap, swipe and hold gestures are still being resolved.
                touchStepPreview = anchor;
                repaint (emptyStepBounds (anchor).expanded (3.0f).getSmallestIntegerContainer());
            }
        }
    }
    void PatternGrid::timerCallback()
    {
        stopTimer();
        clearTouchStepPreview();
        if (controller.source() < 0)
            return;
        if (splitPress)
        {
            if (splitPress->scrubbing || !canSetPreset (splitPress->target))
                return;
            splitPress.reset();
        }
        else if (drag != Drag::pending)
            return;
        if (const auto edge = std::exchange (pendingVelocityEdge, std::nullopt))
        {
            if (edge->split == 0 && velocityEdgeAt (origin, true) == edge)
            {
                cancelPresetHold();
                beginVelocityLine (*edge);
                if (layout.touchRows)
                {
                    velocityAnchorPulseStarted = juce::Time::getMillisecondCounterHiRes();
                    velocityAnchorPulseTimer.startTimerHz (60);
                    updateVelocityAnchorPulse();
                }
                return;
            }
        }
        if (!anchor.valid())
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
        if (origin.getDistanceFrom (current) < 5 && anchor.valid())
        {
            if (controller.cell (anchor).enabled)
                controller.selected.flip (static_cast<size_t> (anchor.key()));
            else
                controller.selected.reset();
        }
        else
        {
            // A straight drag still crosses steps even when one dimension is zero.
            const auto area = juce::Rectangle<float> (origin, current);
            const auto hitArea = area.withWidth (juce::jmax (0.01f, area.getWidth()))
                                     .withHeight (juce::jmax (0.01f, area.getHeight()));
            for (int ch = 0; ch < layout.channels; ++ch)
                for (int s = 0; s < layout.steps; ++s)
                {
                    const Location location { ch, s };
                    // Empty slots keep a fixed marquee target while their selection outline moves.
                    const auto bounds = controller.cell (location).enabled ? cellBounds (location) : layout.cell (location);
                    if (hitArea.intersects (bounds) && !coveringSpan (location).valid())
                        controller.selected.set (static_cast<size_t> (location.key()));
                }
        }
        controller.notify();
        repaint();
    }
    int PatternGrid::velocityAtY (float y) const
    {
        const auto r = layout.cell (anchor);
        const int minimum = controller.isControllerRow (anchor.channel) ? 0 : 1;
        return juce::jlimit (minimum, 127, jsRound (static_cast<float> (minimum)
            + (1 - (y - r.getY()) / r.getHeight()) * static_cast<float> (127 - minimum)));
    }
    void PatternGrid::beginVelocityLine (VelocityEdge edge)
    {
        const auto loop = controller.loop (edge.channel);
        anchor = { edge.channel, juce::jmin (edge.step, loop.loopStart + loop.loopLength - 1) };
        drag = Drag::velocityLine;
        lineStart = edge;
        lineVelocity = velocityAtY (origin.y);
        originals.clear();
        for (int step = 0; step < layout.steps; ++step)
        {
            const Location location { anchor.channel, step };
            originals.emplace_back (location, controller.cell (location));
        }
        controller.begin ("Draw velocity line");
        repaint();
    }
    void PatternGrid::updateVelocityAnchorPulse (bool finished)
    {
        if (!velocityAnchorPulseTimer.isTimerRunning())
            return;
        if (finished || juce::Time::getMillisecondCounterHiRes() - velocityAnchorPulseStarted >= velocityAnchorPulseMs)
            velocityAnchorPulseTimer.stopTimer();

        constexpr float diameter = 2.0f * (velocityAnchorPulseRadius + 8.0f);
        const auto centre = origin.withX (velocityEdgeX (lineStart));
        repaint (juce::Rectangle<float> (diameter, diameter).withCentre (centre).getSmallestIntegerContainer());
    }
    void PatternGrid::finishVelocityTap()
    {
        velocityTapTimer.stopTimer();
        const auto tap = std::exchange (velocityTap, std::nullopt);
        if (!tap || !tap->cell.valid() || controller.editing() || controller.source() != displayedSource
            || displayedEpoch != controller.processor.getUiBridge().getDocumentEpoch()
            || displayLayer() != Layer::velocity || controller.selected.any())
            return;
        toggleCell (tap->cell);
    }

    bool PatternGrid::canClickCycleStrip (const juce::ModifierKeys& mods) const
    {
        // Latched Select still lets a cycle-strip click open the existing selection.
        return !controller.selectHeld && !(controller.keyboardModifiersEnabled && mods.isShiftDown()) && !isCopyMode();
    }

    bool PatternGrid::cycleStripContains (Location location, juce::Point<float> position) const
    {
        const auto body = cellBounds (location);
        const auto artwork = layout.cellArtwork (body);
        if (controller.splitCount (location) > 1)
            return body.contains (position) && position.y >= artwork.getY() + artwork.getHeight() * (2.0f / 3.0f - chanceCycleHeightIncrease);
        const auto strip = cycleStripBounds (body.reduced (layout.cellInsetX(), Layout::cellInsetY));
        const auto paintedStrip = cycleStripBounds (artwork);
        return body.contains (position) && position.y >= juce::jmin (strip.getY(), paintedStrip.getY()) - 3.0f;
    }

    int PatternGrid::splitIndexAt (Location location, float x) const
    {
        const auto body = layout.cellArtwork (cellBounds (location));
        const int count = controller.splitCount (location);
        const float width = body.getWidth() / static_cast<float> (count);
        return width > 0.0f ? juce::jlimit (0, count - 1, static_cast<int> ((x - body.getX()) / width)) : 0;
    }

    bool PatternGrid::beginSplitPress (const PresetPress& press, int selectionDelay)
    {
        if (!canSetPreset (press))
            return false;
        const auto& cell = controller.cell (press.location);
        if (controller.splitCount (press.location) < 2)
            return false;
        const int index = splitIndexAt (press.location, press.position.x);
        const int value = dragLayer == Layer::velocity ? splitValue (cell, index, controller.isControllerRow (press.location.channel))
                            : cell.splitGates[static_cast<size_t> (index)].probability();
        splitPress = SplitPress { press, index, value };
        startTimer (selectionDelay);
        return true;
    }

    void PatternGrid::setSplitProbability (Location location, int index, int value)
    {
        auto cell = controller.cell (location);
        if (!cell.enabled || cell.roll < 2 || index >= cell.roll)
            return;
        cell.splitGates[static_cast<size_t> (index)].setProbability (value);
        controller.editCell (location, cell);
        controller.notify();
    }

    void PatternGrid::dragSplitValue (const juce::MouseEvent& event)
    {
        const auto delta = event.position - splitPress->target.position;
        if (event.getDistanceFromDragStart() >= 6)
            cancelPresetHold();
        if (!splitPress->scrubbing)
        {
            if (std::abs (delta.y) < juce::jmax (6.0f, std::abs (delta.x)) || controller.editing())
                return;
            controller.begin (dragLayer == Layer::velocity ? "Set split velocity" : "Set split probability");
            if (!splitPress)
                return;
            splitPress->scrubbing = true;
            valueStep = splitPress->target.location.step;
            valueVelocity = splitPress->value;
            valueX = splitPress->target.position.x;
        }
        if (canSetPreset (splitPress->target))
        {
            if (dragLayer == Layer::velocity)
            {
                const auto row = layout.cell (splitPress->target.location);
                const auto location = cellAt (event.position.withY (row.getCentreY()));
                const auto step = location.valid() ? location.step : event.position.x < layout.gridLeft() ? 0 : layout.steps - 1;
                const int minimum = controller.isControllerRow (splitPress->target.location.channel) ? 0 : 1;
                dragVelocityValues (event.position.x, step, juce::jlimit (minimum, 127, splitPress->value + dragOffset (-delta.y, 1.25f)));
                controller.notify();
            }
            else
            {
                const auto track = probabilityTrack (layout.cellArtwork (cellBounds (splitPress->target.location)), true);
                const float sensitivity = juce::jmax (0.30f, track.getHeight() / 100.0f);
                setSplitProbability (splitPress->target.location, splitPress->index, splitPress->value + dragOffset (-delta.y, sensitivity));
            }
        }
    }

    void PatternGrid::beginVelocityDrawing (VelocityEdge edge)
    {
        controller.begin ("Draw velocity");
        anchor = { edge.channel, layout.firstStep };
        dragLayer = Layer::velocity;
        drag = Drag::velocityDraw;
        velocityDrawTouchTap = false;
        pointerInGrid = false;
        lastHandleClick.reset();
        updateCursor();
        repaint();
    }

    void PatternGrid::finishVelocityDrawing()
    {
        if (drag != Drag::velocityDraw)
            return;
        drag = Drag::none;
        velocityDrawPoint.reset();
        updateCursor();
        controller.commit();
        repaint();
    }

    juce::Rectangle<float> PatternGrid::velocityDrawingBounds() const
    {
        return layout.cell (anchor).withWidth (layout.gridWidth());
    }

    void PatternGrid::drawVelocity (juce::Point<float> point)
    {
        const auto row = velocityDrawingBounds();
        const auto previous = velocityDrawPoint.value_or (point);
        if (!row.contains (point))
        {
            velocityDrawPoint.reset();
            return;
        }
        velocityDrawPoint = point;
        // Connect consecutive positions inside the row so a fast sweep still reaches every active hit.
        const auto start = row.contains (previous) ? previous : point;
        const float left = juce::jmin (start.x, point.x);
        const float right = juce::jmax (start.x, point.x);
        const auto loop = controller.loop (anchor.channel);
        const bool cc = controller.isControllerRow (anchor.channel);
        const int first = loop.loopStart;
        const int last = juce::jmin (layout.firstStep + layout.visibleSteps(), loop.loopStart + loop.loopLength);
        bool changed = false;
        for (int step = first; step < last; ++step)
        {
            const Location location { anchor.channel, step };
            auto cell = controller.cell (location);
            if (!cell.enabled || (step < layout.firstStep && displayedSpan (location) == 1))
                continue;
            const auto body = cellBounds (location);
            const auto originalCell = cell;
            const int roll = controller.splitCount (location);
            for (int part = 0; part < roll; ++part)
            {
                const auto bounds = splitSegment (body, roll, part).getIntersection (row);
                if (bounds.isEmpty() || bounds.getRight() <= left || bounds.getX() > right)
                    continue;
                const float x = bounds.contains (point) ? point.x : juce::jlimit (left, right, bounds.getCentreX());
                const float fraction = juce::approximatelyEqual (start.x, point.x) ? 1.0f : (x - start.x) / (point.x - start.x);
                const int velocity = velocityAtY (start.y + fraction * (point.y - start.y));
                if (roll == 1)
                    cell = withValue (cell, Layer::velocity, velocity, cc);
                else
                    setSplitValue (cell, part, velocity, cc);
            }
            if (cell != originalCell)
            {
                controller.editCell (location, cell);
                changed = true;
            }
        }
        if (changed)
            controller.notify();
        repaint (row.getSmallestIntegerContainer().expanded (6));
    }

    void PatternGrid::dragVelocityValues (float x, int step, int velocity)
    {
        const auto loop = controller.loop (anchor.channel);
        const bool cc = controller.isControllerRow (anchor.channel);
        const int loopEnd = loop.loopStart + loop.loopLength;
        step = juce::jlimit (loop.loopStart, loopEnd - 1, step);
        const int first = juce::jmin (valueStep, step);
        const int last = juce::jmax (valueStep, step);
        const float left = juce::jmin (valueX, x);
        const float right = juce::jmax (valueX, x);
        const auto interpolate = [&] (float fraction)
        {
            return jsRound (static_cast<float> (valueVelocity) + fraction * static_cast<float> (velocity - valueVelocity));
        };
        // Use the painted split geometry, since a spanned or swung hit can cross grid slots.
        for (int s = loop.loopStart; s < loopEnd; ++s)
        {
            const Location location { anchor.channel, s };
            auto next = controller.cell (location);
            if (next.enabled && next.roll > 1)
            {
                const auto body = layout.cellArtwork (cellBounds (location));
                if (body.getRight() <= left || body.getX() > right)
                    continue;
                for (int part = 0; part < next.roll; ++part)
                {
                    const auto segment = splitSegment (body, next.roll, part);
                    if (segment.getRight() <= left || segment.getX() > right)
                        continue;
                    const bool underPointer = x >= segment.getX() && x < segment.getRight();
                    const float sampleX = underPointer ? x : juce::jlimit (left, right, segment.getCentreX());
                    const int drawn = interpolate (juce::approximatelyEqual (valueX, x) ? 1.0f : (sampleX - valueX) / (x - valueX));
                    setSplitValue (next, part, drawn, cc);
                }
            }
            else
            {
                if (s < first || s > last || coveringSpan (location).valid())
                    continue;
                if (!next.enabled && !cc)
                    next = Cell { .enabled = true };
                else
                    next.enabled = true;
                const float fraction = step == valueStep ? 1.0f : static_cast<float> (s - valueStep) / static_cast<float> (step - valueStep);
                next = withValue (next, Layer::velocity, interpolate (fraction), cc);
            }
            controller.editCell (location, next);
        }
        valueStep = step;
        valueVelocity = velocity;
        valueX = x;
    }

    void PatternGrid::mouseDrag (const juce::MouseEvent& e)
    {
        if (!gesturePointer || *gesturePointer != e.source)
            return;

        if (drag == Drag::velocityDraw)
        {
            mouseMove (e);
            if (e.mods.isLeftButtonDown() && e.getDistanceFromDragStart() >= 4)
            {
                velocityDrawTouchTap = false;
                drawVelocity (e.position);
            }
            return;
        }
        current = e.position;
        modifiers = e.mods;
        if (presetMenu != nullptr && presetMenu->isTrackingPress())
        {
            presetMenu->track (presetMenu->getLocalPoint (this, e.getPosition()));
            return;
        }
        if (splitPress)
        {
            const auto delta = current - origin;
            if (delta.getDistanceFromOrigin() >= 4.0f)
                stopTimer();
            const bool painting = !splitPress->scrubbing && std::abs (delta.x) >= 4.0f && std::abs (delta.x) > std::abs (delta.y)
                                  && !controller.editing() && canSetPreset (splitPress->target);
            if (!painting)
            {
                dragSplitValue (e);
                return;
            }
            splitPress.reset();
            cancelPresetHold();
            original = controller.cell (anchor);
            drag = Drag::pending;
        }
        if (presetPress)
        {
            const auto delta = current - origin;
            const auto slot = layout.at (current);
            const bool crossedStep = touchStepPreview.valid() && std::abs (delta.x) >= 4.0f && std::abs (delta.x) > std::abs (delta.y)
                                     && slot.channel == anchor.channel && slot.step != anchor.step;
            const bool tabletVelocityScrub = layout.touchRows && !layout.compact && dragLayer == Layer::velocity && drag == Drag::pending
                                             && anchor.valid() && original.enabled && std::abs (delta.y) >= 4.0f && std::abs (delta.y) >= std::abs (delta.x);
            if (!crossedStep && !tabletVelocityScrub && delta.getDistanceFromOrigin() < touchPresetPressSlop)
                return;
            cancelPresetHold();
        }
        if (cyclePress && cyclePress->position.getDistanceFrom (current) >= (drag == Drag::pending ? 5.0f : presetPressSlop))
            cyclePress.reset();
        if (origin.getDistanceFrom (current) >= 4.0f)
            stopTimer();
        if (drag == Drag::none)
            return;
        if (drag == Drag::marquee)
        {
            if (origin.getDistanceFrom (current) >= 5.0f)
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
        const int targetRow = juce::jlimit (0, layout.channels - 1, static_cast<int> (std::floor ((current.y - layout.cellsY()) / layout.rowHeight())));
        target = { layout.channelForRow (targetRow), juce::jlimit (layout.firstStep, layout.firstStep + layout.visibleSteps() - 1, layout.firstStep + static_cast<int> (std::floor ((current.x - layout.gridLeft()) / layout.cellWidth()))) };
        if (drag == Drag::pending)
        {
            if (origin.getDistanceFrom (current) < (selectedAtStart ? 5.0f : 4.0f))
                return;
            const auto edge = std::exchange (pendingVelocityEdge, std::nullopt);
            const bool tabletTouch = !e.source.isMouse() && layout.touchRows && !layout.compact;
            if (edge && (!tabletTouch || isVelocityGutter (*edge, origin.x)))
                beginVelocityLine (*edge);
            else
            {
                if (!anchor.valid())
                {
                    drag = Drag::none;
                    return;
                }
                if (selectedAtStart && isCopyMode())
                    drag = Drag::copy;
                else if (selectedAtStart)
                {
                    const bool selectMode = controller.selectLatched || controller.selectHeld;
                    const bool vertical = std::abs (current.y - origin.y) > std::abs (current.x - origin.x);
                    const bool adjustValue = !selectMode && (controller.useSelectionModifier (e.mods) || (dragLayer == Layer::chance && vertical));
                    drag = adjustValue ? Drag::value : Drag::move;
                }
                else
                    drag = std::abs (current.x - origin.x) > std::abs (current.y - origin.y) ? Drag::paint : Drag::value;
                controller.begin ("Edit cells");
            }
        }
        if (drag == Drag::move || drag == Drag::copy)
        {
            drag = isCopyMode() ? Drag::copy : Drag::move;
            updateCursor();
            if (displayedSpan (anchor) > 1)
            {
                const int originStep = layout.firstStep + static_cast<int> (std::floor ((origin.x - layout.gridLeft()) / layout.cellWidth()));
                target.step = juce::jlimit (0, layout.steps - 1, target.step - (originStep - anchor.step));
            }
            repaint();
            return;
        }
        if (drag == Drag::paint)
        {
            const auto loop = controller.loop (anchor.channel);
            const bool enable = !original.enabled;
            for (int s = juce::jmax (loop.loopStart, juce::jmin (anchor.step, target.step)); s <= juce::jmin (loop.loopStart + loop.loopLength - 1, juce::jmax (anchor.step, target.step)); ++s)
            {
                const Location location { anchor.channel, s };
                if (controller.cell (location).enabled == enable || (enable && coveringSpan (location).valid()))
                    continue;
                auto cell = controller.isControllerRow (location.channel) ? controller.cell (location) : Cell {};
                cell.enabled = enable;
                controller.editCell (location, cell);
            }
        }
        else if (drag == Drag::value)
        {
            const float sensitivity = dragLayer == Layer::velocity ? 1.25f : 2.0f;
            const int offset = dragOffset (origin.y - current.y, sensitivity);
            const bool cc = controller.isControllerRow (anchor.channel);
            if (dragLayer == Layer::velocity && !selectedAtStart)
                dragVelocityValues (current.x, target.step, juce::jlimit (cc ? 0 : 1, 127, cellValue (original, dragLayer, cc) + offset));
            else
                for (const auto& [p, c] : originals)
                {
                    const bool ccRow = controller.isControllerRow (p.channel);
                    auto next = withValue (c, dragLayer, cellValue (c, dragLayer, ccRow) + offset, ccRow);
                    if (!selectedAtStart)
                        next.enabled = true;
                    controller.editCell (p, next);
                }
        }
        else if (drag == Drag::velocityLine)
        {
            const auto endEdge = velocityLineEnd();
            const int boundary = lineStart.step;
            const int endBoundary = endEdge.step;
            const int velocity = velocityAtY (current.y);
            const int start = endBoundary > boundary ? boundary : boundary - 1;
            const int end = endBoundary > boundary ? endBoundary - 1 : endBoundary;
            const float leftEdge = velocityEdgeX (lineStart);
            const float rightEdge = velocityEdgeX (endEdge);
            const float left = juce::jmin (leftEdge, rightEdge);
            const float right = juce::jmax (leftEdge, rightEdge);
            for (const auto& [p, c] : originals)
            {
                auto next = c;
                if (endEdge != lineStart)
                {
                    const bool cc = controller.isControllerRow (p.channel);
                    if (c.enabled && c.roll > 1 && right > left)
                    {
                        const auto body = layout.cellArtwork (cellBounds (p));
                        for (int part = 0; part < c.roll; ++part)
                        {
                            const auto segment = splitSegment (body, c.roll, part);
                            if (segment.getRight() <= left + 0.01f || segment.getX() >= right - 0.01f)
                                continue;
                            const float fraction = juce::jlimit (0.0f, 1.0f, (segment.getCentreX() - leftEdge) / (rightEdge - leftEdge));
                            const int drawn = jsRound (static_cast<float> (lineVelocity) + fraction * static_cast<float> (velocity - lineVelocity));
                            setSplitValue (next, part, drawn, cc);
                        }
                    }
                    else if (endBoundary != boundary && p.step >= juce::jmin (start, end) && p.step <= juce::jmax (start, end)
                             && !coveringSpan (p).valid())
                    {
                        const float fraction = static_cast<float> (std::abs (p.step - start)) / static_cast<float> (juce::jmax (1, std::abs (end - start)));
                        next = withValue (next, Layer::velocity,
                                          jsRound (static_cast<float> (lineVelocity) + fraction * static_cast<float> (velocity - lineVelocity)), cc);
                    }
                }
                controller.editCell (p, next);
            }
            // The endpoints can move even when no cell value changes.
            repaint();
        }
        controller.notify();
    }
    void PatternGrid::mouseUp (const juce::MouseEvent& e)
    {
        if (!gesturePointer || *gesturePointer != e.source)
            return;
        gesturePointer.reset();

        stopTimer();
        updateVelocityAnchorPulse (true);
        cancelPresetHold();
        modifiers = e.mods;
        if (drag == Drag::velocityDraw)
        {
            mouseMove (e);
            velocityDrawPoint.reset();
            return;
        }
        if (presetMenu != nullptr && presetMenu->isTrackingPress())
        {
            presetMenu->release (e.position);
            return;
        }
        if (const auto heldSplit = std::exchange (splitPress, std::nullopt))
        {
            const bool touch = !e.source.isMouse();
            if (heldSplit->scrubbing)
                controller.commit();
            else if (heldSplit->target.position.getDistanceFrom (e.position) < (touch ? touchPresetPressSlop : presetPressSlop)
                     && canSetPreset (heldSplit->target) && heldSplit->index < controller.cell (heldSplit->target.location).roll)
                toggleCell (heldSplit->target.location);
            return;
        }
        if (const auto press = std::exchange (cyclePress, std::nullopt))
        {
            drag = Drag::none;
            const bool touch = !e.source.isMouse();
            if (press->position.getDistanceFrom (e.position) < (touch ? touchPresetPressSlop : presetPressSlop) && canSetPreset (*press))
            {
                const auto step = static_cast<size_t> (press->location.key());
                if (controller.splitCount (press->location) > 1)
                    controller.selectSplitCycle (press->location, splitIndexAt (press->location, press->position.x));
                else if (!controller.selected.test (step))
                {
                    controller.selected.reset();
                    controller.selected.set (step);
                }
                controller.notify();
            }
            updateCursor();
            repaint();
            return;
        }
        controller.clearSplitCycleSelection();
        if (drag == Drag::move || drag == Drag::copy)
            drag = isCopyMode() ? Drag::copy : Drag::move;
        if (drag == Drag::marquee && anchor.valid() && !controller.cell (anchor).enabled
            && !e.mouseWasDraggedSinceMouseDown() && origin.getDistanceFrom (e.position) < 5.0f)
            updateSelection();
        if (drag == Drag::loopStart || drag == Drag::loopEnd)
        {
            const auto previous = std::exchange (lastHandleClick, std::nullopt);
            if (!e.mouseWasDraggedSinceMouseDown() && origin.getDistanceFrom (e.position) < 4.0f)
            {
                if (previous && previous->channel == anchor.channel && previous->side == drag
                    && (e.mouseDownTime - previous->time).inMilliseconds() <= juce::MouseEvent::getDoubleClickTimeout())
                {
                    const int delta = drag == Drag::loopStart ? -1 : 1;
                    drag = Drag::none;
                    controller.commit();
                    controller.shift (anchor.channel, delta);
                    updateCursor();
                    repaint();
                    return;
                }
                lastHandleClick = HandleClick { anchor.channel, drag, e.mouseDownTime };
            }
        }
        if (drag == Drag::pending && pendingVelocityEdge && !e.mouseWasDraggedSinceMouseDown())
        {
            velocityTap = VelocityTap { *pendingVelocityEdge, anchor, origin, e.mouseDownTime, !e.source.isMouse() };
            const int remaining = juce::MouseEvent::getDoubleClickTimeout() - e.getLengthOfMousePress();
            if (remaining > 0)
                velocityTapTimer.startTimer (remaining);
            else
                finishVelocityTap();
        }
        else if (drag == Drag::pending && anchor.valid() && !(selectedAtStart && isCopyMode()))
        {
            if (selectedAtStart && additive)
            {
                if (controller.cell (anchor).enabled)
                    controller.selected.reset (static_cast<size_t> (anchor.key()));
                else
                    controller.selected.reset();
            }
            else
                toggleCellsFromClick (anchor, selectedAtStart);
        }
        if ((drag == Drag::move || drag == Drag::copy) && target.valid())
            controller.moveSelection (anchor, target, drag == Drag::copy);
        drag = Drag::none;
        updateCursor();
        controller.commit();
        controller.notify();
        repaint();
    }
    void PatternGrid::cancelGesture()
    {
        gesturePointer.reset();
        const bool chanceWasHeld = std::exchange (controller.chanceHeld, false);
        stopTimer();
        pendingVelocityEdge.reset();
        updateVelocityAnchorPulse (true);
        velocityTapTimer.stopTimer();
        velocityTap.reset();
        velocityDrawPoint.reset();
        cancelPresetHold();
        cyclePress.reset();
        splitPress.reset();
        if (presetMenu != nullptr)
            presetMenu->close();
        clearHoverMarker();
        drag = Drag::none;
        rowSwipe.reset();
        lastHandleClick.reset();
        updateCursor();
        controller.cancel();
        if (chanceWasHeld)
            controller.notify();
        repaint();
    }

    void PatternGrid::cancelPresetHold()
    {
        presetHoldTimer.stopTimer();
        presetPress.reset();
        clearTouchStepPreview();
    }

    void PatternGrid::openHeldPreset()
    {
        const auto remaining = juce::roundToInt (std::ceil (presetHoldDeadline - juce::Time::getMillisecondCounterHiRes()));
        if (remaining > 0)
        {
            // JUCE's shared timer can include time elapsed before this press when its queue is busy.
            presetHoldTimer.startTimer (remaining);
            return;
        }
        presetHoldTimer.stopTimer();
        clearTouchStepPreview();
        const auto press = std::exchange (presetPress, std::nullopt);
        if (!press || !canSetPreset (*press))
            return;

        stopTimer();
        if (drag == Drag::velocityLine)
            controller.cancel();
        drag = Drag::none;
        pendingVelocityEdge.reset();
        cyclePress.reset();
        splitPress.reset();
        controller.selected = initialSelection;
        showCellPresets (*press, true);
        controller.notify();
        updateCursor();
        repaint();
    }

    bool PatternGrid::canSetPreset (const PresetPress& press) const
    {
        const auto location = press.location;
        if (press.source != controller.source() || press.epoch != controller.processor.getUiBridge().getDocumentEpoch()
            || !location.valid() || !layout.channelVisible (location.channel)
            || location.step >= layout.firstStep + layout.visibleSteps()
            || location.step + displayedSpan (location) <= layout.firstStep)
            return false;
        const auto loop = controller.loop (location.channel);
        return location.step >= loop.loopStart && location.step < loop.loopStart + loop.loopLength
            && (controller.cell (location).enabled || (press.allowEmpty && !coveringSpan (location).valid()));
    }

    void PatternGrid::showCellPresets (PresetPress press, bool touch)
    {
        if (presetMenu == nullptr)
            presetMenu = std::make_unique<PresetMenu> (*this);
        presetMenu->open (press, touch);
    }

    juce::Component* PatternGrid::openPresetMenu() const
    {
        return presetMenu != nullptr && presetMenu->isVisible() ? presetMenu.get() : nullptr;
    }

}
