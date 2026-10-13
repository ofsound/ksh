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
        void mouseDown (const juce::MouseEvent&) override;
        void focusLost (FocusChangeType) override;

    private:
        LookAndFeel look;
        juce::TooltipWindow tooltips { this, 600 };
        PatternGrid grid;
        PreviewPanel preview;
        std::array<juce::TextButton, 16> patterns;
        std::array<juce::TextButton, 5> projectButtons;
        juce::TextEditor title, description;
        juce::TextButton mutePattern { "M" }, heading, clear { juce::String::fromUTF8 ("×") }, record { juce::String::fromUTF8 ("●") }, left { juce::String::fromUTF8 ("◀") }, right { juce::String::fromUTF8 ("▶") }, undoButton { juce::String::fromUTF8 ("↶") }, redoButton { juce::String::fromUTF8 ("↷") }, rate, play { "Play" }, dc { "DC" };
        ValueControl swing, subdivision, velocityHumanize, timingHumanize, steps, scale, tempo;
        std::unique_ptr<CycleEditor> popup;
        Location popupLocation;
        std::bitset<8> heldPads;
        int previousSource = -2, previousScale = 100;
        uint64_t previousDocument = 0;
        bool syncing = false;
        void timerCallback() override;
        void configureValue (ValueControl&, const juce::String&, int, int, const std::string&);
        void rateMenu();
        void showCycle (Location, juce::Rectangle<int>);
        bool textFocused() const;
    };
}
