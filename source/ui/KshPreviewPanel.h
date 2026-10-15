#pragma once
#include "KshCycleEditor.h"
namespace ksh::ui
{
    class PreviewPanel : public juce::Component
    {
    public:
        explicit PreviewPanel (EditorController&, bool randomSection = false);
        void sync();
        void updatePlayback();
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        EditorController& controller;
        const bool randomSection;
        juce::TextButton mode, remove { juce::String::fromUTF8 ("×") };
        ValueControl refresh, velocity, probability, roll;
        std::array<ValueControl, 8> locks;
        CycleEditor cycle;
        std::vector<std::pair<Location, Cell>> valueStarts;
        std::optional<Layer> previousLayer;
        std::array<std::array<bool, 32>, 8> flashes {};
        void beginRelative (const juce::String&);
        void applyRelative (Layer, int);
    };
}
