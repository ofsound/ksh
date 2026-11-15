#pragma once
#include "KshLayout.h"
#include "KshStyle.h"
#include "KshNoteChooser.h"
#include <melatonin_blur/melatonin_blur.h>
namespace ksh::ui
{
    class PatternGrid : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        explicit PatternGrid (EditorController&);
        ~PatternGrid() override;
        Layout layout;
        std::function<void (int)> channelSettings;
        std::function<void (int)> rowTriggered;
        std::function<void()> layerChanged;
        std::function<void (int, juce::Component&)> noteChooserRequested;
        std::function<void (int)> scrollRows;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool copySelection = false;
        void sync();
        juce::Rectangle<float> loopHandleBounds (int channel, bool end, bool touch) const;
        void updatePlayback();
        void refreshAccent();
        void toggleDirection (int channel, bool flipReverse);
        NoteInput& rowNoteInput (int channel) { return rows[static_cast<size_t> (channel)].note; }
        juce::Component* rowTitle (int channel) { return juce::isPositiveAndBelow (channel, static_cast<int> (rows.size())) ? &rows[static_cast<size_t> (channel)].label : nullptr; }
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void modifierKeysChanged (const juce::ModifierKeys&) override;
        void cancelGesture();
        bool dismissCyclePopup (bool commitPendingClick = false);
        bool handleCellPopupMouseDown (const juce::Component* clicked);
        bool hasActiveGesture() const;
        Layer displayLayer() const;
        juce::Rectangle<float> cellBounds (Location) const;
        juce::Point<float> hitPosition (Location, int rollIndex = 0) const;
        bool isCopyMode() const;
        bool isSelectMode() const { return controller.shiftDown (modifiers); }

    private:
        EditorController& controller;
        static constexpr float selectionGlowExtent = 20.0f;
        // Share the cached, position-independent blur across equal-sized cells.
        melatonin::DropShadow selectionGlow {
            { Palette::text.withAlpha (0.92f), 18 },
            { Palette::text.withAlpha (0.55f), 5 }
        };
        Location cellAt (juce::Point<float>) const;
        juce::Rectangle<float> cellPaintBounds (Location) const;
        int playbackPositionForCell (Location) const;
        double displayedSwingDelay (Location, double part = 0.0) const;
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
            juce::TextButton clear { juce::String::fromUTF8 ("×") }, reverse { juce::String::fromUTF8 ("→") }, ping { juce::String::fromUTF8 ("↩") };
            GestureButton mute { juce::String::fromUTF8 ("⏻") };
            RowTriggerButton label;
            ClearFlash clearFlash { clear };
            NoteInput note;
            juce::TextButton more { "..." };
        };
        std::array<Row, 8> rows;
        static constexpr int soloHoldMs = 450;
        int pendingMute = -1, muteGestureRow = -1, gestureSource = -1;
        uint64_t mutePress = 0, gestureEpoch = 0;
        void holdMute (int channel);
        void paintMute (int channel);
        bool muteGestureCurrent() const;
        int displayedSource = -2;
        uint64_t displayedEpoch = 0;
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
        struct VelocityEdge
        {
            int channel;
            int step;
            bool operator== (const VelocityEdge&) const = default;
        };
        std::optional<VelocityEdge> hoverEdge;
        std::optional<VelocityEdge> pendingVelocityEdge;
        struct PresetPress
        {
            Location location;
            juce::Point<float> position;
            int source;
            uint64_t epoch;
        };
        std::optional<PresetPress> presetPress;
        std::optional<PresetPress> cyclePress;
        class PresetMenu;
        std::unique_ptr<PresetMenu> presetMenu;
        class CyclePopup;
        std::unique_ptr<CyclePopup> cyclePopup;
        bool canSetPreset (const PresetPress&) const;
        void showCellPresets (PresetPress, bool touch);
        void applyCellPreset (const PresetPress&, int result);
        juce::Point<float> hoverPosition;
        bool hoverMarkerVisible = false, pointerInGrid = false;
        juce::Point<float> origin, current;
        Cell original;
        Layer dragLayer = Layer::velocity;
        std::vector<std::pair<Location, Cell>> originals;
        Selection initialSelection;
        bool selectedAtStart = false, additive = false;
        LoopRange initialLoop;
        struct HandleClick
        {
            int channel;
            Drag side;
            juce::Time time;
        };
        std::optional<HandleClick> lastHandleClick;
        juce::ModifierKeys modifiers;
        std::array<std::array<bool, 32>, 8> lastFlash {};
        bool mutePainting = false, muteValue = false;
        std::array<Cell, 256> displayedCells {};
        std::array<juce::Rectangle<float>, 256> displayedPaintBounds {};
        std::pair<int, int> displayedSwing { -1, -1 };
        Selection displayedSelection;
        std::array<int, 8> displayedLoopStart {}, displayedLoopLength {};
        std::array<bool, 8> displayedMutes {};
        int displayedSteps = -1;
        int displayedFirstStep = -1;
        Layer displayedLayer = Layer::velocity;
        bool displayedColors = true, displayedNames = true, displayedGraphicalValues = false;
        int previousStep = 0;
        struct ProbabilityLabel
        {
            juce::GlyphArrangement glyphs;
            float width = 0.0f;
        };
        std::array<ProbabilityLabel, 101> probabilityLabels;
        std::array<ProbabilityLabel, 201> nudgeLabels;
        float probabilityFontSize = 0.0f, probabilityBaseline = 0.0f;
        std::optional<std::pair<Layout, juce::Rectangle<int>>> laidOut;
        int boundary = 0, lineVelocity = 100;
        int valueStep = 0, valueVelocity = 100;
        void updateSelection();
        int velocityAtY (float) const;
        std::optional<bool> loopHandleAt (juce::Point<float>, bool touch) const;
        void updateCursor();
        std::optional<VelocityEdge> velocityHintAt (juce::Point<float>) const;
        std::optional<VelocityEdge> velocityEdgeAt (juce::Point<float>, bool touch = false) const;
        float velocityEdgeX (VelocityEdge) const;
        float velocityLineStartX (VelocityEdge, bool touch = false) const;
        std::optional<VelocityEdge> nearestVelocityEdge (int channel, float x, bool start, float distance, bool touch = false) const;
        VelocityEdge velocityLineEnd() const;
        void beginVelocityLine (VelocityEdge);
        void clearHoverMarker();
        void prepareProbabilityLabels (float size);
        void timerCallback() override;
    };
}
