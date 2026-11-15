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
        constexpr int touchPresetHoldMs = 1800;
        constexpr float presetPressSlop = 4.0f;
        struct CyclePreset { const char* label; int length; int mask; };
        constexpr std::array cyclePresets {
            CyclePreset { "Every pass", 1, 1 },
            CyclePreset { "First of 2", 2, 1 },
            CyclePreset { "Second of 2", 2, 2 },
            CyclePreset { "First of 3", 3, 1 },
            CyclePreset { "First two of 3", 3, 3 },
            CyclePreset { "First of 4", 4, 1 },
            CyclePreset { "Fourth of 4", 4, 8 },
            CyclePreset { "First three of 4", 4, 7 },
        };
        constexpr std::array rollPresets { 1, 2, 3, 4 };
        constexpr int firstRollMenuId = static_cast<int> (cyclePresets.size()) + 1;

        bool presetMatches (int id, const Cell& cell)
        {
            if (id >= firstRollMenuId)
                return rollPresets[static_cast<size_t> (id - firstRollMenuId)] == cell.roll;
            const auto& preset = cyclePresets[static_cast<size_t> (id - 1)];
            return preset.length == cell.cycle && preset.mask == cell.cycleMask;
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
                g.setColour (juce::Colour (0xff101212));
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

        juce::String accessibleDescription (const Cell& value)
        {
            return juce::String (value.enabled ? "On" : "Off") + ", velocity " + juce::String (value.velocity) + ", probability " + juce::String (value.probability) + ", roll " + juce::String (value.roll) + ", cycle " + juce::String (value.cycle) + ", nudge " + juce::String (value.nudge) + "%";
        }
    }

    class PatternGrid::PresetMenu final : public juce::Component
    {
    public:
        explicit PresetMenu (PatternGrid& grid) : owner (grid)
        {
            setTitle ("Step presets");
            setAlwaysOnTop (true);
            for (const auto& preset : cyclePresets)
                addPreset (preset.label);
            for (const int roll : rollPresets)
                addPreset (juce::String (roll));
        }

        bool isTrackingPress() const { return trackingPress; }
        bool isTouchMenu() const { return touch && isVisible(); }

        void open (PresetPress next, bool touchInput)
        {
            press = next;
            touch = touchInput;
            trackingPress = true;
            constexpr int presetRows = static_cast<int> (cyclePresets.size());
            const int preferredRowHeight = touch ? (owner.layout.compact ? 36 : 50) : 26;
            auto* container = static_cast<juce::Component*> (&owner);
            // Short grids can use the surrounding editor space to fit every preset.
            if (headerHeight + preferredRowHeight * presetRows + padding > owner.getHeight())
                if (auto* parent = owner.getParentComponent())
                    container = parent;
            container->addChildComponent (*this);
            rowHeight = juce::jlimit (1, preferredRowHeight, (container->getHeight() - headerHeight - padding) / presetRows);
            const auto centre = touch ? owner.cellBounds (press.location).getCentre() : press.position;
            const juce::Rectangle<int> bounds (2 * columnWidth + columnGap + 2 * padding, headerHeight + rowHeight * presetRows + padding);
            setBounds (bounds.withCentre (container->getLocalPoint (&owner, centre).roundToInt()).constrainedWithin (container->getLocalBounds()));
            setVisible (true);
            toFront (false);
            refresh();
        }

        void close()
        {
            trackingPress = false;
            setVisible (false);
        }

        void track (juce::Point<int> position)
        {
            const int highlighted = touch ? 0 : itemAt (position);
            for (auto* item : items)
                if (const bool active = item->id == highlighted; item->dragHighlighted != active)
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
            if (!touch && press.position.getDistanceFrom (position) >= presetPressSlop)
            {
                const int id = itemAt (getLocalPoint (&owner, position).roundToInt());
                close();
                owner.applyCellPreset (press, id);
            }
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
            const auto& cell = owner.controller.cell (press.location);
            const auto cycleInk = Palette::channel (press.location.channel, owner.controller.drumColors).brighter (0.1f);
            for (auto* item : items)
            {
                item->setToggleState (presetMatches (item->id, cell), juce::dontSendNotification);
                if (item->id < firstRollMenuId && item->cycleInk != cycleInk)
                {
                    item->cycleInk = cycleInk;
                    item->repaint();
                }
            }
        }

        void mouseExit (const juce::MouseEvent& event) override
        {
            if (!touch && !trackingPress && !getLocalBounds().contains (event.getEventRelativeTo (this).getPosition()))
                close();
        }

        void resized() override
        {
            for (auto* item : items)
            {
                const bool roll = item->id >= firstRollMenuId;
                const int row = item->id - (roll ? firstRollMenuId : 1);
                item->setBounds (padding + (roll ? columnWidth + columnGap : 0), headerHeight + row * rowHeight, columnWidth, rowHeight);
            }
        }

        void paint (juce::Graphics& g) override
        {
            getLookAndFeel().drawPopupMenuBackground (g, getWidth(), getHeight());
            text (g, "Cycle", { 14, 5, 100, 19 }, 13, Palette::muted, true, juce::Justification::centredLeft);
            text (g, "Roll", { 14 + columnWidth + columnGap, 5, 100, 19 }, 13, Palette::muted, true, juce::Justification::centredLeft);
        }

    private:
        class PresetButton final : public juce::TextButton
        {
        public:
            PresetButton (int idIn, const juce::String& label) : juce::TextButton (label), id (idIn) {}
            void paintButton (juce::Graphics& g, bool hovered, bool down) override
            {
                juce::PopupMenu::Item item;
                item.isTicked = getToggleState();
                getLookAndFeel().drawPopupMenuItemWithOptions (g, getLocalBounds(), hovered || down || dragHighlighted, item, {});
                if (id < firstRollMenuId)
                {
                    const auto& preset = cyclePresets[static_cast<size_t> (id - 1)];
                    const auto strip = getLocalBounds().toFloat().withSizeKeepingCentre (78.0f, 12.0f).withX (14.0f);
                    paintCycleStrip (g, strip, preset.length, preset.mask, cycleInk);
                }
                else
                {
                    ksh::ui::text (g, getButtonText(), getLocalBounds().toFloat().withTrimmedLeft (14.0f), 20.0f,
                                   item.isTicked ? Palette::accent : Palette::text, true, juce::Justification::centredLeft);
                }
            }
            const int id;
            bool dragHighlighted = false;
            juce::Colour cycleInk;
        };

        void addPreset (const juce::String& label)
        {
            auto* item = items.add (new PresetButton (items.size() + 1, label));
            addAndMakeVisible (*item);
            item->addMouseListener (this, false);
            item->onClick = [this, item]
            {
                owner.applyCellPreset (press, item->id);
                refresh();
            };
        }

        int itemAt (juce::Point<int> position) const
        {
            for (const auto* item : items)
                if (item->getBounds().contains (position))
                    return item->id;
            return 0;
        }

        static constexpr int padding = 6, columnWidth = 140, columnGap = 16, headerHeight = 28;
        int rowHeight = 0;
        PatternGrid& owner;
        juce::OwnedArray<PresetButton> items;
        PresetPress press {};
        bool trackingPress = false, touch = false;
    };

    class PatternGrid::CyclePopup final : public juce::Component
    {
    public:
        explicit CyclePopup (PatternGrid& grid) : owner (grid), editor (grid.controller)
        {
            setTitle ("Step cycle editor");
            setAlwaysOnTop (true);
            addMouseListener (this, true);
            editor.inlineLayout = true;
            editor.previewCycleLayer = false;
            editor.locations = [this]
            {
                if (target && owner.canSetPreset (*target) && owner.layout.channelVisible (target->location.channel))
                    return std::vector<Location> { target->location };
                return std::vector<Location> {};
            };
            addAndMakeVisible (editor);
        }

        ~CyclePopup() override { removeMouseListener (this); }

        void open (PresetPress press, bool touch)
        {
            target = press;
            editor.compact = owner.layout.compact || !touch;
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
            if (owner.displayLayer() != Layer::chance || !owner.canSetPreset (*target) || !owner.layout.channelVisible (target->location.channel))
            {
                close();
                return;
            }
            const auto* parent = getParentComponent();
            const auto cell = parent->getLocalArea (&owner, owner.cellBounds (target->location)).toNearestInt();
            const auto available = parent->getLocalBounds();
            const int height = editor.compact ? 46 : 54;
            const int width = editor.preferredWidth (height);
            constexpr int gap = 10;
            const int below = cell.getBottom() + gap;
            const int above = cell.getY() - gap - height;
            const int y = below + height <= available.getBottom() || above < available.getY() ? below : above;
            setBounds (juce::Rectangle<int> (cell.getCentreX() - width / 2, y, width, height).constrainedWithin (available));
            editor.setBeatAccent (Palette::channel (target->location.channel, owner.controller.drumColors));
            editor.sync();
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Palette::shell);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
        }
        void resized() override { editor.setBounds (getLocalBounds()); }
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
        CycleEditor editor;
    };

    PatternGrid::~PatternGrid() = default;

    bool PatternGrid::hasActiveGesture() const
    {
        return drag != Drag::none || presetPress.has_value() || cyclePress.has_value() || mutePainting || pendingMute >= 0
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
        setDescription ("Select cells with Shift-click or a long press, drag to paint or change values; Option copies selected cells.");
        setTooltip ("Shift-click to select. Option-drag to copy. Drag edges for velocity lines. Right-click for presets.");
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
                        targetCell.channel = wrap (location.channel + (layout.reverseRows ? 1 : -1), layout.channels);
                    else if (key == juce::KeyPress::downKey)
                        targetCell.channel = wrap (location.channel + (layout.reverseRows ? -1 : 1), layout.channels);
                    else
                        return false;
                    accessibleCells[static_cast<size_t> (targetCell.key())].grabKeyboardFocus();
                    return true;
                };
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
            r.clear.getProperties().set ("rowClear", true);
            r.clear.setTooltip ("Double-click to clear row");
            r.clear.onClick = [this, ch, &r]
            {
                if (r.clearFlash.confirm())
                    controller.clearRow (ch);
            };
            r.note.setTitle ("MIDI note channel " + juce::String (ch + 1));
            r.note.format = [] (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, 4); };
            r.note.onChoose = [this, ch, &r] { if (noteChooserRequested) noteChooserRequested (ch, r.note); };
            r.reverse.onClick = [this, ch] { toggleDirection (ch, true); };
            r.ping.onClick = [this, ch] { toggleDirection (ch, false); };
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
        bool reverse = mode == PlaybackMode::reverse || mode == PlaybackMode::reverse_ping_pong;
        bool ping = mode == PlaybackMode::ping_pong || mode == PlaybackMode::reverse_ping_pong;
        if (flipReverse)
            reverse = !reverse;
        else
            ping = !ping;
        controller.command ("channel_playback_mode", { channel + 1, reverse ? (ping ? "reverse_ping_pong" : "reverse") : (ping ? "ping_pong" : "normal") }, "Playback direction");
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
        if (drag == Drag::loopStart || drag == Drag::loopEnd || (pointerInGrid && loopHandleAt (hoverPosition, false).has_value()))
            setMouseCursor (horizontalResizeCursor());
        else
            setMouseCursor (isCopyMode() ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::NormalCursor);
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
        return cell.enabled ? bounds.withX (hitPosition (location).x) : bounds;
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
        const double offset = static_cast<double> (cell.nudge) / 100.0 + part + displayedSwingDelay (location, part);
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
        if (point.x >= layout.gridLeft() && point.x < layout.gridLeft() + layout.gridWidth())
            for (int ch = 0; ch < layout.channels; ++ch)
                for (int step = layout.firstStep + layout.visibleSteps() - 1; step >= layout.firstStep; --step)
                {
                    const Location location { ch, step };
                    if (controller.cell (location).enabled && cellBounds (location).contains (point))
                        return location;
                }
        return layout.at (point);
    }
    Layer PatternGrid::displayLayer() const
    {
        if (drag != Drag::none && drag != Drag::marquee && drag != Drag::move && drag != Drag::copy)
            return dragLayer;
        return controller.inspectorHoverLayer.value_or (controller.chanceHeld || (controller.optionDown (modifiers) && controller.selected.none()) ? Layer::chance : controller.layer);
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
    std::optional<PatternGrid::VelocityEdge> PatternGrid::velocityEdgeAt (juce::Point<float> point, bool touch) const
    {
        const int channel = layout.channelAtY (point.y);
        if (layout.compact || channel < 0)
            return {};
        const auto row = layout.cell ({ channel, layout.firstStep });
        if (point.y < row.getY() || point.y >= row.getBottom())
            return {};
        const float radius = touch ? juce::jmin (touchVelocityEdgeHitRadius, layout.cellWidth() * 0.3f)
                                   : juce::jmin (velocityEdgeHitRadius, layout.cellWidth() * 0.25f);
        return nearestVelocityEdge (channel, point.x, true, radius, touch);
    }
    float PatternGrid::velocityEdgeX (VelocityEdge edge) const
    {
        const auto loop = controller.loop (edge.channel);
        const int loopEnd = loop.loopStart + loop.loopLength;
        const Location right { edge.channel, edge.step };
        const Location left { edge.channel, edge.step - 1 };
        if (right.step >= loop.loopStart && right.step < loopEnd && controller.cell (right).enabled)
            return cellBounds (right).getX();
        if (left.step >= loop.loopStart && left.step < loopEnd && controller.cell (left).enabled)
        {
            const float x = cellBounds (left).getRight();
            return edge.step == layout.steps ? juce::jmin (x, layout.gridLeft() + layout.gridWidth()) : x;
        }
        return layout.cell (right).getX();
    }
    float PatternGrid::velocityLineStartX (VelocityEdge edge, bool touch) const
    {
        const auto loop = controller.loop (edge.channel);
        const float x = velocityEdgeX (edge);
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
        // Later steps paint over earlier steps when two shifted edges coincide.
        for (int step = last; step >= first; --step)
        {
            if (start)
            {
                if (step < loop.loopStart || step > loopEnd)
                    continue;
                const bool rightActive = step < loopEnd && controller.cell ({ channel, step }).enabled;
                const bool leftActive = step > loop.loopStart && controller.cell ({ channel, step - 1 }).enabled;
                if (!leftActive && !rightActive)
                    continue;
            }
            const VelocityEdge edge { channel, step };
            const float edgeX = start ? velocityLineStartX (edge, touch) : velocityEdgeX (edge);
            if (edgeX < layout.gridLeft() || edgeX > gridRight)
                continue;
            const float delta = std::abs (x - edgeX);
            if (delta < distance)
            {
                nearest = edge;
                distance = delta;
            }
        }
        return nearest;
    }
    PatternGrid::VelocityEdge PatternGrid::velocityLineEnd() const
    {
        return nearestVelocityEdge (anchor.channel, current.x, false, std::numeric_limits<float>::infinity()).value_or (VelocityEdge { anchor.channel, boundary });
    }
    void PatternGrid::clearHoverMarker()
    {
        if (hoverEdge && drag == Drag::none)
            stopTimer();
        hoverEdge.reset();
        if (hoverMarkerVisible)
        {
            hoverMarkerVisible = false;
            repaint();
        }
    }
    void PatternGrid::mouseEnter (const juce::MouseEvent& e) { mouseMove (e); }
    void PatternGrid::mouseMove (const juce::MouseEvent& e)
    {
        if (!e.source.canHover())
            return;
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
            if (hoverMarkerVisible)
                repaint();
            return;
        }
        clearHoverMarker();
        hoverEdge = nextHoverEdge;
        if (hoverEdge)
            startTimer (75);
    }
    void PatternGrid::mouseExit (const juce::MouseEvent& e)
    {
        if (!e.source.canHover())
            return;
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
        if (presetMenu != nullptr)
            presetMenu->refresh();
        if (displayedSource != controller.source() || displayedEpoch != controller.state.documentEpoch)
        {
            lastHandleClick.reset();
            presetPress.reset();
            cyclePress.reset();
            displayedSource = controller.source();
            displayedEpoch = controller.state.documentEpoch;
            pendingMute = muteGestureRow = -1;
            mutePainting = false;
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
            r.label.setColour (juce::TextButton::textColourOffId, Palette::channel (ch, controller.drumColors));
            r.note.setVisible (visible && !layout.compact);
            r.more.setVisible (visible && layout.compact);
            r.mute.getProperties().set ("outlinedIcon", layout.phoneRowControls);
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
        for (int ch = 0; ch < 8; ++ch)
            for (int step = 0; step < 32; ++step)
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
                accessible.setVisible (layout.channelVisible (ch) && layout.stepVisible (step));
                accessible.setEnabled (controller.source() >= 0);
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
        for (int ch = 0; ch < 8; ++ch)
            for (int step = 0; step < 32; ++step)
                accessibleCells[static_cast<size_t> (ch * 32 + step)].setBounds (cellBounds ({ ch, step }).toNearestInt());
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
        const auto gridBackdrop = Palette::shell.interpolatedWith (Palette::app, 0.28f);
        const auto loopTint = Palette::accent.withAlpha (0.05f);
        g.fillAll (gridBackdrop);
        const auto layer = displayLayer();
        const float headingY = layout.cellsY() - 18.0f;
        if (layout.compact && overviewLayer (layer))
        {
            const auto name = layer == Layer::nudge ? "Nudge" : layer == Layer::roll ? "Roll" : "Cycle";
            const float y = layout.phoneRowControls ? headingY : layout.cellsY() - 32.0f;
            text (g, juce::String (name) + " / overview", { 16.0f, y, layout.gridLeft() - 24.0f, layout.phoneRowControls ? 16.0f : 22.0f }, 12, Palette::accent, true, juce::Justification::centredLeft);
        }
        const float horizontalInset = layout.cellInsetX();
        const auto referenceCell = layout.cell ({ 0, layout.firstStep }).reduced (horizontalInset, Layout::cellInsetY);
        const float valueSize = juce::jmin (18.0f, referenceCell.getWidth() * 0.38f, referenceCell.getHeight() * 0.5f);
        const bool hasSelection = controller.selected.any();
        const bool graphicalValues = controller.graphicalCellValues;
        if ((layer == Layer::chance && !graphicalValues) || layer == Layer::nudge)
            prepareProbabilityLabels (valueSize * 0.78f);
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
                for (int step = layout.firstStep; step < layout.firstStep + layout.visibleSteps(); ++step)
                {
                    if (step < range.loopStart || step >= range.loopStart + range.loopLength)
                        continue;
                    const Location pos { ch, step };
                    if (pass == 1 && !controller.cell (pos).enabled)
                        continue;
                    const auto cellArea = (pass == 0 ? layout.cell (pos) : cellBounds (pos)).expanded (3).getSmallestIntegerContainer();
                    if (!g.clipRegionIntersects (cellArea))
                        continue;
                    const auto& c = controller.cell (pos);
                    auto r = (pass == 0 ? layout.cell (pos) : cellBounds (pos)).reduced (horizontalInset, Layout::cellInsetY);
                    const juce::Graphics::ScopedSaveState cellClip (g);
                    g.reduceClipRegion (juce::Rectangle<float> (layout.gridLeft(), r.getY() - 2, layout.gridWidth(), r.getHeight() + 4).getSmallestIntegerContainer());
                    auto colour = Palette::channel (ch, controller.drumColors);
                    if (controller.source() >= 0 && controller.state.playback.sourceChannelMutes[static_cast<size_t> (controller.source())][static_cast<size_t> (ch)])
                        colour = colour.interpolatedWith (Palette::surface, 0.58f);
                    if (c.enabled && hasSelection && !controller.selected.test (static_cast<size_t> (pos.key())))
                        colour = colour.withMultipliedSaturation (0.65f).interpolatedWith (Palette::app, 0.6f);
                    const auto remaining = controller.sourceFlashes[static_cast<size_t> (ch)][static_cast<size_t> (step)] - now;
                    const auto pulse = static_cast<float> (juce::jlimit (0.0, 1.0, remaining / EditorController::sourceFlashDurationMs));
                    const auto valueColour = Palette::app;
                    const auto dark = colour.interpolatedWith (Palette::app, 0.48f);
                    const auto light = colour.brighter (0.1f);
                    const auto fillBottom = colour.interpolatedWith (Palette::app, 0.1f);
                    const auto paintCell = [&]
                    {
                        if (pass == 0)
                        {
                            const bool darkBeat = !c.enabled && step % 4 == 0;
                            surface (g, r, false, false, [gridBackdrop, darkBeat] (juce::Colour ink)
                            {
                                const auto base = ink.interpolatedWith (gridBackdrop, 0.35f);
                                return (darkBeat ? base.darker (0.30f) : base).withAlpha (ink.getFloatAlpha());
                            });
                            return;
                        }
                        if (!c.enabled)
                            return;
                        juce::ColourGradient fill (light, r.getTopLeft(), fillBottom, r.getBottomLeft(), false);
                        if (layer == Layer::velocity)
                        {
                            // The unlit share of the cell becomes a hard-edged leading band of the same gradient.
                            const double shade = 1.0 - c.velocity / 127.0;
                            const double edge = juce::jmin (1.0, shade + 0.5 / juce::jmax (1.0, static_cast<double> (r.getHeight())));
                            if (shade > 0.0)
                            {
                                fill.clearColours();
                                fill.addColour (0.0, dark);
                                fill.addColour (shade, dark);
                                fill.addColour (edge, light.interpolatedWith (fillBottom, static_cast<float> (edge)));
                                fill.addColour (1.0, fillBottom);
                            }
                        }
                        g.setGradientFill (fill);
                        g.fillRoundedRectangle (r, 5);
                        // Divide the painted body evenly; swing affects playback timing, not these count indicators.
                        if (c.roll > 1 && (!overviewLayer (layer) || layer == Layer::roll))
                        {
                            juce::Graphics::ScopedSaveState rollClip (g);
                            juce::Path clip;
                            clip.addRoundedRectangle (r, 5);
                            g.reduceClipRegion (clip);
                            for (int part = 0; part < c.roll; ++part)
                            {
                                const float x = r.getX() + r.getWidth() * static_cast<float> (part) / static_cast<float> (c.roll);
                                const float end = r.getX() + r.getWidth() * static_cast<float> (part + 1) / static_cast<float> (c.roll);
                                if (part % 2 == 1)
                                {
                                    g.setColour (Palette::app.withAlpha (0.14f));
                                    if (end > x)
                                        g.fillRect (x, r.getY(), end - x, r.getHeight());
                                }
                                if (part > 0)
                                {
                                    g.setColour (Palette::text.withAlpha (0.18f));
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
                            const auto strip = cycleStripBounds (r);
                            if (graphicalValues)
                            {
                                const auto track = r.reduced (4, 3).withBottom (strip.getY() - 3.0f);
                                const float level = static_cast<float> (juce::jlimit (0, 100, c.probability)) / 100.0f;
                                const float fillY = track.getBottom() - track.getHeight() * level;
                                g.setColour (juce::Colour (0xff101212));
                                g.fillRoundedRectangle (track, 2);
                                const juce::Graphics::ScopedSaveState faderClip (g);
                                juce::Path clip;
                                clip.addRoundedRectangle (track, 2);
                                g.reduceClipRegion (clip);
                                g.setColour (foreground);
                                if (level > 0.0f)
                                    g.fillRect (track.withTop (fillY));
                                g.setColour (Palette::text.withAlpha (0.3f + 0.7f * foregroundPulse));
                                const float handleY = juce::jlimit (track.getY(), track.getBottom() - 1.0f, fillY - 0.5f);
                                g.fillRect (track.getX(), handleY, track.getWidth(), 1.0f);
                                paintInsetEdge (g, track);
                            }
                            else
                            {
                                // Keep the percentage label above the cycle strip.
                                const auto& label = probabilityLabels[static_cast<size_t> (juce::jlimit (0, 100, c.probability))];
                                g.setColour (valueColour);
                                label.glyphs.draw (g, juce::AffineTransform::translation (r.getCentreX() - label.width * 0.5f,
                                                         r.getCentreY() - 3.0f + probabilityBaseline));
                            }
                            paintCycleStrip (g, strip, c.cycle, c.cycleMask, foreground);
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
                            text (g, juce::String (c.velocity), r.translated (0, 2), valueSize * 0.9f, valueColour, true);
                    };
                    paintCell();
                }
            }
            // Use the filled silhouette for a full outer halo, keeping the cell interior clear.
            selectionGlow.setColor (Palette::channel (ch, controller.drumColors).brighter (0.3f).withAlpha (0.92f), 0);
            for (int step = layout.firstStep; step < layout.firstStep + layout.visibleSteps(); ++step)
            {
                if (step < range.loopStart || step >= range.loopStart + range.loopLength)
                    continue;
                const Location pos { ch, step };
                if (!controller.selected.test (static_cast<size_t> (pos.key()))
                    || !g.clipRegionIntersects (cellPaintBounds (pos).getSmallestIntegerContainer()))
                    continue;
                const auto frame = cellBounds (pos).reduced (horizontalInset - 0.65f, 0.15f);
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
                g.setColour (Palette::text.withAlpha (0.9f));
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
        if (hoverMarkerVisible && hoverEdge && velocityHintAt (hoverPosition) == hoverEdge)
            paintMarker ({ velocityEdgeX (*hoverEdge), hoverPosition.y });
        if (drag == Drag::velocityLine && anchor.valid())
        {
            const auto row = layout.cell (anchor);
            const auto start = juce::Point<float> (velocityEdgeX ({ anchor.channel, boundary }),
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
            for (auto p : controller.selection())
            {
                Location d { wrap (target.channel + p.channel - anchor.channel, layout.channels), wrap (target.step + p.step - anchor.step, layout.steps) };
                const auto& cell = controller.cell (p);
                const auto offset = static_cast<float> (static_cast<double> (cell.nudge) / 100.0 + displayedSwingDelay (d));
                g.setColour (Palette::accent.withAlpha (0.3f));
                const auto destination = layout.cell (d).translated (offset * layout.cellWidth(), 0);
                const auto body = layout.compact ? destination.reduced (2) : destination.reduced (horizontalInset, Layout::cellInsetY);
                g.fillRoundedRectangle (body, 4);
            }
    }
    void PatternGrid::refreshAccent()
    {
        for (size_t ch = 0; ch < rows.size(); ++ch)
            rows[ch].label.setColour (juce::TextButton::textColourOffId, Palette::channel (static_cast<int> (ch), controller.drumColors));
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
        stopTimer();
        pendingVelocityEdge.reset();
        presetPress.reset();
        cyclePress.reset();
        const bool cyclePopupWasOpen = dismissCyclePopup (true);
        if (presetMenu != nullptr)
        {
            const bool dismissTouchMenu = presetMenu->isTouchMenu() && e.source.isTouch();
            presetMenu->close();
            if (dismissTouchMenu)
                return;
        }
        clearHoverMarker();
        const bool popup = e.mods.isPopupMenu();
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
            const PresetPress press { cellAt (e.position), e.position, controller.source(), controller.state.documentEpoch };
            if (canSetPreset (press) && !loopHandleAt (e.position, e.source.isTouch()))
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
        modifiers = e.mods;
        origin = current = e.position;
        anchor = target = cellAt (e.position);
        if (controller.chanceHeld && anchor.valid())
            controller.chanceUsed = true;
        dragLayer = displayLayer();
        initialSelection = controller.selected;
        additive = controller.useSelectionModifier (e.mods);
        const int row = layout.channelAtY (e.position.y);
        if (e.source.isTouch())
            pointerInGrid = false;
        const auto handle = loopHandleAt (e.position, e.source.isTouch());
        if (!layout.phoneRowControls && dragLayer == Layer::chance && !handle && anchor.valid() && !isCopyMode()
            && !(controller.keyboardModifiersEnabled && e.mods.isShiftDown()))
        {
            const auto body = cellBounds (anchor);
            const auto strip = cycleStripBounds (body.reduced (layout.cellInsetX(), Layout::cellInsetY));
            const PresetPress press { anchor, origin, controller.source(), controller.state.documentEpoch };
            if (body.contains (origin) && origin.y >= strip.getY() - 3.0f && canSetPreset (press))
            {
                lastHandleClick.reset();
                cyclePress = press;
                if (e.source.isTouch() && !additive)
                {
                    presetPress = press;
                    startTimer (touchPresetHoldMs);
                }
                return;
            }
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
        if (!layout.phoneRowControls && e.source.isTouch() && !additive && !isCopyMode())
        {
            const PresetPress press { anchor, origin, controller.source(), controller.state.documentEpoch };
            if (canSetPreset (press) && !(velocityEdges && velocityEdgeAt (origin, true)))
            {
                presetPress = press;
                startTimer (touchPresetHoldMs);
            }
        }
        if (velocityEdges && !e.source.isTouch() && (pendingVelocityEdge = velocityEdgeAt (e.position)))
        {
            // A click still toggles the cell; movement starts the line at the nearby edge.
            const auto loop = controller.loop (row);
            if (anchor.step < loop.loopStart || anchor.step >= loop.loopStart + loop.loopLength)
                anchor = {};
            selectedAtStart = false;
            drag = Drag::pending;
            return;
        }
        selectedAtStart = anchor.valid() && controller.selected.test (static_cast<size_t> (anchor.key()));
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
        if (!anchor.valid() || (additive && (!selectedAtStart || !controller.cell (anchor).enabled)))
        {
            drag = Drag::marquee;
            if (!anchor.valid() || controller.cell (anchor).enabled)
                updateSelection();
            return;
        }
        const auto loop = controller.loop (anchor.channel);
        if (anchor.step < loop.loopStart || anchor.step >= loop.loopStart + loop.loopLength)
            return;
        original = controller.cell (anchor);
        valueStep = anchor.step;
        valueVelocity = original.velocity;
        originals.clear();
        if (selectedAtStart)
            for (auto p : controller.selection())
                originals.emplace_back (p, controller.cell (p));
        else
            originals.emplace_back (anchor, original);
        drag = Drag::pending;
        if (e.mods.isLeftButtonDown() && !additive && !isCopyMode())
        {
            if (e.source.isTouch() && velocityEdges)
                pendingVelocityEdge = velocityEdgeAt (origin, true);
            startTimer (selectHoldMs);
        }
    }
    void PatternGrid::timerCallback()
    {
        stopTimer();
        if (presetPress && drag != Drag::pending)
        {
            const auto press = *std::exchange (presetPress, std::nullopt);
            cyclePress.reset();
            if (canSetPreset (press))
            {
                controller.selected = initialSelection;
                showCellPresets (press, true);
                controller.notify();
                repaint();
            }
            return;
        }
        if (drag == Drag::none && hoverEdge && velocityHintAt (hoverPosition) == hoverEdge)
        {
            hoverMarkerVisible = true;
            repaint();
            return;
        }
        if (drag != Drag::pending || !anchor.valid() || controller.source() < 0)
            return;
        if (const auto edge = std::exchange (pendingVelocityEdge, std::nullopt))
        {
            if (velocityEdgeAt (origin, true) == edge)
            {
                beginVelocityLine (*edge);
                return;
            }
        }
        // Use the same additive selection toggle as Shift-click, without editing the cell.
        additive = true;
        current = origin;
        drag = Drag::none;
        if (presetPress)
            startTimer (touchPresetHoldMs - selectHoldMs);
        updateSelection();
    }
    void PatternGrid::updateSelection()
    {
        controller.selected = additive ? initialSelection : Selection {};
        auto rectangle = juce::Rectangle<float> (origin, current);
        if (origin.getDistanceFrom (current) < 5 && anchor.valid())
        {
            if (controller.cell (anchor).enabled)
                controller.selected.flip (static_cast<size_t> (anchor.key()));
            else
                controller.selected.reset();
        }
        else
            for (int ch = 0; ch < layout.channels; ++ch)
                for (int s = 0; s < layout.steps; ++s)
                    if (rectangle.intersects (cellBounds ({ ch, s })))
                        controller.selected.set (static_cast<size_t> (ch * 32 + s));
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
        boundary = edge.step;
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
    void PatternGrid::mouseDrag (const juce::MouseEvent& e)
    {
        current = e.position;
        modifiers = e.mods;
        if (presetMenu != nullptr && presetMenu->isTrackingPress())
        {
            presetMenu->track (presetMenu->getLocalPoint (this, e.getPosition()));
            return;
        }
        if (presetPress && presetPress->position.getDistanceFrom (current) >= presetPressSlop)
            presetPress.reset();
        if (cyclePress && cyclePress->position.getDistanceFrom (current) >= presetPressSlop)
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
                else if (selectedAtStart && dragLayer == Layer::chance)
                    drag = Drag::value;
                else if (selectedAtStart)
                    drag = controller.useSelectionModifier (e.mods) ? Drag::value : Drag::move;
                else
                    drag = std::abs (current.x - origin.x) > std::abs (current.y - origin.y) ? Drag::paint : Drag::value;
                controller.begin ("Edit cells");
            }
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
            const float sensitivity = dragLayer == Layer::velocity ? 1.25f : 2.0f;
            const int offset = dragOffset (origin.y - current.y, sensitivity);
            if (dragLayer == Layer::velocity && !selectedAtStart)
            {
                const auto loop = controller.loop (anchor.channel);
                const int step = juce::jlimit (loop.loopStart, loop.loopStart + loop.loopLength - 1, target.step);
                const int velocity = juce::jlimit (1, 127, original.velocity + offset);
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
            const int endBoundary = velocityLineEnd().step;
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
        if (presetMenu != nullptr && presetMenu->isTrackingPress())
        {
            presetMenu->release (e.position);
            return;
        }
        presetPress.reset();
        if (const auto press = std::exchange (cyclePress, std::nullopt))
        {
            if (press->position.getDistanceFrom (e.position) < presetPressSlop && canSetPreset (*press))
            {
                if (cyclePopup == nullptr)
                    cyclePopup = std::make_unique<CyclePopup> (*this);
                cyclePopup->open (*press, e.source.isTouch());
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
        if (drag == Drag::pending && anchor.valid() && !(selectedAtStart && isCopyMode()))
        {
            if (selectedAtStart && additive)
            {
                controller.selected.reset (static_cast<size_t> (anchor.key()));
            }
            else
            {
                controller.begin (selectedAtStart ? "Fill or clear selection" : "Toggle cell");
                const auto cells = selectedAtStart ? controller.selection() : std::vector<Location> { anchor };
                const bool enable = std::any_of (cells.begin(), cells.end(), [this] (Location p) { return !controller.cell (p).enabled; });
                for (auto p : cells)
                {
                    auto c = controller.cell (p);
                    if (c.enabled == enable)
                        continue;
                    c.enabled = enable;
                    controller.editCell (p, c);
                }
            }
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
        const bool chanceWasHeld = std::exchange (controller.chanceHeld, false);
        stopTimer();
        presetPress.reset();
        cyclePress.reset();
        dismissCyclePopup();
        if (presetMenu != nullptr)
            presetMenu->close();
        clearHoverMarker();
        drag = Drag::none;
        mutePainting = false;
        pendingMute = muteGestureRow = -1;
        lastHandleClick.reset();
        updateCursor();
        controller.cancel();
        if (chanceWasHeld)
            controller.notify();
        repaint();
    }

    bool PatternGrid::canSetPreset (const PresetPress& press) const
    {
        const auto location = press.location;
        if (press.source != controller.source() || press.epoch != controller.processor.getUiBridge().getDocumentEpoch()
            || !location.valid() || location.channel >= layout.channels || !layout.stepVisible (location.step))
            return false;
        const auto loop = controller.loop (location.channel);
        return location.step >= loop.loopStart && location.step < loop.loopStart + loop.loopLength
            && controller.cell (location).enabled;
    }

    void PatternGrid::showCellPresets (PresetPress press, bool touch)
    {
        if (presetMenu == nullptr)
            presetMenu = std::make_unique<PresetMenu> (*this);
        presetMenu->open (press, touch);
    }

    void PatternGrid::applyCellPreset (const PresetPress& press, int result)
    {
        if (result <= 0 || result >= firstRollMenuId + static_cast<int> (rollPresets.size()))
            return;
        controller.refresh();
        if (!canSetPreset (press) || controller.editing())
            return;
        if (result < firstRollMenuId)
        {
            const auto& preset = cyclePresets[static_cast<size_t> (result - 1)];
            controller.begin ("Set cycle preset");
            controller.setCycle (preset.length, preset.mask, { press.location });
        }
        else
        {
            controller.begin ("Set roll preset");
            auto cell = controller.cell (press.location);
            cell.roll = rollPresets[static_cast<size_t> (result - firstRollMenuId)];
            controller.editCell (press.location, cell);
        }
        controller.commit();
    }
}
