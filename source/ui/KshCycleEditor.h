#pragma once
#include "KshEditorController.h"
#include "KshStyle.h"
namespace ksh::ui
{
    class CycleEditor : public juce::Component
    {
    public:
        explicit CycleEditor (EditorController& c) : controller (c)
        {
            setTitle ("Cycle pattern");
            length.minimum = 1;
            length.maximum = 64;
            length.resetValue = 1;
            length.suffix = " steps";
            length.setTitle ("Cycle length");
            length.onBegin = [this] { controller.begin ("Cycle length"); };
            length.onChange = [this] (int n) { auto list = locations(); if (! list.empty()) controller.setCycle (n, controller.cell (list.front()).cycleMask, list); repaint(); };
            length.onEnd = [this] { controller.commit(); };
            addAndMakeVisible (length);
            for (int i = 0; i < 8; ++i)
            {
                auto& b = bits[static_cast<size_t> (i)];
                addAndMakeVisible (b);
                b.setButtonText (juce::String (i + 1));
                b.setTooltip ("Toggle cycle beat; click beyond length to extend");
                b.onClick = [this, i] {
                    const auto list = locations();
                    if (list.empty())
                        return;
                    const auto first = controller.cell (list.front());
                    const int nextLength = juce::jmax (first.cycle, i + 1);
                    const int mask = i >= first.cycle ? first.cycleMask : first.cycleMask ^ (1 << i);
                    controller.begin ("Cycle pattern");
                    controller.setCycle (nextLength, mask, list);
                    controller.commit();
                    sync();
                };
            }
        }
        std::function<std::vector<Location>()> locations = [] { return std::vector<Location>(); };
        void sync()
        {
            const auto list = locations();
            setEnabled (!list.empty());
            const auto c = list.empty() ? Cell {} : controller.cell (list.front());
            length.setValue (c.cycle);
            for (int i = 0; i < 8; ++i)
                bits[static_cast<size_t> (i)].setToggleState (i < c.cycle && (c.cycleMask & (1 << i)) != 0, juce::dontSendNotification);
            repaint();
        }
        void resized() override
        {
            const int w = (getWidth() - 20) / 8;
            for (int i = 0; i < 8; ++i)
                bits[static_cast<size_t> (i)].setBounds (10 + i * w, 10, w - 4, juce::jmax (24, getHeight() - 56));
            length.setBounds (10, getHeight() - 32, 100, 24);
        }
        void paint (juce::Graphics& g) override
        {
            surface (g, getLocalBounds().toFloat().reduced (0.5f));
            text (g, "CYCLE", { 120.0f, static_cast<float> (getHeight() - 30), static_cast<float> (getWidth() - 132), 20.0f }, 10, Palette::muted, true, juce::Justification::centredRight);
        }

    private:
        EditorController& controller;
        ValueControl length;
        std::array<juce::TextButton, 8> bits;
    };
}
