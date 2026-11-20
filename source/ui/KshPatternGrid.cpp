#include "KshPatternGrid.h"
#include "KshCycleEditor.h"
#include "../engine/KshMath.h"
namespace ksh::ui
{
    namespace
    {
        constexpr float velocityEdgeHitRadius = 9.0f;
        constexpr float touchVelocityEdgeHitRadius = 13.0f;
        constexpr int selectHoldMs = 800;
        constexpr int touchPresetHoldMs = 1350;
        constexpr float presetPressSlop = 4.0f;
        constexpr float touchPresetPressSlop = 12.0f;

        int cellDoubleClickWindowMs()
        {
            // Bound the delay on single-click step edits without changing other double-click gestures.
            return juce::jmin (160, juce::MouseEvent::getDoubleClickTimeout());
        }

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
            RhythmPreset { "3-hit roll", 0.0, 3 }
        };
        int spanForPreset (const RhythmPreset& preset, double beatsPerStep)
        {
            if (preset.beats == 0.0)
                return 1;
            const double steps = preset.beats / beatsPerStep;
            const int nearest = juce::roundToInt (steps);
            return std::abs (steps - nearest) < 1.0e-9 ? nearest : 0;
        }

        void paintInsetEdge (juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            g.setColour (Palette::app.withAlpha (0.22f));
            g.drawRoundedRectangle (bounds.reduced (0.3f), 1.5f, 0.6f);
            g.setColour (Palette::text.withAlpha (0.12f));
            g.drawHorizontalLine (juce::roundToInt (bounds.getBottom() - 1.0f), bounds.getX() + 1.5f, bounds.getRight() - 1.5f);
        }

        void paintCycleStrip (juce::Graphics& g, juce::Rectangle<float> strip, int length, int mask, juce::Colour ink)
        {
            const int bits = juce::jmin (length, 30);
            const float stride = strip.getWidth() / static_cast<float> (bits);
            for (int i = 0; i < bits; ++i)
            {
                // Split the two-pixel gap between both sides to centre the strip.
                const juce::Rectangle<float> bit (strip.getX() + 1.0f + static_cast<float> (i) * stride, strip.getY(),
                                                  juce::jmax (1.0f, stride - 2.0f), strip.getHeight());
                g.setColour (Palette::shadeBackground (juce::Colour (0xff101212)));
                g.fillRoundedRectangle (bit, 2);
                const juce::Graphics::ScopedSaveState cycleClip (g);
                juce::Path clip;
                clip.addRoundedRectangle (bit, 2);
                g.reduceClipRegion (clip);
                g.setColour ((mask & (1 << i)) != 0 ? ink : Palette::app);
                g.fillRect (bit);
                paintInsetEdge (g, bit);
            }
        }

        juce::Rectangle<float> cycleStripBounds (juce::Rectangle<float> body)
        {
            return body.withY (body.getY() + body.getHeight() * 0.8f - 3.0f)
                       .withHeight (body.getHeight() * 0.14f + 3.0f).reduced (3, 0);
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
            constexpr int gap = 10;
            const int below = step.getBottom() + gap;
            const int above = step.getY() - gap - height;
            const int y = below + height <= available.getBottom() || above < available.getY() ? below : above;
            return juce::Rectangle<int> (step.getCentreX() - width / 2, y, width, height).constrainedWithin (available);
        }

