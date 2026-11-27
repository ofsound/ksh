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
        std::function<void (int, juce::Component&)> trackColourRequested;
        std::function<void (int)> scrollRows;
        std::function<void (Location)> revealKeyboardCell;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool copySelection = false;
        juce::Component* openPresetMenu() const;
        void sync();
        juce::Rectangle<float> loopHandleBounds (int channel, bool end) const;
        void updatePlayback();
        void refreshAccent();
        void toggleDirection (int channel, bool flipReverse);
        NoteInput& rowNoteInput (int channel) { return rows[static_cast<size_t> (channel)].note; }
        juce::Component* rowTitle (int channel) { return juce::isPositiveAndBelow (channel, static_cast<int> (rows.size())) ? &rows[static_cast<size_t> (channel)].label : nullptr; }
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void visibilityChanged() override;
        void focusLost (FocusChangeType) override;
        bool keyPressed (const juce::KeyPress&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void modifierKeysChanged (const juce::ModifierKeys&) override;
        void cancelGesture();
        void finishVelocityDrawing();
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
        Location coveringSpan (Location) const;
        int displayedSpan (Location) const;
        juce::Rectangle<float> cellPaintBounds (Location) const;
        juce::Path makeControllerCurve (int channel) const;
        juce::Rectangle<float> emptyStepBounds (Location) const;
        int playbackPositionForCell (Location) const;
        double displayedSwingDelay (Location, double part = 0.0) const;
        Location keyboardCell;
        bool handleCellKey (Location, const juce::KeyPress&);
        void toggleCell (Location);
        void toggleCellsFromClick (Location, bool wholeSelection);
        class AccessibleCell : public juce::Button
        {
        public:
            AccessibleCell() : juce::Button ("Cell") { setInterceptsMouseClicks (false, false); }
            std::function<bool (const juce::KeyPress&)> navigate;
            bool keyPressed (const juce::KeyPress& key) override { return navigate && navigate (key); }
            bool keyStateChanged (bool) override { return false; }
            void paintButton (juce::Graphics& g, bool, bool) override
            {
                if (hasKeyboardFocus (false))
                {
                    g.setColour (Palette::text);
                    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3), 4, 2);
                }
            }
        };
        std::array<AccessibleCell, Constants::cellsPerPattern> accessibleCells;
        struct Row
        {
            juce::TextButton clear { juce::String::fromUTF8 ("×") };
            RowPlaybackButton reverse { juce::String::fromUTF8 ("→") }, ping { juce::String::fromUTF8 ("↩") }, mute { juce::String::fromUTF8 ("⏻") };
            RowTriggerButton label;
            ClearFlash clearFlash { clear };
            NoteInput note;
            juce::TextButton more { "..." };
        };
        std::array<Row, 8> rows;
        enum class RowControl { power, reverse, pingPong };
        struct RowSwipe
        {
            juce::MouseInputSource pointer;
            int channel, source;
            uint64_t epoch;
            RowControl control;
            bool extendSolo;
            bool clickPending = true;
            int endRow = -1;
            std::array<bool, Constants::maxChannels> initialValues {};
        };
        std::optional<RowSwipe> rowSwipe;
        static constexpr int soloHoldMs = 450;
        uint64_t soloPress = 0;
        void beginRowSwipe (int channel, RowControl, const juce::MouseEvent&);
        void updateRowSwipe (const juce::MouseEvent&);
        void endRowSwipe (const juce::MouseEvent&, const RowPlaybackButton&);
        bool rowSwipeMatches (int channel, RowControl, const juce::MouseEvent&) const;
        bool rowSwipeCurrent (const RowSwipe&) const;
        void holdRowSolo();
        bool rowControlValue (const RowSwipe&, int channel) const;
        void setDirection (int channel, bool reverse, bool enabled);
        int displayedSource = -2;
        uint64_t displayedEpoch = 0;
        std::optional<EditorController::SplitCycleSelection> displayedSplitCycle;
        enum class Drag { none,
            pending,
            paint,
            value,
            marquee,
            move,
            copy,
            loopStart,
            loopEnd,
            velocityLine,
            velocityDraw };
        Drag drag = Drag::none;
        std::optional<juce::MouseInputSource> gesturePointer;
        Location anchor, target;
        Location touchStepPreview;
        void clearTouchStepPreview();
        void cancelTouchStepPress();
        struct VelocityEdge
        {
            int channel;
            int step;
            int split = 0;
            bool operator== (const VelocityEdge&) const = default;
        };
        std::optional<VelocityEdge> hoverEdge;
        std::optional<VelocityEdge> pendingVelocityEdge;
        struct VelocityTap
        {
            VelocityEdge edge;
            Location cell;
            juce::Point<float> position;
            juce::Time time;
            bool touch;
        };
        std::optional<VelocityTap> velocityTap;
        juce::TimedCallback velocityTapTimer { [this] { finishVelocityTap(); } };
        std::optional<juce::Point<float>> velocityDrawPoint;
        bool velocityDrawTouchTap = false;
        struct PresetPress
        {
            Location location;
            juce::Point<float> position;
            int source;
            uint64_t epoch;
            bool allowEmpty = false;
        };
        std::optional<PresetPress> presetPress;
        double presetHoldDeadline = 0.0;
        juce::TimedCallback presetHoldTimer { [this] { openHeldPreset(); } };
        void cancelPresetHold();
        void openHeldPreset();
        std::optional<PresetPress> cyclePress;
        struct SplitPress
        {
            PresetPress target;
            int index, value;
            bool scrubbing = false;
        };
        std::optional<SplitPress> splitPress;
        bool beginSplitPress (const PresetPress&, int selectionDelay);
        void setSplitProbability (Location, int, int);
        void dragSplitValue (const juce::MouseEvent&);
        bool canClickCycleStrip (const juce::ModifierKeys&) const;
        bool cycleStripContains (Location, juce::Point<float>) const;
        int splitIndexAt (Location, float x) const;
        class PresetMenu;
        std::unique_ptr<PresetMenu> presetMenu;
        bool canSetPreset (const PresetPress&) const;
        void showCellPresets (PresetPress, bool touch);
        juce::Point<float> hoverPosition;
        double hoverMarkerStarted = 0.0;
        juce::TimedCallback hoverMarkerTimer { [this]
        {
            if (hoverEdge)
                repaint (juce::Rectangle<float> (34.0f, 34.0f).withCentre (hoverMarkerPoint()).getSmallestIntegerContainer());
        } };
        bool pointerInGrid = false;
        juce::Point<float> origin, current;
        double velocityAnchorPulseStarted = 0.0;
        juce::TimedCallback velocityAnchorPulseTimer { [this] { updateVelocityAnchorPulse(); } };
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
        std::array<std::array<bool, Constants::maxSteps>, Constants::maxChannels> lastFlash {};
        std::array<Cell, Constants::cellsPerPattern> displayedCells {};
        std::array<int, Constants::maxChannels> displayedCcModes {};
        struct ControllerCurve
        {
            juce::Path path;
            LoopRange loop;
            int mode = 0;
        };
        std::array<ControllerCurve, Constants::maxChannels> controllerCurves;
        double controllerCurveRate = 0.0;
        std::array<juce::Rectangle<float>, Constants::cellsPerPattern> displayedPaintBounds {};
        std::pair<int, int> displayedSwing { -1, -1 };
        Selection displayedSelection;
        std::array<int, 8> displayedLoopStart {}, displayedLoopLength {};
        std::array<std::pair<float, float>, 8> displayedLoopEdges {};
        std::array<bool, 8> displayedMutes {};
        int displayedSteps = -1;
        int displayedBeatStride = 4;
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
        VelocityEdge lineStart { 0, 0 };
        int lineVelocity = 100;
        int valueStep = 0, valueVelocity = 100;
        float valueX = 0;
        void updateSelection();
        int velocityAtY (float) const;
        std::optional<bool> loopHandleAt (juce::Point<float>) const;
        void updateCursor();
        std::optional<VelocityEdge> velocityHintAt (juce::Point<float>) const;
        juce::Point<float> hoverMarkerPoint() const;
        std::optional<VelocityEdge> velocityEdgeAt (juce::Point<float>, bool touch = false) const;
        float velocityEdgeX (VelocityEdge) const;
        bool isVelocityGutter (VelocityEdge, float x) const;
        std::optional<VelocityEdge> nearestVelocityEdge (int channel, float x, bool start, float distance, bool touch = false) const;
        VelocityEdge velocityLineEnd() const;
        void beginVelocityLine (VelocityEdge);
        void updateVelocityAnchorPulse (bool finished = false);
        void finishVelocityTap();
        void beginVelocityDrawing (VelocityEdge);
        juce::Rectangle<float> velocityDrawingBounds() const;
        void drawVelocity (juce::Point<float>);
        void dragVelocityValues (float x, int step, int velocity);
        void clearHoverMarker();
        void prepareProbabilityLabels (float size);
        void timerCallback() override;
    };
}
