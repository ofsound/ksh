#pragma once
#include "KshCycleEditor.h"
#include "KshPatternGenerator.h"
namespace ksh::ui
{
    class AccentWordmark : public juce::Component
    {
    public:
        explicit AccentWordmark (bool showRandomizer = false, float artworkScale = 1.0f);
        std::function<void (bool)> onAccentSelected;
        std::function<void (bool)> onThemeGesture;
        std::function<void (bool, std::optional<juce::Colour>)> onAccentColourEdited;
        std::function<void (Palette::BackgroundTone)> onBackgroundToneEdited;
        std::function<void()> onRandomizerCycle;
        void paint (juce::Graphics&) override;
        void resized() override;
        void setArtworkBounds (juce::Rectangle<float>);
        static void paintBranding (juce::Graphics&, juce::Rectangle<float>, float artworkScale = 1.0f);

    private:
        class LogoButton : public GestureButton
        {
        public:
            std::function<void()> onDoubleClick;
            void mouseDoubleClick (const juce::MouseEvent& event) override
            {
                if (isEnabled() && !event.mods.isPopupMenu() && !event.mouseWasDraggedSinceMouseDown() && onDoubleClick)
                    onDoubleClick();
            }
            void paintButton (juce::Graphics&, bool, bool) override {}
        };
        class AccentLogoButton : public LogoButton
        {
        public:
            explicit AccentLogoButton (bool blue) : blueAccent (blue) { setTriggeredOnMouseDown (true); }
            std::function<void (bool)> onThemeGesture;
            std::function<void (std::optional<juce::Colour>)> onColourEdited;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseDrag (const juce::MouseEvent&) override;
            void mouseUp (const juce::MouseEvent&) override;
            void mouseDoubleClick (const juce::MouseEvent&) override;

        private:
            const bool blueAccent;
            std::optional<juce::Colour> startingColour;
            bool draggingAccent = false;
        };
        const float artworkScale;
        std::optional<juce::Rectangle<float>> artworkBounds;
        LogoButton backdrop;
        std::optional<Palette::BackgroundTone> startingTone;
        bool draggingBackdrop = false;
        AccentLogoButton orangeLogo { false }, blueLogo { true };
        LogoButton randomizerLogo;
    };

    class PreviewPanel : public juce::Component
    {
    public:
        explicit PreviewPanel (EditorController&, bool randomSection = false);
        ~PreviewPanel() override;
        std::function<void()> onRandomizerCycle;
        void setXYRandomizer (bool);
        bool isXYRandomizer() const { return xyRandomizer; }
        std::function<void()> onClose;
        std::function<void()> onRhythm;
        std::function<void (bool)> onAccentSelected;
        std::function<void (bool)> onThemeGesture;
        std::function<void (bool, std::optional<juce::Colour>)> onAccentColourEdited;
        std::function<void (Palette::BackgroundTone)> onBackgroundToneEdited;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void visibilityChanged() override;
        void sync();
        void updatePlayback();
        void paint (juce::Graphics&) override;
        void resized() override;
        void setInspectorBounds (juce::Rectangle<int>);
        void setDesktopRandomizerSize (int width, int height, int rightInset);
        void setCompact (bool, bool phone = false);
        void setDisplayedLayer (Layer);
        void cancelGesture();
        bool reverseRows = false;

    private:
        struct ChildHoverListener : juce::MouseListener
        {
            explicit ChildHoverListener (PreviewPanel& p) : owner (p) {}
            void mouseEnter (const juce::MouseEvent& event) override { owner.mouseEnter (event); }
            void mouseMove (const juce::MouseEvent& event) override { owner.mouseMove (event); }
            void mouseExit (const juce::MouseEvent& event) override { owner.mouseExit (event); }
            PreviewPanel& owner;
        };
        AccentWordmark wordmark { true, 0.92f };
        ChildHoverListener childHoverListener { *this };
        EditorController& controller;
        const bool randomSection;
        bool compact = false;
        bool phoneGenerator = false;
        bool xyRandomizer = false;
        int randomizerRightInset = 0;
        PatternXYPad xyPad { controller };
        std::array<juce::TextButton, generatorSeedCount> xySeeds;
        juce::Rectangle<int> inspectorBounds;
        juce::TextButton close { "Back to Grid" }, mode, remove { juce::String::fromUTF8 ("×") };
        juce::TextButton rhythm { "Triplets" };
        ValueControl patternLength, refresh, velocity, probability, roll, nudge;
        std::array<ValueControl, 8> locks;
        CycleEditor cycle;
        std::vector<std::pair<Location, Cell>> valueStarts;
        std::optional<Layer> previousLayer;
        std::array<std::array<bool, Constants::maxSteps>, Constants::maxChannels> flashes {};
        // Everything paint() reads from the controller; sync() repaints only when it changes.
        struct Painted
        {
            size_t selectedCount = 0;
            int channelCount = 0, stepCount = 0, source = -1;
            bool reverseRows = false;
            std::array<juce::Colour, 8> channelColours {};
            std::array<std::string, 8> labels {};
            std::array<std::array<uint8_t, Constants::maxSteps>, Constants::maxChannels> velocities {};
            bool operator== (const Painted&) const = default;
        };
        std::optional<Painted> painted;
        void beginRelative (const juce::String&);
        void applyRelative (Layer, int);
        void resetSelectedValue (const juce::String&, int Cell::*, int);
        void updateHoverLayer (juce::Point<int>);
        void setHoverLayer (std::optional<Layer>);
        void paintRandomizerGrid (juce::Graphics&, juce::Rectangle<float>, float labelWidth, float labelSize) const;
        void layoutXYPad (juce::Rectangle<int>);
    };
}
