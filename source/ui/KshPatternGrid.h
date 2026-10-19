#pragma once
#include "KshLayout.h"
#include "KshStyle.h"
namespace ksh::ui
{
    class PatternGrid : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        explicit PatternGrid (EditorController&);
        Layout layout;
        std::function<void (int)> channelSettings;
        std::function<void()> layerChanged;
        bool copySelection = false;
        void sync();
        void updatePlayback();
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void modifierKeysChanged (const juce::ModifierKeys&) override;
        void cancelGesture();
        Layer displayLayer() const;
        bool isCopyMode() const;
        bool isSelectMode() const { return controller.shiftDown (modifiers); }

    private:
        EditorController& controller;
        class AccessibleCell : public juce::Button
        {
        public:
            AccessibleCell() : juce::Button ("Cell") { setInterceptsMouseClicks (false, false); }
            std::function<bool (const juce::KeyPress&)> navigate;
            bool keyPressed (const juce::KeyPress& key) override { return (navigate && navigate (key)) || juce::Button::keyPressed (key); }
            void paintButton (juce::Graphics& g, bool, bool) override
            {
                if (hasKeyboardFocus (false))
                {
                    g.setColour (Palette::text);
                    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3), 4, 2);
                }
            }
        };
        std::array<AccessibleCell, 256> accessibleCells;
        struct Row
        {
            GestureButton clear { juce::String::fromUTF8 ("×") }, reverse { juce::String::fromUTF8 ("→") }, ping { juce::String::fromUTF8 ("↩") }, mute { juce::String::fromUTF8 ("⏻") }, left { juce::String::fromUTF8 ("◀") }, right { juce::String::fromUTF8 ("▶") };
            AuditionLabel label;
            ValueControl note;
            juce::TextButton more { "..." };
        };
        std::array<Row, 8> rows;
        enum class Drag { none,
            pending,
            paint,
            value,
            marquee,
            move,
            copy,
            loopStart,
            loopEnd,
            velocityLine };
        Drag drag = Drag::none;
        Location anchor, target;
        Location hoverCell;
        juce::Point<float> hoverPosition;
        bool hoverMarkerVisible = false;
        juce::Point<float> origin, current;
        Cell original;
        Layer dragLayer = Layer::velocity;
        std::vector<std::pair<Location, Cell>> originals;
        Selection initialSelection;
        bool selectedAtStart = false, additive = false;
        LoopRange initialLoop;
        juce::ModifierKeys modifiers;
        std::array<std::array<bool, 32>, 8> lastFlash {};
        bool mutePainting = false, muteValue = false;
        std::array<Cell, 256> displayedCells {};
        Selection displayedSelection;
        std::array<int, 8> displayedLoopStart {}, displayedLoopLength {};
        std::array<bool, 8> displayedMutes {};
        int displayedSteps = -1;
        int displayedFirstStep = -1;
        Layer displayedLayer = Layer::velocity;
        bool displayedColors = true, displayedNames = true;
        int previousStep = 0;
        std::optional<std::pair<Layout, juce::Rectangle<int>>> laidOut;
        int boundary = 0, lineVelocity = 100;
        int valueStep = 0, valueVelocity = 100;
        void updateSelection();
        int velocityAtY (float) const;
        Location velocityHintAt (juce::Point<float>) const;
        Location velocityEdgeAt (juce::Point<float>) const;
        void clearHoverMarker();
        void timerCallback() override;
    };
}
