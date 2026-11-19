#pragma once
#include "KshEditorController.h"
#include "KshStyle.h"

namespace ksh::ui
{
    /** Phone-only pattern navigation. Cell selection never changes a pattern tap into a copy. */
    class CompactPatternPicker : public juce::Component
    {
    public:
        explicit CompactPatternPicker (EditorController& c) : controller (c)
        {
            setTitle ("Pattern picker");
            setWantsKeyboardFocus (true);
            setFocusContainerType (FocusContainerType::keyboardFocusContainer);
            for (int i = 0; i < 16; ++i)
            {
                auto& tile = tiles[static_cast<size_t> (i)];
                addAndMakeVisible (tile);
                tile.setButtonText (juce::String (i + 1));
                tile.setTitle ("Choose pattern " + juce::String (i + 1));
                tile.onHold = [this, i]
                {
                    if (isVisible() && fullView && copySource < 0
                        && epoch == controller.processor.getUiBridge().getDocumentEpoch())
                        beginDuplicate (i);
                };
                tile.onClick = [this, i]
                {
                    if (copySource >= 0)
                    {
                        destination = i;
                        sync();
                    }
                    else
                    {
                        controller.copySource = -1;
                        controller.selectSource (i);
                        dismiss();
                    }
                };
            }
            for (auto* button : { &close, &duplicate, &mute })
                addAndMakeVisible (*button);
            close.setTitle ("Close pattern picker");
            close.getProperties().set ("icon", "close");
            close.onClick = [this] { dismiss(); };
            duplicate.setTitle ("Duplicate pattern");
            duplicate.onClick = [this]
            {
                if (copySource < 0)
                    beginDuplicate (controller.source());
                else if (destination >= 0 && destination != copySource)
                {
                    // Capture the source explicitly; the destination tap only previews this operation.
                    controller.selectSource (copySource, true);
                    controller.selectSource (destination);
                    dismiss();
                }
            };
            mute.setTitle ("Mute patterns");
            mute.onClick = [this]
            {
                if (copySource >= 0)
                {
                    copySource = destination = -1;
                    sync();
                    return;
                }
                controller.copySource = -1;
                controller.selectSource (-1);
                dismiss();
            };
        }
        std::function<void()> onDismiss;
        void open (bool useFullView)
        {
            fullView = useFullView;
            close.setVisible (!fullView);
            duplicate.setVisible (!fullView);
            mute.setTitle (fullView ? "Muted Pattern" : "Mute patterns");
            setOpaque (fullView);
            resized();
            copySource = destination = -1;
            epoch = controller.processor.getUiBridge().getDocumentEpoch();
            controller.copySource = -1;
            setVisible (true);
            sync();
            toFront (false);
            if (isShowing())
                grabKeyboardFocus();
        }
        void dismiss()
        {
            for (auto& tile : tiles)
                tile.cancelHold();
            copySource = destination = -1;
            controller.copySource = -1;
            setVisible (false);
            if (onDismiss)
                onDismiss();
        }
        void sync()
        {
            if (epoch != controller.processor.getUiBridge().getDocumentEpoch())
            {
                dismiss();
                return;
            }
            const auto& state = controller.state.playback;
            for (int i = 0; i < 16; ++i)
            {
                auto& tile = tiles[static_cast<size_t> (i)];
                int hits = 0;
                const int steps = state.sourceSettings[static_cast<size_t> (i)].stepCount;
                for (int ch = 0; ch < state.channelCount; ++ch)
                    for (int step = 0; step < steps; ++step)
                        hits += state.sources[static_cast<size_t> (i)][static_cast<size_t> (ch)][static_cast<size_t> (step)].enabled ? 1 : 0;
                tile.hits = hits;
                tile.current = controller.source() == i;
                tile.copyDestination = copySource >= 0 && copySource != i;
                tile.setTooltip (fullView && copySource < 0 ? "Hold to duplicate this pattern" : "");
                tile.setToggleState (copySource >= 0 ? destination == i : tile.current, juce::dontSendNotification);
                tile.setEnabled (copySource != i);
                tile.repaint();
            }
            duplicate.setVisible (!fullView || copySource >= 0);
            duplicate.setEnabled (copySource < 0 ? controller.source() >= 0 : destination >= 0 && destination != copySource);
            duplicate.setButtonText (copySource < 0 ? controller.source() < 0 ? juce::String ("Duplicate pattern") : "Duplicate P" + juce::String (controller.source() + 1)
                                                   : destination >= 0 ? "Copy to P" + juce::String (destination + 1) : "Choose destination");
            mute.setButtonText (copySource >= 0 ? "Cancel copy" : fullView ? "Muted Pattern" : "Mute patterns");
            mute.setToggleState (copySource < 0 && controller.source() < 0, juce::dontSendNotification);
            repaint();
        }
        void resized() override
        {
            if (!fullView)
            {
                const auto card = overlayBounds();
                close.setBounds (card.getRight() - 50, card.getY() + 6, 44, 40);
                const int stride = (card.getWidth() - 24) / 4;
                for (int i = 0; i < 16; ++i)
                {
                    auto& tile = tiles[static_cast<size_t> (i)];
                    tile.fullView = false;
                    tile.setBounds (card.getX() + 12 + (i % 4) * stride, card.getY() + 56 + (i / 4) * 49, stride - 6, 44);
                }
                mute.setBounds (card.getX() + 12, card.getBottom() - 56, 142, 44);
                duplicate.setBounds (mute.getRight() + 8, mute.getY(), card.getRight() - mute.getRight() - 20, 44);
                return;
            }
            constexpr int columns = 8, rows = 2, gap = 8;
            auto content = getLocalBounds().reduced (12, 8);
            content.removeFromTop (44);
            auto actions = content.removeFromBottom (44);
            mute.setBounds (actions.removeFromLeft (actions.getWidth() / 3));
            duplicate.setBounds (actions.withTrimmedLeft (gap).removeFromRight (mute.getWidth()));
            content.reduce (0, gap);
            const int width = content.getWidth() - (columns - 1) * gap;
            const int height = content.getHeight() - (rows - 1) * gap;
            for (int i = 0; i < 16; ++i)
            {
                const int column = i % columns, row = i / columns;
                auto& tile = tiles[static_cast<size_t> (i)];
                tile.fullView = true;
                tile.setBounds (content.getX() + column * width / columns + column * gap,
                                content.getY() + row * height / rows + row * gap,
                                (column + 1) * width / columns - column * width / columns,
                                (row + 1) * height / rows - row * height / rows);
            }
        }
        void paint (juce::Graphics& g) override
        {
            const auto title = copySource >= 0 ? "Duplicate P" + juce::String (copySource + 1) : "Patterns";
            const auto hint = copySource >= 0 ? destination >= 0 ? "Destination contents will be replaced. Undo is available." : "Choose a destination, then confirm the copy."
                                             : "Tap a pattern to return to the grid.";
            if (!fullView)
            {
                const auto card = overlayBounds().toFloat();
                g.fillAll (Palette::app.withAlpha (0.68f));
                g.setColour (Palette::shell);
                g.fillRoundedRectangle (card, 10);
                g.setColour (Palette::border);
                g.drawRoundedRectangle (card.reduced (0.5f), 10, 1);
                text (g, title, card.reduced (14, 8).withHeight (22), 16, Palette::text, true, juce::Justification::centredLeft);
                text (g, hint, card.reduced (14, 0).withTrimmedTop (32).withHeight (18), 10, Palette::muted, false, juce::Justification::centredLeft);
                return;
            }
            g.fillAll (Palette::shell);
            const auto heading = juce::Rectangle<float> (14, 8, static_cast<float> (getWidth() - 72), 44);
            text (g, title, heading.withHeight (22), 18, Palette::text, true, juce::Justification::centredLeft);
            text (g, copySource >= 0 ? hint : "Hold a pattern to duplicate it.",
                  heading.withTrimmedTop (22), 12, Palette::muted, false, juce::Justification::centredLeft);
        }
        void mouseDown (const juce::MouseEvent& event) override
        {
            if (!fullView && !overlayBounds().contains (event.getPosition()))
                dismiss();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key != juce::KeyPress::escapeKey)
                return false;
            dismiss();
            return true;
        }

