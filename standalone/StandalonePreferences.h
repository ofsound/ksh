#pragma once
#include "../source/PluginProcessor.h"

namespace ksh
{
    inline void restoreStandaloneUiScale (PluginProcessor& processor, const juce::PropertySet& settings)
    {
        processor.setProjectUiScalePercent (settings.getIntValue ("uiScalePercent", 100));
    }

    inline void saveStandaloneUiScale (const PluginProcessor& processor, juce::PropertySet& settings)
    {
        settings.setValue ("uiScalePercent", processor.getProjectUiScalePercent());
    }
}
