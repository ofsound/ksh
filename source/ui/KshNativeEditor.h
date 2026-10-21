#pragma once
#include "KshPatternGrid.h"
#include "KshPatternButton.h"
#include "KshPreviewPanel.h"
#include "KshProjectDetails.h"
namespace ksh::ui
{
    class NativeEditor : public juce::Component, private juce::Timer
    {
    public:
        explicit NativeEditor (PluginProcessor&);
        ~NativeEditor() override;
        EditorController controller;
        Layout layout;
        std::function<void (int)> projectAction; // new, load, save, previous, next, save as
        std::function<void()> preferredSizeChanged;
        std::function<void (bool)> zenModeChanged;
        void setZenMode (bool);
        void setZenScaleLimit (int);
        juce::String projectTitle() const { return controller.processor.getProjectName(); }
        juce::String projectDescription() const { return controller.processor.getProjectDescription(); }
        void sync();
        void confirmDiscardingChanges (std::function<void()>);
        void useViewportFit();
        void setCompactViewport (bool, int width, int height);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool) override;
        void focusLost (FocusChangeType) override;
        void modifierKeysChanged (const juce::ModifierKeys&) override;

    private:
        LookAndFeel look;
        std::unique_ptr<juce::PropertiesFile> uiPreferences;
        FooterHelp footerHelp { *this };
        juce::TextButton tooltipToggle { "Tips" };
        juce::TextButton rowOrder, cellDisplay, zen { "Zen" };
        bool viewportFit = false;
        juce::TextButton modifierToggle { "Keys" };
        PatternGrid grid;
        PreviewPanel preview, randomPanel;
        juce::Viewport aboutViewport;
        std::array<PatternButton, 16> patterns;
        std::array<juce::TextButton, 6> projectButtons;
        ProjectHeader projectHeader;
        ProjectDetails projectDetails;
        NoteChooser noteChooser;
        uint64_t noteChooserEpoch = 0;
        int noteChooserChannel = -1, noteChooserOriginal = 0;
        void showNoteChooser (int, juce::Component&);
        DiscardChanges discardChanges;
        uint64_t discardEpoch = 0;
        GestureButton selectMode { "Select" };
        juce::TextButton stop { "Stop" };
        juce::TextButton mutePattern { "M" }, velocityLayer { "Velocity" }, chanceLayer { "Chance" }, clear { juce::String::fromUTF8 ("×") }, record { juce::String::fromUTF8 ("●") }, left { juce::String::fromUTF8 ("◀") }, right { juce::String::fromUTF8 ("▶") }, undoButton { juce::String::fromUTF8 ("↶") }, redoButton { juce::String::fromUTF8 ("↷") }, rate, play { "Play" }, dc { "DC" };
        ValueControl swing, subdivision, velocityHumanize, timingHumanize, steps, scale, tempo;
        std::bitset<8> heldPads;
        int previousScale = 100;
        std::optional<std::pair<Layout, juce::Rectangle<int>>> laidOut;
        bool syncing = false;
        enum class Panel { none, patterns, project, pattern, feel, generate, channel, help };
        Panel panel = Panel::none;
        int selectedChannel = 0;
        juce::TextButton compactPattern, controls { "Controls" }, closePanel { "Done" }, deselect { "Deselect" }, page { "1-16" }, duplicate { "Copy" };
        std::array<juce::TextButton, 6> panelTabs;
        juce::TextEditor channelName;
        ValueControl loopStart, loopLength;
        NoteInput channelNote;
        juce::TextButton channelDirection, channelPing { "Ping-pong" }, channelSolo { "Solo" }, channelClear { "Clear row" }, channelReset { "Reset row" };
        void resizedCompact();
        void setupCompactControls();
        void showPanel (Panel);
        void syncChannel();
        void cancelCompactGestures();
        void timerCallback() override;
        void updateLayerHeading();
        void updatePatternButtons();
        void configureValue (ValueControl&, const juce::String&, int, int, const std::string&);
        void rateMenu();
        void dragPattern (int source);
        static const std::array<juce::String, 8> rateNames;
        static int rateControlWidth();
        void toggleAboutPanel();
        bool textFocused() const;
    };
}
