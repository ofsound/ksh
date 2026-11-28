#pragma once
#include "KshCycleEditor.h"
#include "KshPatternGenerator.h"
namespace ksh::ui
{
    class AccentWordmark : public juce::Component
    {
    public:
        explicit AccentWordmark (float artworkScale = 1.0f);
        std::function<void (bool)> onAccentSelected;
        std::function<void (bool)> onThemeGesture;
        std::function<void (bool, std::optional<juce::Colour>)> onAccentColourEdited;
        std::function<void (Palette::BackgroundTone)> onBackgroundToneEdited;
        std::function<void (int)> onTextBrightnessEdited;
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
        LogoButton textBrightnessLogo;
        std::optional<int> startingTextBrightness;
    };

    class PreviewPanel : public juce::Component
    {
    public:
        explicit PreviewPanel (EditorController&, bool randomSection = false);
        ~PreviewPanel() override;
        std::function<void (int)> onTextBrightnessEdited;
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
        void setCompact (bool, bool usePhoneLayout = false, bool onPhone = false);
        void setDisplayedLayer (Layer);
        void cancelGesture();
        void setGeneratorRowLimit (int limit) { xyPad.rowLimit = limit; }
        void commitPendingCycleEdit() { cycle.commitPendingBeatClick(); }
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
        AccentWordmark wordmark { 0.92f };
        ChildHoverListener childHoverListener { *this };
        EditorController& controller;
        const bool randomSection;
        bool compact = false;
        int randomizerRightInset = 0;
        PatternXYPad xyPad { controller };
        std::array<juce::TextButton, drumGrooves.size()> grooveButtons;
        juce::Rectangle<int> inspectorBounds;
        juce::TextButton remove { juce::String::fromUTF8 ("×") };
        class GlideButton final : public juce::TextButton
        {
        public:
            void paintButton (juce::Graphics&, bool hover, bool down) override;
            juce::Colour accent;
        };
        GlideButton glide;
        juce::Rectangle<int> glideCaptionBounds;
        ValueControl velocity, probability, nudge, span, roll;
        bool ccValueInspector = false;
        CycleEditor cycle;
        std::optional<juce::Colour> rowAccent;
        std::vector<std::pair<Location, Cell>> valueStarts;
        std::optional<Layer> previousLayer;
        std::array<std::array<bool, Constants::maxSteps>, Constants::maxChannels> flashes {};
        struct GridHit
        {
            uint8_t velocity = 0, roll = 1, span = 1;
            std::array<uint8_t, Constants::maxRoll> splitVelocities {};
            bool operator== (const GridHit&) const = default;
        };
        // Everything paint() reads from the controller; sync() repaints only when it changes.
        struct Painted
        {
            size_t selectedCount = 0;
            int channelCount = 0, stepCount = 0, source = -1;
            bool reverseRows = false, drumColors = true;
            std::array<juce::Colour, Constants::maxChannels> channelColours {};
            std::array<std::string, Constants::maxChannels> labels {};
            std::array<std::array<GridHit, Constants::maxSteps>, Constants::maxChannels> hits {};
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