        juce::String accessibleDescription (const Cell& value)
        {
            return juce::String (value.enabled ? "On" : "Off") + ", velocity " + juce::String (value.velocity) + ", probability " + juce::String (value.probability) + ", split " + juce::String (value.roll) + ", cycle " + juce::String (value.cycle) + ", nudge " + juce::String (value.nudge) + "%" + (value.span > 1 ? ", span " + juce::String (value.span) + " steps" : juce::String());
        }
    }

    class PatternGrid::PresetMenu final : public juce::Component
    {
    public:
        explicit PresetMenu (PatternGrid& grid) : owner (grid)
        {
            setTitle ("Step presets");
            setAlwaysOnTop (true);
            for (const auto& preset : rhythmPresets)
            {
                auto* item = items.add (new PresetButton (preset));
                addAndMakeVisible (*item);
                item->addMouseListener (this, false);
                item->onClick = [this, item] { choosePreset (item->preset, touch); };
            }
            for (auto* input : { &span, &split })
            {
                addAndMakeVisible (*input);
                input->minimum = input->resetValue = 1;
                input->horizontalDragOnTouch = true;
                input->setTooltip ("Drag to adjust or double-click to enter a value.");
            }
            span.setTitle ("Step span");
            span.format = [] (int value) { return juce::String (value) + (value == 1 ? " step" : " steps"); };
            split.setTitle ("Step split");
            split.maximum = Constants::maxRoll;
            // Keep the popup anchored while scrubbing, since a new span changes the cell bounds.
            span.onChange = [this] (int value) { pendingSpan = value; };
            split.onChange = [this] (int value) { pendingSplit = value; };
            span.onEnd = [this] { finishManualEdit (true); };
            split.onEnd = [this] { finishManualEdit (false); };
        }

        bool isTrackingPress() const { return trackingPress; }
        bool isTouchMenu() const { return touch && isVisible(); }

        void open (PresetPress next, bool touchInput, bool trackPress)
        {
            press = next;
            touch = touchInput;
            trackingPress = trackPress;
            auto* container = owner.getParentComponent();
            if (container == nullptr)
                container = &owner;
            container->addChildComponent (*this);
            rowHeight = juce::jlimit (1, targetSize, (container->getHeight() - fixedHeight) / totalRows);
            const juce::Rectangle<int> bounds (2 * columnWidth + columnGap + 2 * padding, fixedHeight + rowHeight * totalRows);
            setBounds (stepPopupBounds (stepBounds (*container), container->getLocalBounds(), bounds.getWidth(), bounds.getHeight()));
            resized();
            setVisible (true);
            toFront (false);
            refresh();
        }

        void close()
        {
            trackingPress = false;
            pendingSpan.reset();
            pendingSplit.reset();
            span.cancelGesture();
            split.cancelGesture();
            setVisible (false);
        }

        void track (juce::Point<int> position)
        {
            const auto* highlighted = itemAt (position);
            for (auto* item : items)
                if (const bool active = item == highlighted; item->dragHighlighted != active)
                {
                    item->dragHighlighted = active;
                    item->repaint();
                }
        }

        void leaveAnchor (juce::Point<int> position)
        {
            if (isVisible() && !touch && !trackingPress
                && !owner.getLocalArea (this, getLocalBounds()).getUnion (owner.cellBounds (press.location).getSmallestIntegerContainer()).contains (position))
                close();
        }

        void release (juce::Point<float> position)
        {
            trackingPress = false;
            track ({ -1, -1 });
            if (press.position.getDistanceFrom (position) < presetPressSlop)
                return;
            if (const auto* item = itemAt (getLocalPoint (&owner, position).roundToInt()))
                choosePreset (item->preset, true);
            else if (!touch)
                close();
        }

        void refresh()
        {
            if (!isVisible())
                return;
            if (!owner.canSetPreset (press))
            {
                close();
                return;
            }
            if (auto* container = getParentComponent())
                setBounds (stepPopupBounds (stepBounds (*container), container->getLocalBounds(), getWidth(), getHeight()));
            const auto& savedCell = owner.controller.cell (press.location);
            const auto cell = savedCell.enabled ? savedCell : Cell {};
            const auto colour = Palette::channel (press.location.channel);
            if (std::exchange (accent, colour) != colour)
                repaint();
            for (auto* item : items)
            {
                const int length = spanForPreset (item->preset, owner.controller.state.playback.beatsPerStep);
                const bool enabled = owner.canFitSpan (press.location, length);
                item->setAccent (colour);
                item->setEnabled (enabled);
                item->setToggleState (enabled && cell.enabled && cell.span == length && cell.roll == item->preset.split, juce::dontSendNotification);
            }
            span.maximum = juce::jmax (manualSpanLimit, cell.span);
            while (span.maximum > span.minimum && !owner.canFitSpan (press.location, span.maximum))
                --span.maximum;
            span.setEnabled (span.maximum > span.minimum || cell.span > span.minimum);
            span.setValue (cell.span);
            split.setValue (cell.roll);
        }

        void mouseExit (const juce::MouseEvent& event) override
        {
            if (!touch && !trackingPress && !getLocalBounds().contains (event.getEventRelativeTo (this).getPosition()))
                close();
        }

        void resized() override
        {
            for (int i = 0; i < items.size(); ++i)
                items[i]->setBounds (columnX (i / topRows), headingHeight + (i % topRows) * rowHeight, columnWidth, rowHeight);
            const int inputY = manualHeaderY() + headingHeight;
            span.setBounds (columnX (0) + 8, inputY, 80, rowHeight);
            split.setBounds (columnX (1) + 8, inputY, targetSize, rowHeight);
        }

        void paint (juce::Graphics& g) override
        {
            getLookAndFeel().drawPopupMenuBackground (g, getWidth(), getHeight());
            g.setColour (accent.withAlpha (0.35f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 7.0f, 1.0f);
            const auto heading = [&] (const char* label, int column, int y)
            {
                const juce::Rectangle<int> bounds (columnX (column) + 8, y, columnWidth - 8, headingHeight);
                text (g, label, bounds.toFloat(), 13, Palette::muted, true, juce::Justification::centredLeft);
            };
            heading ("Presets", 0, 0);
            heading ("Span", 0, manualHeaderY());
            heading ("Split", 1, manualHeaderY());
            g.setColour (Palette::border.withAlpha (0.65f));
            g.drawHorizontalLine (manualHeaderY() - 3, static_cast<float> (padding + 2), static_cast<float> (getWidth() - padding - 2));
        }

    private:
        juce::Rectangle<int> stepBounds (const juce::Component& container) const
        {
            return container.getLocalArea (&owner, owner.cellBounds (press.location).getSmallestIntegerContainer())
                .getIntersection (container.getLocalArea (&owner, owner.getLocalBounds()));
        }

        class PresetButton final : public juce::TextButton
        {
        public:
            explicit PresetButton (const RhythmPreset& rhythm) : juce::TextButton (rhythm.label), preset (rhythm) {}
            void setAccent (juce::Colour colour)
            {
                if (std::exchange (accent, colour) != colour)
                    repaint();
            }
            void paintButton (juce::Graphics& g, bool hovered, bool down) override
            {
                const bool selected = getToggleState();
                const bool highlight = isEnabled() && (hovered || down || dragHighlighted);
                const auto bounds = getLocalBounds().toFloat().reduced (2.0f, 1.0f);
                if (selected || highlight)
                {
                    g.setColour (accent.withAlpha (highlight ? 0.22f : 0.12f));
                    g.fillRoundedRectangle (bounds, 4.0f);
                }
                const auto ink = !isEnabled() ? Palette::faint : selected ? accent : Palette::text;
                ksh::ui::text (g, getButtonText(), bounds.withTrimmedLeft (6.0f).withTrimmedRight (selected ? 17.0f : 4.0f),
                               14, ink, selected, juce::Justification::centredLeft);
                if (selected)
                {
                    const float x = bounds.getRight() - 13.0f, y = bounds.getCentreY();
                    juce::Path tick;
                    tick.startNewSubPath (x, y);
                    tick.lineTo (x + 3.0f, y + 3.0f);
                    tick.lineTo (x + 9.0f, y - 4.0f);
                    g.setColour (accent);
                    g.strokePath (tick, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }
            const RhythmPreset& preset;
            bool dragHighlighted = false;

        private:
            juce::Colour accent;
        };

        void choosePreset (const RhythmPreset& preset, bool dismiss)
        {
            owner.controller.refresh();
            const int length = spanForPreset (preset, owner.controller.state.playback.beatsPerStep);
            if (applyRhythm (length, preset.split, preset.beats == 0.0 ? "Set roll preset" : "Set triplet preset") && dismiss)
                close();
            refresh();
        }

        void finishManualEdit (bool adjustingSpan)
        {
            const auto value = std::exchange (adjustingSpan ? pendingSpan : pendingSplit, std::nullopt);
            if (!value || !isVisible())
                return;
            owner.controller.refresh();
            if (applyRhythm (adjustingSpan ? value : std::nullopt, adjustingSpan ? std::nullopt : value,
                             adjustingSpan ? "Set step span" : "Set split") && touch)
                close();
            refresh();
        }

        bool applyRhythm (std::optional<int> length, std::optional<int> count, const juce::String& label)
        {
            auto& editor = owner.controller;
            if (!isVisible() || editor.editing() || !owner.canSetPreset (press)
                || (length && !owner.canFitSpan (press.location, *length)))
                return false;

            const auto& savedCell = editor.cell (press.location);
            auto cell = savedCell.enabled ? savedCell : Cell {};
            const int nextSpan = length.value_or (cell.span);
            const int nextSplit = count.value_or (cell.roll);
            if (cell.span == nextSpan && cell.roll == nextSplit)
                return true;

            cell.enabled = true;
            cell.span = nextSpan;
            cell.roll = nextSplit;
            editor.begin (label);
            editor.editCell (press.location, cell);
            editor.commit();
            return true;
        }

        const PresetButton* itemAt (juce::Point<int> position) const
        {
            for (const auto* item : items)
                if (item->isVisible() && item->isEnabled() && item->getBounds().contains (position))
                    return item;
            return nullptr;
        }

        int columnX (int column) const { return padding + column * (columnWidth + columnGap); }
        int manualHeaderY() const { return headingHeight + topRows * rowHeight + sectionGap; }
        static constexpr int padding = 6, columnWidth = 154, columnGap = 8, headingHeight = 24, sectionGap = 8;
        static constexpr int targetSize = 42, manualSpanLimit = 8;
        static constexpr int topRows = 3, totalRows = topRows + 1;
        static constexpr int fixedHeight = 2 * headingHeight + sectionGap + padding;
        int rowHeight = 0;
        PatternGrid& owner;
        juce::OwnedArray<PresetButton> items;
        ValueControl span, split;
        std::optional<int> pendingSpan, pendingSplit;
        PresetPress press {};
        juce::Colour accent;
        bool trackingPress = false, touch = false;
    };

    class PatternGrid::CyclePopup final : public juce::Component
    {
    public:
        explicit CyclePopup (PatternGrid& grid) : owner (grid), editor (grid.controller)
        {
            setTitle ("Step cycle editor");
            setAlwaysOnTop (true);
            setWantsKeyboardFocus (true);
            addMouseListener (this, true);
            editor.inlineLayout = true;
            editor.previewCycleLayer = false;
            editor.useAnchorPatternForGroup = true;
            editor.locations = [this]
            {
                if (!target || owner.controller.selected != selectionAtOpen || !owner.canSetPreset (*target))
                    return std::vector<Location> {};

                const auto location = target->location;
                if (editor.split >= 0 || !selectionAtOpen.test (static_cast<size_t> (location.key())))
                    return std::vector<Location> { location };

                auto locations = owner.controller.selection (true);
                // Display and edit from the cycle on the step that opened the popup.
                if (const auto clicked = std::find (locations.begin(), locations.end(), location); clicked != locations.end())
                    std::iter_swap (locations.begin(), clicked);
                return locations;
            };
            addAndMakeVisible (editor);
        }

        ~CyclePopup() override { removeMouseListener (this); }

        void open (PresetPress press, bool touch, int split = -1)
        {
            target = press;
            selectionAtOpen = owner.controller.selected;
            editor.compact = owner.layout.compact || !touch;
            editor.split = split;
            editor.forwardEscape = owner.isSelectedChanceMode();
            setTitle (split >= 0 ? "Split cycle editor" : "Step cycle editor");
            auto* parent = owner.getParentComponent();
            (parent != nullptr ? parent : &owner)->addChildComponent (*this);
            setVisible (true);
            refresh();
            toFront (false);
        }

        void close (bool commitPendingClick = false)
        {
            if (commitPendingClick)
                editor.commitPendingBeatClick();
            target.reset();
            setVisible (false);
            editor.cancelGesture();
        }

        void refresh()
        {
            if (!target)
                return;
            const auto& value = owner.controller.cell (target->location);
            const bool isSplit = editor.split >= 0;
            const auto layer = owner.displayLayer();
            if ((layer != Layer::chance && !(isSplit && layer == Layer::velocity))
                || owner.controller.selected != selectionAtOpen || !owner.canSetPreset (*target)
                || (isSplit && (value.roll < 2 || editor.split >= value.roll)))
            {
                close();
                return;
            }
            const auto* parent = getParentComponent();
            const auto cell = parent->getLocalArea (&owner, owner.cellBounds (target->location)).toNearestInt();
            const auto available = parent->getLocalBounds();
            const int cycleHeight = editor.compact ? 52 : 56;
            const int width = editor.preferredWidth (cycleHeight);
            setBounds (stepPopupBounds (cell, available, width, cycleHeight));
            editor.setBeatAccent (Palette::channel (target->location.channel, owner.controller.drumColors));
            setDescription (isSplit ? "Split " + juce::String (editor.split + 1) + " of " + juce::String (value.roll)
                                           + ". Shift and Left or Right chooses a split."
                                    : juce::String());
            editor.sync();
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Palette::shell);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
        }
        void resized() override { editor.setBounds (getLocalBounds()); }
        void updatePlayback() { editor.updatePlayback(); }
        bool keyPressed (const juce::KeyPress& key) override
        {
            const int direction = key.isKeyCode (juce::KeyPress::leftKey) ? -1 : key.isKeyCode (juce::KeyPress::rightKey) ? 1 : 0;
            if (!target || editor.split < 0 || !key.getModifiers().isShiftDown() || direction == 0)
                return false;

            const auto& cell = owner.controller.cell (target->location);
            const int next = juce::jlimit (0, cell.roll - 1, editor.split + direction);
            if (next != editor.split)
            {
                editor.commitPendingBeatClick();
                editor.cancelGesture();
                editor.split = next;
                refresh();
            }
            return true;
        }
        void mouseExit (const juce::MouseEvent& event) override
        {
            if (!event.mods.isAnyMouseButtonDown())
                dismissAfterPointerLeaves (event);
        }
        void mouseUp (const juce::MouseEvent& event) override { dismissAfterPointerLeaves (event); }

    private:
        void dismissAfterPointerLeaves (const juce::MouseEvent& event)
        {
            if (isVisible() && event.source.canHover() && !getLocalBounds().contains (event.getEventRelativeTo (this).getPosition()))
                close (true);
        }
        PatternGrid& owner;
        std::optional<PresetPress> target;
        Selection selectionAtOpen;
        CycleEditor editor;
    };

    PatternGrid::~PatternGrid() = default;

    bool PatternGrid::hasActiveGesture() const
    {
        return drag != Drag::none || velocityTap.has_value() || (cycleTap && cycleTap->singleClick != CycleTapAction::none) || presetPress.has_value() || cyclePress.has_value() || splitPress.has_value() || mutePainting || pendingMute >= 0 || directionSwipe.has_value()
            || (cyclePopup != nullptr && cyclePopup->isVisible())
            || (presetMenu != nullptr && presetMenu->isVisible());
    }

    bool PatternGrid::dismissCyclePopup (bool commitPendingClick)
    {
        if (cyclePopup == nullptr || !cyclePopup->isVisible())
            return false;
        cyclePopup->close (commitPendingClick);
        return true;
    }

    bool PatternGrid::handleCellPopupMouseDown (const juce::Component* clicked)
    {
        const auto clickedInside = [clicked] (const juce::Component* popup)
        {
            return clicked == popup || (clicked != nullptr && popup->isParentOf (clicked));
        };
        if (cyclePopup != nullptr && cyclePopup->isVisible())
        {
            if (clickedInside (cyclePopup.get()))
                return true;
            if (clicked != this)
                cyclePopup->close (true);
        }
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
                cell.setDescription (accessibleDescription (displayedCells[static_cast<size_t> (location.key())]));
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
            r.label.onClick = [this, ch] { if (rowTriggered) rowTriggered (ch); };
            r.label.showColourPicker = [this, ch]
            {
                if (!layout.phoneRowControls && trackColourRequested)
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
            for (auto* button : { &r.reverse, &r.ping })
            {
                const bool reverse = button == &r.reverse;
                button->onClick = [this, ch, reverse] { toggleDirection (ch, reverse); };
                button->pressed = [this, ch, reverse, button] (const juce::MouseEvent& e)
                {
                    if (e.mods.isLeftButtonDown() && !e.mods.isPopupMenu() && controller.source() >= 0 && !directionSwipe)
                        directionSwipe = DirectionSwipe { ch, layout.displayRow (ch), controller.source(), controller.state.documentEpoch,
                                                          reverse, !button->getToggleState() };
                };
                button->dragged = [this, ch, reverse] (const juce::MouseEvent& e)
                {
                    if (!directionSwipeCurrent() || directionSwipe->channel != ch || directionSwipe->reverse != reverse)
                        return;
                    const int channel = layout.channelAtY (e.getEventRelativeTo (this).position.y);
                    if (channel < 0)
                        return;
                    const int row = layout.displayRow (channel);
                    const auto swipe = *directionSwipe;
                    if (row == swipe.lastRow)
                        return;
                    directionSwipe->lastRow = row;
                    directionSwipe->painting = true;
                    if (!swipe.painting)
                    {
                        controller.begin (reverse ? "Paint reverse playback" : "Paint ping-pong playback");
                        setDirection (ch, reverse, swipe.value);
                    }
                    const int delta = row > swipe.lastRow ? 1 : -1;
                    for (int next = swipe.lastRow + delta; next != row + delta; next += delta)
                        setDirection (layout.channelForRow (next), reverse, swipe.value);
                };
                button->released = [this, ch, reverse, button] (const juce::MouseEvent& e)
                {
                    if (!directionSwipeCurrent() || directionSwipe->channel != ch || directionSwipe->reverse != reverse)
                        return;
                    const auto swipe = std::exchange (directionSwipe, std::nullopt);
                    if (swipe->painting)
                        controller.commit();
                    else if (button->getLocalBounds().contains (e.getPosition()))
                        button->onClick();
                };
            }
            r.mute.onClick = [this, ch] { if (!controller.exitSolo()) controller.toggleMute (ch); };
            r.mute.pressed = [this, ch] (const juce::MouseEvent& e)
            {
                if (!e.mods.isLeftButtonDown() || e.mods.isPopupMenu() || controller.source() < 0 || pendingMute >= 0 || mutePainting)
                    return;
                pendingMute = muteGestureRow = ch;
                gestureSource = controller.source();
                gestureEpoch = controller.state.documentEpoch;
                const auto press = ++mutePress;
                if (layout.phoneRowControls)
                    return;
                const juce::Component::SafePointer<PatternGrid> safe (this);
                juce::Timer::callAfterDelay (soloHoldMs, [safe, press, ch]
                {
                    if (safe != nullptr && safe->mutePress == press && safe->pendingMute == ch)
                        safe->holdMute (ch);
                });
            };
            r.mute.dragged = [this, ch] (const juce::MouseEvent& e)
            {
                if (ch != muteGestureRow || !muteGestureCurrent() || (!mutePainting && pendingMute != ch) || e.getDistanceFromDragStart() < 4)
                    return;
                const int row = layout.channelAtY (e.getEventRelativeTo (this).position.y);
                if (!mutePainting && row == ch)
                {
                    if (!rows[static_cast<size_t> (ch)].mute.getLocalBounds().contains (e.getPosition()))
                        pendingMute = -1;
                    return;
                }
                pendingMute = -1;
                if (controller.soloedChannel() >= 0)
                    return;
                if (!mutePainting)
                {
                    mutePainting = true;
                    muteValue = !controller.state.playback.sourceChannelMutes[static_cast<size_t> (gestureSource)][static_cast<size_t> (ch)];
                    controller.begin ("Paint channel mutes");
                    paintMute (ch);
                }
                if (row >= 0)
                    paintMute (row);
            };
            r.mute.released = [this, ch, &r] (const juce::MouseEvent& e)
            {
                if (ch != muteGestureRow)
                    return;
                muteGestureRow = -1;
                if (mutePainting)
                {
                    mutePainting = false;
                    controller.commit();
                }
                if (pendingMute != ch)
                    return;
                if (muteGestureCurrent() && r.mute.getLocalBounds().contains (e.getPosition()))
                {
                    if (!layout.phoneRowControls && e.getLengthOfMousePress() >= soloHoldMs)
                        holdMute (ch);
                    else
                        r.mute.onClick();
                }
                pendingMute = -1;
            };
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

    bool PatternGrid::directionSwipeCurrent() const
    {
        return directionSwipe && directionSwipe->source == controller.source()
            && directionSwipe->epoch == controller.processor.getUiBridge().getDocumentEpoch();
    }

    bool PatternGrid::muteGestureCurrent() const
    {
        return gestureSource >= 0 && gestureSource == controller.source() && gestureEpoch == controller.processor.getUiBridge().getDocumentEpoch();
    }

    void PatternGrid::holdMute (int channel)
    {
        pendingMute = -1;
        if (muteGestureCurrent())
            controller.toggleSolo (channel);
    }

    void PatternGrid::paintMute (int channel)
    {
        if (controller.state.playback.sourceChannelMutes[static_cast<size_t> (gestureSource)][static_cast<size_t> (channel)] != muteValue)
            controller.command ("source_channel_mute", { gestureSource + 1, channel + 1, muteValue });
    }

    juce::Rectangle<float> PatternGrid::loopHandleBounds (int channel, bool end, bool touch) const
    {
        const auto loop = controller.loop (channel);
        const auto first = layout.cell ({ channel, loop.loopStart });
        const float edge = first.getX() + (end ? static_cast<float> (loop.loopLength) * layout.cellWidth() : 0.0f);
        const float x = edge + (end ? Layout::rowHandleOffset : -Layout::rowHandleOffset);
        const float width = touch ? static_cast<float> (Layout::touchTarget) : static_cast<float> (Layout::rowHandleWidth);
        auto bounds = juce::Rectangle<float> (width, static_cast<float> (Layout::touchTarget)).withCentre ({ x, first.getCentreY() });
        // Leave the centres of the steps on both sides of each grip available for editing.
        const float reach = layout.cellWidth() * 0.5f - 1.0f;
        if (end || loop.loopStart > 0)
            bounds.setLeft (juce::jmax (bounds.getX(), edge - reach));
        if (!end || loop.loopStart + loop.loopLength < layout.steps)
            bounds.setRight (juce::jmin (bounds.getRight(), edge + reach));
        if (end)
            bounds.setRight (juce::jmin (bounds.getRight(), layout.width() - Layout::rowClearWidth));
        return bounds;
    }
    std::optional<bool> PatternGrid::loopHandleAt (juce::Point<float> point, bool touch) const
    {
        const int row = layout.channelAtY (point.y);
        if (layout.compact || row < 0 || controller.source() < 0 || overviewLayer (displayLayer()))
            return {};
        if (loopHandleBounds (row, false, touch).contains (point))
            return false;
        if (loopHandleBounds (row, true, touch).contains (point))
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

        if (pointerInGrid && loopHandleAt (hoverPosition, false))
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
        return cell.enabled ? bounds.withX (hitPosition (location).x).withWidth (bounds.getWidth() * static_cast<float> (displayedSpan (location))) : bounds;
    }
    bool PatternGrid::canFitSpan (Location location, int span) const
    {
        const int source = controller.source();
        return source >= 0 && location.valid() && span >= 1 && span <= Constants::maxSteps
            && rollSpanFits (controller.state.playback.sources[static_cast<size_t> (source)][static_cast<size_t> (location.channel)],
                             controller.loop (location.channel), location.step, span);
    }
    int PatternGrid::displayedSpan (Location location) const
    {
        const auto& cell = controller.cell (location);
        return cell.enabled && cell.span > 1 && canFitSpan (location, cell.span) ? cell.span : 1;
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
        const int roll = juce::jlimit (1, Constants::maxRoll, cell.roll);
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
    Location PatternGrid::cellAt (juce::Point<float> point) const
    {
        // Match the topmost painted hit before falling back to the empty beat slot.
        const int channel = layout.channelAtY (point.y);
        if (channel >= 0 && point.x >= layout.gridLeft() && point.x < layout.gridLeft() + layout.gridWidth())
        {
            const auto loop = controller.loop (channel);
            for (int step = juce::jmin (loop.loopStart + loop.loopLength, layout.firstStep + layout.visibleSteps()) - 1; step >= loop.loopStart; --step)
            {
                const Location location { channel, step };
                if (controller.cell (location).enabled && (layout.stepVisible (step) || displayedSpan (location) > 1) && cellBounds (location).contains (point))
                    return location;
            }
        }
        const auto slot = layout.at (point);
        const auto owner = slot.valid() ? coveringSpan (slot) : Location {};
        return owner.valid() ? owner : slot;
    }
    Layer PatternGrid::displayLayer() const
    {
        if (drag != Drag::none && drag != Drag::marquee && drag != Drag::move && drag != Drag::copy)
            return dragLayer;
        return controller.inspectorHoverLayer.value_or (controller.chanceHeld || isSelectedChanceMode()
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
            const bool optionWasDown = controller.optionDown (modifiers);
            modifiers = m;
            const bool optionDown = controller.optionDown (m);
            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto* parent = getParentComponent();
            const bool canLatch = controller.selected.any() && isVisible() && !hasActiveGesture() && !controller.editing()
                && dynamic_cast<juce::TextEditor*> (getCurrentlyFocusedComponent()) == nullptr
                && (!isShowing() || (parent != nullptr ? parent->hasKeyboardFocus (true) : hasKeyboardFocus (true)));
            if (m == juce::ModifierKeys (juce::ModifierKeys::altModifier) && optionDown)
            {
                if (!optionWasDown && canLatch)
                    optionTap = OptionTap { now, controller.selected };
            }
            else if (const auto tap = std::exchange (optionTap, std::nullopt);
                     tap && optionWasDown && m == juce::ModifierKeys() && canLatch
                     && now - tap->started < 300.0 && controller.selected == tap->selection && !selectedChance)
            {
                selectedChance = true;
                controller.notify();
            }
            if (drag == Drag::move || drag == Drag::copy)
                drag = isCopyMode() ? Drag::copy : Drag::move;
            if (hoverEdge && velocityHintAt (hoverPosition) != hoverEdge)
                clearHoverMarker();
            repaint();
            if (const auto layer = displayLayer(); layer != previousLayer)
            {
                if (layer != Layer::chance)
                    dismissCyclePopup();
                if (layerChanged)
                    layerChanged();
            }
        }
        updateCursor();
    }
    std::optional<PatternGrid::VelocityEdge> PatternGrid::velocityHintAt (juce::Point<float> point) const
    {
        if (controller.source() < 0 || displayLayer() != Layer::velocity || controller.selected.any()
            || controller.shiftDown (modifiers) || modifiers.isAnyMouseButtonDown() || loopHandleAt (point, false).has_value())
            return {};
        return velocityEdgeAt (point);
    }
    juce::Point<float> PatternGrid::hoverMarkerPoint() const
    {
        return { velocityLineStartX (*hoverEdge, false), hoverPosition.y };
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
                hits += cell.roll;
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
            return cellBounds (right).getX();
        if (edge.step > loop.loopStart && edge.step <= loopEnd)
        {
            const Location left { edge.channel, edge.step - 1 };
            const auto previous = controller.cell (left).enabled ? left : coveringSpan (left);
            if (previous.valid())
            {
                const float x = cellBounds (previous).getRight();
                return edge.step == layout.steps ? juce::jmin (x, layout.gridLeft() + layout.gridWidth()) : x;
            }
        }
        return layout.cell (right).getX();
    }
    float PatternGrid::velocityLineStartX (VelocityEdge edge, bool touch) const
    {
        const auto loop = controller.loop (edge.channel);
        const float x = velocityEdgeX (edge);
        if (edge.split > 0)
            return x;
        const float clearance = touch ? 0.5f : juce::jmin (velocityEdgeHitRadius, layout.cellWidth() * 0.25f);
        // Keep loop resizing first in hit testing, with the line target just inside the grip.
        if (edge.step == loop.loopStart)
            return juce::jmax (x, loopHandleBounds (edge.channel, false, touch).getRight() + clearance);
        if (edge.step == loop.loopStart + loop.loopLength)
            return juce::jmin (x, loopHandleBounds (edge.channel, true, touch).getX() - clearance);
        return x;
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
            const float edgeX = start ? velocityLineStartX (edge, touch) : velocityEdgeX (edge);
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
                if (!start || active || previous.valid())
                {
                    float radius = distance;
                    const auto hit = active ? location : previous;
                    if (start && controller.cell (hit).roll > 1)
                    {
                        const auto raw = cellBounds (hit);
                        const auto body = layout.cellArtwork (raw);
                        const float width = body.getWidth() / static_cast<float> (controller.cell (hit).roll);
                        radius = juce::jmin (radius, body.getX() - raw.getX() + width * (touch ? 0.3f : 0.25f));
                    }
                    consider ({ channel, step }, radius);
                }
            }
            if (active)
            {
                const auto roll = controller.cell (location).roll;
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
        if (!e.source.canHover())
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
        const auto previousHandle = pointerInGrid ? loopHandleAt (hoverPosition, false) : std::nullopt;
        pointerInGrid = true;
        hoverPosition = e.position;
        modifierKeysChanged (e.mods);
        const auto handle = loopHandleAt (hoverPosition, false);
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
        if (!e.source.canHover())
            return;
        if (drag == Drag::velocityDraw)
            velocityDrawPoint.reset();
        if (presetMenu != nullptr)
            presetMenu->leaveAnchor (e.getPosition());
        if (pointerInGrid && loopHandleAt (hoverPosition, false))
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
        layout.channels = p.channelCount;
        layout.steps = p.stepCount;
        const bool patternChanged = displayedSource != controller.source() || displayedEpoch != controller.state.documentEpoch;
        const bool layoutChanged = laidOut.has_value() && laidOut->first != layout;
        if (patternChanged || controller.selected.none())
        {
            selectedChance = false;
            cancelOptionTap();
        }
        if (patternChanged || layoutChanged)
        {
            velocityTapTimer.stopTimer();
            velocityTap.reset();
            cancelPresetHold();
            cancelCycleTap();
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
            cyclePress.reset();
            displayedSource = controller.source();
            displayedEpoch = controller.state.documentEpoch;
            pendingMute = muteGestureRow = -1;
            mutePainting = false;
            directionSwipe.reset();
            for (auto& row : rows)
                row.clearFlash.cancel();
        }
        if (hoverEdge && velocityHintAt (hoverPosition) != hoverEdge)
            clearHoverMarker();
        const bool fullPaint = displayedFirstStep != layout.firstStep || displayedSteps != layout.steps || displayedLayer != displayLayer() || displayedColors != controller.drumColors
                               || displayedGraphicalValues != controller.graphicalCellValues
                               || displayedSelection.any() != controller.selected.any()
                               || !laidOut.has_value() || laidOut->first != layout || swingChanged;
        displayedSwing = swing;
        displayedSteps = layout.steps;
        displayedFirstStep = layout.firstStep;
        displayedLayer = displayLayer();
        displayedColors = controller.drumColors;
        displayedGraphicalValues = controller.graphicalCellValues;
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
            r.label.setEnabled (p.deviceActive);
            r.label.allowColourPickerHold = layout.touchRows && !layout.phoneRowControls;
            r.label.setTooltip (layout.phoneRowControls ? "Triggers this row. Records when armed."
                                                        : "Tap to trigger this row. Right-click or hold to choose its color.");
            r.label.setColour (juce::TextButton::textColourOffId, Palette::channel (ch, controller.drumColors));
            r.note.setVisible (visible && !layout.compact);
            r.more.setVisible (visible && layout.compact);
            r.note.setValue (channel.note);
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
            if (displayedLoopStart[index] != range.loopStart || displayedLoopLength[index] != range.loopLength || displayedMutes[index] != muted)
                repaint (juce::roundToInt (layout.gridLeft()) - 36, juce::roundToInt (layout.cell ({ ch, 0 }).getY()) - 2, juce::roundToInt (layout.gridWidth()) + 72, juce::roundToInt (layout.rowHeight()) + 4);
            displayedLoopStart[index] = range.loopStart;
            displayedLoopLength[index] = range.loopLength;
            displayedMutes[index] = muted;
            const int solo = controller.soloedChannel();
            r.mute.setToggleState (controller.source() >= 0 && !muted, juce::dontSendNotification);
            if (static_cast<bool> (r.mute.getProperties()["soloActive"]) != (solo == ch))
            {
                r.mute.getProperties().set ("soloActive", solo == ch);
                r.mute.repaint();
            }
            r.mute.setTooltip (layout.phoneRowControls ? (solo >= 0 ? "Exit solo" : "Toggle row") :
                                                        (solo >= 0 ? "Tap to exit solo. Hold to solo this row." : "Hold to solo"));
        }
        for (int ch = 0; ch < Constants::maxChannels; ++ch)
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
                if (displayedCells[index] != value)
                    accessible.setDescription (accessibleDescription (value));
                displayedCells[index] = value;
                accessible.setVisible (layout.channelVisible (ch) && !coveringSpan (location).valid()
                    && step < layout.firstStep + layout.visibleSteps() && step + displayedSpan (location) > layout.firstStep);
                const auto loop = controller.loop (ch);
                accessible.setEnabled (controller.source() >= 0 && step >= loop.loopStart && step < loop.loopStart + loop.loopLength);
                accessible.setToggleState (value.enabled, juce::dontSendNotification);
            }
        updateCursor();
        displayedSelection = controller.selected;
        displayedNames = controller.noteNames;
        if (laidOut != std::pair { layout, getLocalBounds() })
            resized();
        else if (cyclePopup != nullptr)
            cyclePopup->refresh();
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
                    r.label.setBounds (4, y, 28, height);
                    r.more.setBounds (34, buttonY, side, side);
                    r.mute.setBounds (68, buttonY, side, side);
                }
                else
                {
                    r.label.setBounds (4, y, 26, height);
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
            r.label.setBounds (16, y, 40, height);
            constexpr int controlsLeft = 62;
            constexpr int gap = 2;
            const int handleWidth = layout.touchRows ? Layout::touchTarget : Layout::rowHandleWidth;
            const int controlsRight = juce::roundToInt (layout.gridLeft()) - Layout::rowHandleOffset - handleWidth / 2 - gap;
            juce::Rectangle<int> controls (controlsLeft, y, controlsRight - controlsLeft, height);
            const std::array<juce::Component*, 4> buttons { &r.note, &r.reverse, &r.ping, &r.mute };
            for (size_t i = 0; i < buttons.size(); ++i)
            {
                const int remaining = static_cast<int> (buttons.size() - i);
                buttons[i]->setBounds (controls.removeFromLeft ((controls.getWidth() - (remaining - 1) * gap) / remaining));
                controls.removeFromLeft (gap);
            }
            r.note.valueFontSize = 16;
            r.clear.setBounds (gridRight + Layout::rowEndHandleSpace, y, Layout::rowClearWidth, height);
        }
        if (cyclePopup != nullptr)
            cyclePopup->refresh();
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
        const auto gridBackdrop = gridBackdropColour();
        const auto loopTint = Palette::accent.withAlpha (0.05f);
        g.fillAll (gridBackdrop);
        const auto layer = displayLayer();
        const float headingY = layout.cellsY() - 18.0f;
        if (layout.compact && overviewLayer (layer))
        {
            const auto name = layer == Layer::nudge ? "Nudge" : layer == Layer::roll ? "Split" : "Cycle";
            const float y = layout.phoneRowControls ? headingY : layout.cellsY() - 32.0f;
            text (g, juce::String (name) + " / overview", { 16.0f, y, layout.gridLeft() - 24.0f, layout.phoneRowControls ? 16.0f : 22.0f }, 12, Palette::accent, true, juce::Justification::centredLeft);
        }
        const auto referenceCell = layout.cellArtwork (layout.cell ({ 0, layout.firstStep }));
        const float valueSize = juce::jmin (18.0f, referenceCell.getWidth() * 0.38f, referenceCell.getHeight() * 0.5f);
        const bool hasSelection = controller.selected.any();
        const bool graphicalValues = controller.graphicalCellValues;
        if ((layer == Layer::chance && !graphicalValues) || layer == Layer::nudge)
            prepareProbabilityLabels (valueSize * 0.78f);
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
                    g.setColour (Palette::shadeBackground (juce::Colour (0xff101212)));
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
                                         r.getCentreY() - 3.0f + probabilityBaseline));
            }
            if (split && strip.getWidth() < 2.0f * static_cast<float> (cycle))
            {
                const float active = static_cast<float> (std::popcount (static_cast<unsigned int> (mask))) / static_cast<float> (cycle);
                g.setColour (Palette::app.interpolatedWith (foreground, active));
                g.fillRect (strip);
            }
            else
                paintCycleStrip (g, strip, cycle, mask, foreground);
        };
        const auto paintHit = [&] (const Cell& c, juce::Rectangle<float> r, juce::Colour colour, int span, float pulse, float slotX, bool selected)
        {
            const auto valueColour = Palette::app;
            const auto light = colour.brighter (0.1f);
            paintGridHitFill (g, r, colour, layer == Layer::velocity ? c.velocity : 127);
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
                    if (layer == Layer::velocity && c.splitVelocity (part) != c.velocity)
                        paintGridHitFill (g, r.withX (x).withWidth (end - x), colour, c.splitVelocity (part), 0.0f);
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
                    const double shade = 1.0 - c.velocity / 127.0;
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
            g.setColour (Palette::text.withAlpha (0.14f));
            g.drawRoundedRectangle (r.reduced (0.5f), 5, 1);
            if (layer == Layer::chance)
            {
                // Keep the hit visible above the inset graphics without flashing the labels.
                const auto foregroundPulse = 0.9f * pulse * pulse;
                const auto foreground = light.interpolatedWith (Palette::text, foregroundPulse);
                if (c.roll == 1 || (selected && isSelectedChanceMode()))
                    paintChance (r, c.probability, c.cycle, c.cycleMask, foreground, foregroundPulse, false);
                else
                    for (int part = 0; part < c.roll; ++part)
                    {
                        const auto& gate = c.splitGates[static_cast<size_t> (part)];
                        paintChance (splitSegment (r, c.roll, part), gate.probability(), gate.cycle(), gate.cycleMask(), foreground, foregroundPulse, true);
                    }
            }
            else if (layer == Layer::roll)
                text (g, juce::String (c.roll), r, valueSize, valueColour);
            else if (overviewLayer (layer))
            {
                const auto ink = valueColour;
                if (layer == Layer::nudge)
                {
                    const auto& label = nudgeLabels[static_cast<size_t> (juce::jlimit (-100, 100, c.nudge) + 100)];
                    g.setColour (ink);
                    label.glyphs.draw (g, juce::AffineTransform::translation (r.getCentreX() - label.width * 0.5f,
                                             r.getCentreY() - 3.0f + probabilityBaseline));
                    const auto bar = r.withY (r.getY() + r.getHeight() * 0.8f - 3.0f).withHeight (r.getHeight() * 0.14f + 3.0f).reduced (4, 0);
                    g.setColour (ink.withMultipliedAlpha (0.18f));
                    g.fillRoundedRectangle (bar, 1);
                    const float end = bar.getCentreX() + bar.getWidth() * 0.5f * static_cast<float> (c.nudge) / 100.0f;
                    g.setColour (ink.withMultipliedAlpha (0.8f));
                    g.fillRect (juce::jmin (bar.getCentreX(), end), bar.getY(), juce::jmax (1.0f, std::abs (end - bar.getCentreX())), bar.getHeight());
                    g.fillRect (bar.getCentreX() - 0.5f, bar.getY(), 1.0f, bar.getHeight());
                }
                else
                {
                    const auto strip = r.reduced (4, 0).withHeight (r.getHeight() * 0.24f).withCentre (r.getCentre());
                    for (int beat = 0; beat < c.cycle; ++beat)
                    {
                        const float stride = strip.getWidth() / static_cast<float> (c.cycle);
                        auto bit = juce::Rectangle<float> (strip.getX() + static_cast<float> (beat) * stride + 0.75f, strip.getY(), juce::jmax (1.0f, stride - 1.5f), strip.getHeight());
                        g.setColour ((c.cycleMask & (1 << beat)) != 0 ? light : Palette::app);
                        g.fillRoundedRectangle (bit, 1);
                    }
                }
            }
            else if (!graphicalValues)
            {
                for (int part = 0; part < c.roll; ++part)
                {
                    const auto segment = splitSegment (r, c.roll, part);
                    const float size = valueSize * 0.9f;
                    if (c.roll == 1 || segment.getWidth() >= 2.0f * size)
                        text (g, juce::String (c.roll == 1 ? c.velocity : c.splitVelocity (part)), segment.translated (0, 2), size, valueColour, true);
                }
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
            const bool muted = source >= 0 && controller.state.playback.sourceChannelMutes[static_cast<size_t> (source)][static_cast<size_t> (ch)];
            auto first = layout.cell ({ ch, range.loopStart });
            auto brace = first.withWidth (static_cast<float> (range.loopLength) * layout.cellWidth()).expanded (layout.compact ? 0.0f : static_cast<float> (Layout::rowEndHandleSpace), 1);
            if (layout.compact)
                brace = brace.getIntersection ({ layout.gridLeft(), brace.getY(), layout.gridWidth(), brace.getHeight() });
            else
                brace = brace.withSizeKeepingCentre (brace.getWidth(), layout.rowHeight());
            g.setColour (loopTint);
            g.fillRoundedRectangle (brace, 6);
            // Lay down all beat slots before drawing shifted hits over them.
            for (int pass = 0; pass < 2; ++pass)
            {
                const juce::Graphics::ScopedSaveState rowClip (g);
                const bool fadeHits = muted && pass == 1;
                if (fadeHits)
                {
                    const auto hitArea = brace.withX (layout.gridLeft()).withWidth (layout.gridWidth()).expanded (0.0f, 3.0f);
                    if (!g.reduceClipRegion (hitArea.getSmallestIntegerContainer()))
                        continue;
                    // Fade the complete step, including its roll divisions and Chance markings.
                    g.beginTransparencyLayer (0.16f);
                }
                for (int step = pass == 0 ? layout.firstStep : range.loopStart; step < layout.firstStep + layout.visibleSteps(); ++step)
                {
                    if (step < range.loopStart || step >= range.loopStart + range.loopLength)
                        continue;
                    const Location pos { ch, step };
                    if (pass == 1 && (!controller.cell (pos).enabled || (step < layout.firstStep && displayedSpan (pos) == 1)))
                        continue;
                    const auto slot = pass == 0 ? layout.cell (pos) : cellBounds (pos);
                    const auto cellArea = slot.expanded (3).getSmallestIntegerContainer();
                    if (!g.clipRegionIntersects (cellArea))
                        continue;
                    const auto& c = controller.cell (pos);
                    const auto r = layout.cellArtwork (slot);
                    const juce::Graphics::ScopedSaveState cellClip (g);
                    g.reduceClipRegion (juce::Rectangle<float> (layout.gridLeft(), r.getY() - 2, layout.gridWidth(), r.getHeight() + 4).getSmallestIntegerContainer());
                    auto colour = Palette::channel (ch, controller.drumColors);
                    if (c.enabled && hasSelection && !controller.selected.test (static_cast<size_t> (pos.key())))
                        colour = colour.withMultipliedSaturation (0.65f).interpolatedWith (Palette::app, 0.6f);
                    const auto remaining = controller.sourceFlashes[static_cast<size_t> (ch)][static_cast<size_t> (step)] - now;
                    const auto pulse = muted ? 0.0f : static_cast<float> (juce::jlimit (0.0, 1.0, remaining / EditorController::sourceFlashDurationMs));
                    if (pass == 0)
                        paintGridSlot (g, r, gridBackdrop, !c.enabled && step % 4 == 0);
                    else
                        paintHit (c, r, colour, displayedSpan (pos), pulse, slot.getX(), controller.selected.test (static_cast<size_t> (pos.key())));
                }
                if (fadeHits)
                    g.endTransparencyLayer();
            }
            // Use the filled silhouette for a full outer halo, keeping the cell interior clear.
            selectionGlow.setColor (Palette::channel (ch, controller.drumColors).brighter (0.3f).withAlpha (muted ? 0.20f : 0.92f), 0);
            for (int step = range.loopStart; step < layout.firstStep + layout.visibleSteps(); ++step)
            {
                if (step < range.loopStart || step >= range.loopStart + range.loopLength)
                    continue;
                const Location pos { ch, step };
                if ((step < layout.firstStep && displayedSpan (pos) == 1)
                    || !controller.selected.test (static_cast<size_t> (pos.key()))
                    || !g.clipRegionIntersects (cellPaintBounds (pos).getSmallestIntegerContainer()))
                    continue;
                const auto frame = layout.cellArtwork (cellBounds (pos)).expanded (0.65f);
                const juce::Graphics::ScopedSaveState selectionClip (g);
                g.reduceClipRegion (juce::Rectangle<float> (layout.gridLeft(), frame.getY() - selectionGlowExtent, layout.gridWidth(), frame.getHeight() + selectionGlowExtent * 2).getSmallestIntegerContainer());
                juce::Path outline;
                outline.addRoundedRectangle (frame, 5.65f);
                {
                    const juce::Graphics::ScopedSaveState glowClip (g);
                    juce::Path exterior;
                    exterior.setUsingNonZeroWinding (false);
                    exterior.addRectangle (frame.expanded (selectionGlowExtent));
                    exterior.addRoundedRectangle (frame.reduced (0.6f), 5.05f);
                    g.reduceClipRegion (exterior);
                    selectionGlow.render (g, outline);
                }
                g.setColour (Palette::text.withAlpha (muted ? 0.40f : 0.9f));
                g.drawRoundedRectangle (frame, 5.65f, 1.0f);
            }
            // Keep the loop boundary and handles above the cells.
            g.setColour (Palette::accent.withAlpha (0.42f));
            g.drawRoundedRectangle (brace, 6, 1);
            if (!layout.compact)
            {
                for (const bool end : { false, true })
                {
                    const auto bounds = loopHandleBounds (ch, end, false);
                    const auto handleDrag = end ? Drag::loopEnd : Drag::loopStart;
                    const bool highlighted = (drag == handleDrag && anchor.channel == ch)
                        || (drag == Drag::none && pointerInGrid && layout.channelAtY (hoverPosition.y) == ch
                            && loopHandleAt (hoverPosition, false) == end);
                    paintLengthGrip (g, bounds.withSizeKeepingCentre (bounds.getWidth(), brace.getHeight() - 6),
                                     highlighted ? Palette::accent : Palette::text);
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
        if (hoverEdge && velocityHintAt (hoverPosition) == hoverEdge)
        {
            constexpr double hoverDelayMs = 65.0, ripplePeriodMs = 550.0;
            const double elapsed = juce::Time::getMillisecondCounterHiRes() - hoverMarkerStarted - hoverDelayMs;
            if (elapsed >= 0.0)
            {
                const auto point = hoverMarkerPoint();
                const float phase = static_cast<float> (std::fmod (elapsed, ripplePeriodMs) / ripplePeriodMs);
                const float radius = 8.5f + phase * 5.0f;
                g.setColour (Palette::accent.withAlpha (0.68f * (1.0f - phase)));
                g.drawEllipse (point.x - radius, point.y - radius, radius * 2, radius * 2, 1.5f);
                paintMarker (point);
            }
        }
        if (drag == Drag::velocityLine && anchor.valid())
        {
            const auto row = layout.cell (anchor);
            const auto start = juce::Point<float> (velocityEdgeX (lineStart),
                row.getBottom() - static_cast<float> (lineVelocity - 1) / 126.0f * row.getHeight());
            const auto end = juce::Point<float> (velocityEdgeX (velocityLineEnd()),
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
                auto slot = layout.cell (destination);
                const auto span = displayedSpan (p);
                if (cell.enabled)
                {
                    const auto offset = static_cast<float> (cell.nudge / 100.0 + (span == 1 ? displayedSwingDelay (destination) : 0.0));
                    slot.translate (offset * layout.cellWidth(), 0.0f);
                    slot.setWidth (slot.getWidth() * static_cast<float> (span));
                }
                const auto body = layout.cellArtwork (slot);
                if (cell.enabled)
                    paintHit (cell, body, Palette::channel (destination.channel, controller.drumColors), span, 0.0f, slot.getX(), true);
                else
                    paintGridSlot (g, body, gridBackdrop, destination.step % 4 == 0);

                g.setColour (Palette::text.withAlpha (cell.enabled ? 0.9f : 0.38f));
                g.drawRoundedRectangle (body.reduced (0.5f), 5, 1);
            }
        }
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
        g.setColour (Palette::channel (anchor.channel, controller.drumColors).withAlpha (0.9f));
        g.drawRoundedRectangle (row.withX (1.0f).withWidth (area.getWidth() - 2.0f).reduced (0.75f), 5.0f, 1.5f);
    }

    void PatternGrid::visibilityChanged()
    {
        if (!isVisible())
        {
            clearHoverMarker();
            cancelPresetHold();
            cancelCycleTap();
            velocityTapTimer.stopTimer();
            velocityTap.reset();
            finishVelocityDrawing();
        }
    }

    void PatternGrid::focusLost (FocusChangeType)
    {
        cancelOptionTap();
        cancelPresetHold();
        finishVelocityTap();
        finishCycleTap();
        finishVelocityDrawing();
    }

    bool PatternGrid::keyPressed (const juce::KeyPress& key)
    {
        if (!key.isKeyCode (juce::KeyPress::escapeKey))
        {
            finishVelocityTap();
            finishCycleTap();
        }
        return handleCellKey (keyboardCell, key);
    }

    bool PatternGrid::handleCellKey (Location location, const juce::KeyPress& key)
    {
        cancelOptionTap();
        if (controller.source() < 0 || hasActiveGesture() || controller.editing())
            return false;
        if (key.isKeyCode (juce::KeyPress::F10Key) && key.getModifiers().isShiftDown() && !overviewLayer (displayLayer()))
        {
            const PresetPress press { location, {}, controller.source(), controller.state.documentEpoch };
            if (canSetPreset (press) && controller.cell (location).roll > 1)
            {
                showSplitEditor (press, 0, false);
                if (cyclePopup->isShowing())
                    cyclePopup->grabKeyboardFocus();
                return true;
            }
        }
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
            controller.editCell (p, Cell { .enabled = enable });
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
        if (cyclePopup != nullptr && cyclePopup->isVisible())
            cyclePopup->updatePlayback();
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
        cancelOptionTap();
        const bool popup = e.mods.isPopupMenu();
        const bool touch = !e.source.isMouse();
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
        pendingVelocityEdge.reset();
        cancelPresetHold();
        cyclePress.reset();
        if (isCycleResetClick (e))
        {
            dismissCyclePopup (true);
            finishCycleTap (true);
            return;
        }
        finishCycleTap();
        const bool cyclePopupWasOpen = dismissCyclePopup (true);
        if (presetMenu != nullptr)
        {
            const bool dismissTouchMenu = presetMenu->isTouchMenu() && touch;
            presetMenu->close();
            if (dismissTouchMenu)
                return;
        }
        clearHoverMarker();
        modifiers = e.mods;
        if (velocityTap && velocityTap->touch == touch
            && controller.source() == displayedSource && displayedEpoch == controller.processor.getUiBridge().getDocumentEpoch()
            && e.mods.isLeftButtonDown() && !popup && displayLayer() == Layer::velocity
            && controller.selected.none() && !controller.useSelectionModifier (e.mods)
            && !loopHandleAt (e.position, touch) && layout.channelAtY (e.position.y) == velocityTap->edge.channel
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
            if (layout.phoneRowControls)
                return;
            const PresetPress press { cellAt (e.position), e.position, controller.source(), controller.state.documentEpoch, true };
            if (canSetPreset (press) && !loopHandleAt (e.position, touch))
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
        const auto handle = loopHandleAt (e.position, touch);
        const PresetPress press { anchor, origin, controller.source(), controller.state.documentEpoch };
        if (touch && !handle)
        {
            auto heldPress = press;
            heldPress.allowEmpty = true;
            if (canSetPreset (heldPress))
            {
                presetPress = heldPress;
                presetHoldTimer.startTimer (touchPresetHoldMs);
            }
        }
        if (dragLayer == Layer::chance && !handle && !additive && initialSelection.none() && !isCopyMode()
            && anchor.valid() && beginSplitPress (press, cycleStripContains (anchor, origin)))
            return;
        if (dragLayer == Layer::chance && !handle && canClickCycleStrip (e.mods)
            && (!additive || selectedAtStart) && canSetPreset (press) && cycleStripContains (anchor, origin))
        {
            lastHandleClick.reset();
            cyclePress = press;
            if (!selectedAtStart)
                return;
        }
        if (cyclePopupWasOpen)
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
            if (anchor.step < loop.loopStart || anchor.step >= loop.loopStart + loop.loopLength)
                anchor = {};
            selectedAtStart = false;
            drag = Drag::pending;
            if (touch)
                startTimer (selectHoldMs);
            return;
        }
        if (velocityEdges && !isCopyMode() && beginSplitPress (press, false))
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
        valueVelocity = original.velocity;
        valueX = origin.x;
        originals.clear();
        if (selectedAtStart)
            for (auto p : controller.selection())
                originals.emplace_back (p, controller.cell (p));
        else
            originals.emplace_back (anchor, original);
        drag = Drag::pending;
        if (e.mods.isLeftButtonDown() && !additive && !isCopyMode() && !cyclePress)
        {
            startTimer (selectHoldMs);
        }
    }
    void PatternGrid::timerCallback()
    {
        stopTimer();
        if (drag != Drag::pending || controller.source() < 0)
            return;
        if (const auto edge = std::exchange (pendingVelocityEdge, std::nullopt))
        {
            if (velocityEdgeAt (origin, true) == edge)
            {
                beginVelocityLine (*edge);
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
                    if (!coveringSpan ({ ch, s }).valid() && hitArea.intersects (cellBounds ({ ch, s })))
                        controller.selected.set (static_cast<size_t> (Location { ch, s }.key()));
        }
        controller.notify();
        repaint();
    }
    int PatternGrid::velocityAtY (float y) const
    {
        const auto r = layout.cell (anchor);
        return juce::jlimit (1, 127, jsRound (1 + (1 - (y - r.getY()) / r.getHeight()) * 126));
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
        // A latched Select mode can still edit the selected steps through their cycle strips.
        return !controller.selectHeld && !(controller.keyboardModifiersEnabled && mods.isShiftDown()) && !isCopyMode();
    }

    bool PatternGrid::cycleStripContains (Location location, juce::Point<float> position) const
    {
        const auto body = cellBounds (location);
        const auto strip = cycleStripBounds (body.reduced (layout.cellInsetX(), Layout::cellInsetY));
        const auto paintedStrip = cycleStripBounds (layout.cellArtwork (body));
        return body.contains (position) && position.y >= juce::jmin (strip.getY(), paintedStrip.getY()) - 3.0f;
    }

    bool PatternGrid::beginSplitPress (const PresetPress& press, bool openEditor)
    {
        if (!canSetPreset (press))
            return false;
        const auto& cell = controller.cell (press.location);
        const auto body = layout.cellArtwork (cellBounds (press.location));
        const float width = body.getWidth() / static_cast<float> (cell.roll);
        if (cell.roll < 2 || width <= 0.0f)
            return false;
        const int index = juce::jlimit (0, cell.roll - 1, static_cast<int> ((press.position.x - body.getX()) / width));
        const int value = dragLayer == Layer::velocity ? cell.splitVelocity (index)
                            : cell.splitGates[static_cast<size_t> (index)].probability();
        splitPress = SplitPress { press, index, value, openEditor };
        return true;
    }

    void PatternGrid::showSplitEditor (const PresetPress& press, int index, bool touch)
    {
        if (cyclePopup == nullptr)
            cyclePopup = std::make_unique<CyclePopup> (*this);
        cyclePopup->open (press, touch, index);
    }

    void PatternGrid::setSplitValue (Location location, int index, Layer layer, int value)
    {
        auto cell = controller.cell (location);
        if (!cell.enabled || cell.roll < 2 || index >= cell.roll)
            return;
        if (layer == Layer::velocity)
        {
            const int next = juce::jlimit (1, 127, value);
            cell.setSplitVelocity (index, next == cell.velocity ? 0 : next);
        }
        else
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
            if (splitPress->openEditor || std::abs (delta.y) < juce::jmax (6.0f, std::abs (delta.x)) || controller.editing())
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
                dragVelocityValues (event.position.x, step, juce::jlimit (1, 127, splitPress->value + dragOffset (-delta.y, 1.25f)));
                controller.notify();
            }
            else
            {
                const auto track = probabilityTrack (layout.cellArtwork (cellBounds (splitPress->target.location)), true);
                const float sensitivity = juce::jmax (0.30f, track.getHeight() / 100.0f);
                setSplitValue (splitPress->target.location, splitPress->index, dragLayer, splitPress->value + dragOffset (-delta.y, sensitivity));
            }
        }
    }

    void PatternGrid::queueCycleTap (PresetPress press, const juce::MouseEvent& event, CycleTapAction singleClick)
    {
        cycleTap = CycleTap { press, controller.cell (press.location), controller.selected,
                              event.mouseDownTime, singleClick, !event.source.isMouse() };
        const int remaining = cellDoubleClickWindowMs() - event.getLengthOfMousePress();
        if (remaining > 0)
            cycleTapTimer.startTimer (remaining);
        else
            finishCycleTap();
    }

    bool PatternGrid::isCycleResetClick (const juce::MouseEvent& event) const
    {
        if (!cycleTap || event.getNumberOfClicks() < 2 || !event.mods.isLeftButtonDown() || event.mods.isPopupMenu()
            || !canClickCycleStrip (event.mods) || displayLayer() != Layer::chance
            || (event.mouseDownTime - cycleTap->time).inMilliseconds() > cellDoubleClickWindowMs())
            return false;

        const auto location = cycleTap->press.location;
        return cycleTap->touch == !event.source.isMouse() && !loopHandleAt (event.position, cycleTap->touch)
            && cellAt (event.position) == location && cycleStripContains (location, event.position);
    }

    void PatternGrid::cancelCycleTap()
    {
        cycleTapTimer.stopTimer();
        cycleTap.reset();
    }

    void PatternGrid::finishCycleTap (bool reset)
    {
        cycleTapTimer.stopTimer();
        const auto tap = std::exchange (cycleTap, std::nullopt);
        if (!tap || (!reset && tap->singleClick == CycleTapAction::none) || controller.editing()
            || tap->press.epoch != controller.processor.getUiBridge().getDocumentEpoch())
            return;
        controller.refresh();
        const auto location = tap->press.location;
        if (!canSetPreset (tap->press) || controller.cell (location) != tap->cell || controller.selected != tap->selection)
            return;

        const auto index = static_cast<size_t> (location.key());
        const bool selected = tap->selection.test (index);
        if (reset)
        {
            const auto cells = selected ? controller.selection (true) : std::vector<Location> { location };
            controller.begin ("Reset cycle");
            controller.setCycle (CellDefaults::cycle, CellDefaults::cycleMask, cells);
            controller.commit();
        }
        else if (tap->singleClick == CycleTapAction::toggle)
        {
            toggleCellsFromClick (location, selected);
            controller.commit();
        }
        else if (tap->singleClick == CycleTapAction::select)
        {
            controller.selected.reset();
            controller.selected.set (index);
        }
        else if (tap->singleClick == CycleTapAction::deselect)
            controller.selected.reset (index);
        controller.notify();
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
            for (int part = 0; part < cell.roll; ++part)
            {
                const auto bounds = splitSegment (body, cell.roll, part).getIntersection (row);
                if (bounds.isEmpty() || bounds.getRight() <= left || bounds.getX() > right)
                    continue;
                const float x = bounds.contains (point) ? point.x : juce::jlimit (left, right, bounds.getCentreX());
                const float fraction = juce::approximatelyEqual (start.x, point.x) ? 1.0f : (x - start.x) / (point.x - start.x);
                const int velocity = velocityAtY (start.y + fraction * (point.y - start.y));
                if (cell.roll == 1)
                    cell.velocity = velocity;
                else
                    cell.setSplitVelocity (part, velocity == cell.velocity ? 0 : velocity);
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
                    next.setSplitVelocity (part, drawn == next.velocity ? 0 : drawn);
                }
            }
            else
            {
                if (s < first || s > last || coveringSpan (location).valid())
                    continue;
                if (!next.enabled)
                    next = Cell { .enabled = true };
                const float fraction = step == valueStep ? 1.0f : static_cast<float> (s - valueStep) / static_cast<float> (step - valueStep);
                next.velocity = interpolate (fraction);
            }
            controller.editCell (location, next);
        }
        valueStep = step;
        valueVelocity = velocity;
        valueX = x;
    }

    void PatternGrid::mouseDrag (const juce::MouseEvent& e)
    {
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
            if (presetPress->position.getDistanceFrom (current) < touchPresetPressSlop)
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
            if (const auto edge = std::exchange (pendingVelocityEdge, std::nullopt))
                beginVelocityLine (*edge);
            else
            {
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
                controller.editCell (location, Cell { .enabled = enable });
            }
        }
        else if (drag == Drag::value)
        {
            const float sensitivity = dragLayer == Layer::velocity ? 1.25f : 2.0f;
            const int offset = dragOffset (origin.y - current.y, sensitivity);
            if (dragLayer == Layer::velocity && !selectedAtStart)
                dragVelocityValues (current.x, target.step, juce::jlimit (1, 127, original.velocity + offset));
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
                            next.setSplitVelocity (part, drawn == c.velocity ? 0 : drawn);
                        }
                    }
                    else if (endBoundary != boundary && p.step >= juce::jmin (start, end) && p.step <= juce::jmax (start, end)
                             && !coveringSpan (p).valid())
                    {
                        const float fraction = static_cast<float> (std::abs (p.step - start)) / static_cast<float> (juce::jmax (1, std::abs (end - start)));
                        next.velocity = jsRound (static_cast<float> (lineVelocity) + fraction * static_cast<float> (velocity - lineVelocity));
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
        stopTimer();
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
            {
                if (heldSplit->openEditor)
                    showSplitEditor (heldSplit->target, heldSplit->index, touch);
                else
                    toggleCell (heldSplit->target.location);
            }
            return;
        }
        if (const auto press = std::exchange (cyclePress, std::nullopt))
        {
            drag = Drag::none;
            const bool touch = !e.source.isMouse();
            if (press->position.getDistanceFrom (e.position) < (touch ? touchPresetPressSlop : presetPressSlop) && canSetPreset (*press))
            {
                auto action = CycleTapAction::none;
                if (additive)
                    action = CycleTapAction::deselect;
                else if (layout.phoneRowControls && !isSelectedChanceMode())
                    action = initialSelection.any() && !selectedAtStart ? CycleTapAction::select : CycleTapAction::toggle;
                else
                {
                    if (cyclePopup == nullptr)
                        cyclePopup = std::make_unique<CyclePopup> (*this);
                    cyclePopup->open (*press, touch);
                }
                queueCycleTap (*press, e, action);
            }
            return;
        }
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
        cancelOptionTap();
        const bool chanceWasHeld = std::exchange (controller.chanceHeld, false);
        stopTimer();
        velocityTapTimer.stopTimer();
        velocityTap.reset();
        cancelCycleTap();
        velocityDrawPoint.reset();
        cancelPresetHold();
        cyclePress.reset();
        splitPress.reset();
        dismissCyclePopup();
        if (presetMenu != nullptr)
            presetMenu->close();
        clearHoverMarker();
        drag = Drag::none;
        mutePainting = false;
        pendingMute = muteGestureRow = -1;
        directionSwipe.reset();
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
    }

    void PatternGrid::openHeldPreset()
    {
        presetHoldTimer.stopTimer();
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

    void PatternGrid::showSelectedRhythmPresets()
    {
        const auto selected = controller.selection (true);
        if (selected.size() != 1)
            return;
        const juce::Rectangle<float> gridArea (layout.gridLeft(), 0.0f, layout.gridWidth(), static_cast<float> (getHeight()));
        const auto location = selected.front();
        const PresetPress press { location, cellBounds (location).getIntersection (gridArea).getCentre(), controller.source(), controller.state.documentEpoch };
        if (canSetPreset (press))
            showCellPresets (press, true, false);
    }

    void PatternGrid::showCellPresets (PresetPress press, bool touch, bool trackPress)
    {
        if (presetMenu == nullptr)
            presetMenu = std::make_unique<PresetMenu> (*this);
        presetMenu->open (press, touch, trackPress);
    }

    juce::Component* PatternGrid::openPresetMenu() const
    {
        return presetMenu != nullptr && presetMenu->isVisible() ? presetMenu.get() : nullptr;
    }

}
