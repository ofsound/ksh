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
            close.getProperties().set ("icon", "clear");
            close.onClick = [this] { dismiss(); };
            duplicate.setTitle ("Duplicate pattern");
            duplicate.onClick = [this]
            {
                if (copySource < 0)
                {
                    copySource = controller.source();
                    destination = -1;
                    sync();
                }
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
        void open()
        {
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
                tile.setToggleState (copySource >= 0 ? destination == i : tile.current, juce::dontSendNotification);
                tile.setEnabled (copySource != i);
                tile.repaint();
            }
            duplicate.setEnabled (copySource < 0 ? controller.source() >= 0 : destination >= 0 && destination != copySource);
            duplicate.setButtonText (copySource < 0 ? controller.source() < 0 ? juce::String ("Duplicate pattern") : "Duplicate P" + juce::String (controller.source() + 1)
                                                   : destination >= 0 ? "Copy to P" + juce::String (destination + 1) : "Choose destination");
            mute.setButtonText (copySource >= 0 ? "Cancel copy" : "Mute patterns");
            mute.setToggleState (copySource < 0 && controller.source() < 0, juce::dontSendNotification);
            repaint();
        }
        void resized() override
        {
            card = { 6, 4, juce::jmin (428, getWidth() - 12), 316 };
            close.setBounds (card.getRight() - 50, card.getY() + 6, 44, 40);
            const int stride = (card.getWidth() - 24) / 4;
            for (int i = 0; i < 16; ++i)
                tiles[static_cast<size_t> (i)].setBounds (card.getX() + 12 + (i % 4) * stride, card.getY() + 56 + (i / 4) * 49, stride - 6, 44);
            mute.setBounds (card.getX() + 12, card.getBottom() - 56, 142, 44);
            duplicate.setBounds (mute.getRight() + 8, mute.getY(), card.getRight() - mute.getRight() - 20, 44);
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (Palette::app.withAlpha (0.68f));
            g.setColour (Palette::shell);
            g.fillRoundedRectangle (card.toFloat(), 10);
            g.setColour (Palette::border);
            g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10, 1);
            text (g, copySource >= 0 ? "Duplicate P" + juce::String (copySource + 1) : "Patterns", card.reduced (14, 8).withHeight (22).toFloat(), 16, Palette::text, true, juce::Justification::centredLeft);
            const auto hint = copySource >= 0 ? destination >= 0 ? "Destination contents will be replaced. Undo is available." : "Choose a destination, then confirm the copy."
                                             : "Tap a pattern to return to the grid.";
            text (g, hint, { static_cast<float> (card.getX() + 14), static_cast<float> (card.getY() + 32), static_cast<float> (card.getWidth() - 28), 18 }, 10, Palette::muted, false, juce::Justification::centredLeft);
        }
        void mouseDown (const juce::MouseEvent& event) override
        {
            if (!card.contains (event.getPosition()))
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
        class Tile : public juce::TextButton
        {
        public:
            int hits = 0;
            bool current = false;
            void paintButton (juce::Graphics& g, bool hover, bool down) override
            {
                footerSurface (g, getLocalBounds().toFloat().reduced (0.5f), getToggleState(), hover, down, isEnabled());
                ksh::ui::text (g, getButtonText().paddedLeft ('0', 2), { 8, 4, 30, 32 }, 19, isEnabled() ? Palette::text : Palette::muted, true);
                ksh::ui::text (g, hits == 0 ? "Empty" : juce::String (hits) + " hits", { 40, 10, static_cast<float> (getWidth() - 44), 24 }, 10, Palette::muted);
                if (current)
                {
                    g.setColour (Palette::accent);
                    g.fillEllipse (9, 36, 4, 4);
                }
            }
        };
        EditorController& controller;
        std::array<Tile, 16> tiles;
        juce::TextButton close, duplicate, mute;
        juce::Rectangle<int> card;
        int copySource = -1, destination = -1;
        uint64_t epoch = 0;
    };
}
