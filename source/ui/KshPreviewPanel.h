#pragma once
#include "KshCycleEditor.h"
namespace ksh::ui
{
    class PreviewPanel : public juce::Component
    {
    public:
        explicit PreviewPanel (EditorController&, bool randomSection = false);
        ~PreviewPanel() override;
        std::function<void()> onLogoDoubleClick;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void visibilityChanged() override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void sync();
        void updatePlayback();
        void paint (juce::Graphics&) override;
        void resized() override;
        void setInspectorBounds (juce::Rectangle<int>);
        void setCompact (bool);
        void cancelGesture();
        bool reverseRows = false;

    private:
        EditorController& controller;
        const bool randomSection;
        bool compact = false;
        juce::Rectangle<int> inspectorBounds;
        juce::TextButton mode, remove { juce::String::fromUTF8 ("×") };
        ValueControl refresh, velocity, probability, roll, nudge;
        std::array<ValueControl, 8> locks;
        CycleEditor cycle;
        ValueControl cycleLength;
        std::vector<std::pair<Location, Cell>> valueStarts;
        std::optional<Layer> previousLayer;
        std::array<std::array<bool, 32>, 8> flashes {};
        // Everything paint() reads from the controller; sync() repaints only when it changes.
        struct Painted
        {
            size_t selectedCount = 0;
            int channelCount = 0, stepCount = 0;
            bool reverseRows = false;
            std::array<std::string, 8> labels {};
            std::array<std::array<bool, 32>, 8> generated {};
            bool operator== (const Painted&) const = default;
        };
        std::optional<Painted> painted;
        void beginRelative (const juce::String&);
        void applyRelative (Layer, int);
        void updateHoverLayer (juce::Point<int>);
        void setHoverLayer (std::optional<Layer>);
    };
}
