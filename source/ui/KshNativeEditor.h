#pragma once
#include "KshPatternGrid.h"
#include "KshPreviewPanel.h"
namespace ksh::ui
{
    class NativeEditor : public juce::Component, private juce::Timer
    {
    public:
        explicit NativeEditor (PluginProcessor&);
        ~NativeEditor() override;
        EditorController controller;
        Layout layout;
        std::function<void (int)> projectAction; // new, load, save, previous, next
        std::function<void()> preferredSizeChanged;
        juce::String projectTitle() const { return title.getText(); }
        juce::String projectDescription() const { return description.getText(); }
        void sync();
        void useViewportFit();
        void setCompactViewport (bool, int width, int height);
        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool) override;
        void focusLost (FocusChangeType) override;
        void modifierKeysChanged (const juce::ModifierKeys&) override;

    private:
        LookAndFeel look;
        std::unique_ptr<juce::PropertiesFile> uiPreferences;
        FooterHelp footerHelp { *this };
        juce::TextButton tooltipToggle { "Tips" };
        PatternGrid grid;
        PreviewPanel preview, randomPanel;
        juce::Viewport aboutViewport;
        std::array<juce::TextButton, 16> patterns;
        std::array<juce::TextButton, 5> projectButtons;
        juce::TextEditor title, description;
        GestureButton selectMode { "Select" };
        juce::TextButton stop { "Stop" };
        juce::TextButton mutePattern { "M" }, velocityLayer { "Velocity" }, chanceLayer { "Chance" }, clear { juce::String::fromUTF8 ("×") }, record { juce::String::fromUTF8 ("●") }, left { juce::String::fromUTF8 ("◀") }, right { juce::String::fromUTF8 ("▶") }, undoButton { juce::String::fromUTF8 ("↶") }, redoButton { juce::String::fromUTF8 ("↷") }, rate, play { "Play" }, dc { "DC" };
        ValueControl swing, subdivision, velocityHumanize, timingHumanize, steps, scale, tempo;
        std::bitset<8> heldPads;
        int previousScale = 100;
        bool syncing = false;
        enum class Panel { none, patterns, project, pattern, feel, generate, channel, help };
        Panel panel = Panel::none;
        int selectedChannel = 0;
        juce::TextButton compactPattern, controls { "Controls" }, closePanel { "Done" }, deselect { "Deselect" }, page { "1-16" }, duplicate { "Copy" };
        std::array<juce::TextButton, 6> panelTabs;
        juce::TextEditor channelName;
        ValueControl channelNote, loopStart, loopLength;
        juce::TextButton channelDirection, channelPing { "Ping-pong" }, channelSolo { "Solo" }, channelClear { "Clear row" }, channelReset { "Reset row" };
        void resizedCompact();
        void setupCompactControls();
        void showPanel (Panel);
        void syncChannel();
        void cancelCompactGestures();
        void timerCallback() override;
        void updateLayerHeading();
        void configureValue (ValueControl&, const juce::String&, int, int, const std::string&);
        void rateMenu();
        static const std::array<juce::String, 8> rateNames;
        static int rateControlWidth();
        void toggleAboutPanel();
        bool textFocused() const;
    };
}