    private:
        void beginDuplicate (int source)
        {
            if (source < 0)
                return;
            copySource = source;
            destination = -1;
            for (auto& tile : tiles)
                tile.cancelHold();
            sync();
        }

        juce::Rectangle<int> overlayBounds() const { return { 6, 4, juce::jmin (428, getWidth() - 12), 316 }; }

        class Tile : public juce::TextButton, private juce::Timer
        {
        public:
            std::function<void()> onHold;
            int hits = 0;
            bool current = false, fullView = false, copyDestination = false;
            void cancelHold() { stopTimer(); }
            void mouseDown (const juce::MouseEvent& event) override
            {
                held = false;
                juce::TextButton::mouseDown (event);
                if (fullView && !copyDestination && isEnabled() && event.mods.isLeftButtonDown() && !event.mods.isPopupMenu())
                    startTimer (holdMs);
            }
            void mouseDrag (const juce::MouseEvent& event) override
            {
                if (event.getDistanceFromDragStart() >= 10 || !getLocalBounds().contains (event.getPosition()))
                    cancelHold();
                if (!held)
                    juce::TextButton::mouseDrag (event);
            }
            void mouseUp (const juce::MouseEvent& event) override
            {
                const bool finishedHold = isTimerRunning() && event.getLengthOfMousePress() >= holdMs
                    && getLocalBounds().contains (event.getPosition());
                cancelHold();
                if (finishedHold)
                    activateHold();
                if (!std::exchange (held, false))
                    juce::TextButton::mouseUp (event);
            }
            void paintButton (juce::Graphics& g, bool hover, bool down) override
            {
                const auto bounds = getLocalBounds().toFloat();
                footerSurface (g, bounds.reduced (0.5f), getToggleState(), hover, down, isEnabled());
                if (copyDestination && hover && isEnabled())
                    paintPatternCopyOutline (g, bounds);
                const auto number = fullView ? getButtonText() : getButtonText().paddedLeft ('0', 2);
                const auto status = hits == 0 ? "Empty" : juce::String (hits) + " hits";
                const auto ink = isEnabled() ? Palette::text : Palette::muted;
                if (fullView)
                {
                    const auto label = bounds.withSizeKeepingCentre (bounds.getWidth(), 48);
                    ksh::ui::text (g, number, label.withHeight (32), 28, ink, true);
                    ksh::ui::text (g, status, label.withTrimmedTop (32), 12, Palette::muted);
                }
                else
                {
                    ksh::ui::text (g, number, { 8, 4, 30, 32 }, 19, ink, true);
                    ksh::ui::text (g, status, { 40, 10, bounds.getWidth() - 44, 24 }, 10, Palette::muted);
                }
                if (current && !fullView)
                {
                    g.setColour (Palette::accent);
                    g.fillEllipse (9, 36, 4, 4);
                }
            }

        private:
            void timerCallback() override
            {
                cancelHold();
                if (isShowing())
                    activateHold();
            }
            void activateHold()
            {
                if (!isEnabled())
                    return;
                held = true;
                setState (buttonNormal);
                if (onHold)
                    onHold();
            }
            static constexpr int holdMs = 550;
            bool held = false;
        };
        EditorController& controller;
        std::array<Tile, 16> tiles;
        juce::TextButton close, duplicate, mute;
        bool fullView = false;
        int copySource = -1, destination = -1;
        uint64_t epoch = 0;
    };
}
