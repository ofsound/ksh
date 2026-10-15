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
        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool) override;
        void focusLost (FocusChangeType) override;

    private:
        LookAndFeel look;
        juce::TooltipWindow tooltips { this, 600 };
        PatternGrid grid;
        juce::Label patternHeading;
        PreviewPanel preview, randomPanel;
        std::array<juce::TextButton, 16> patterns;
        std::array<juce::TextButton, 5> projectButtons;
        juce::TextEditor title, description;
        juce::TextButton mutePattern { "M" }, heading, clear { juce::String::fromUTF8 ("×") }, record { juce::String::fromUTF8 ("●") }, left { juce::String::fromUTF8 ("◀") }, right { juce::String::fromUTF8 ("▶") }, undoButton { juce::String::fromUTF8 ("↶") }, redoButton { juce::String::fromUTF8 ("↷") }, rate, play { "Play" }, dc { "DC" };
        juce::TextButton randomButton { "Random" };
        ValueControl swing, subdivision, velocityHumanize, timingHumanize, steps, scale, tempo;
        std::bitset<8> heldPads;
        int previousScale = 100;
        bool syncing = false;
        void timerCallback() override;
        void configureValue (ValueControl&, const juce::String&, int, int, const std::string&);
        void rateMenu();
        bool textFocused() const;
    };
}
