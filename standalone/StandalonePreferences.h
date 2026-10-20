#pragma once
#include "../source/PluginProcessor.h"

namespace ksh
{
    inline void restoreStandaloneProjectFile (PluginProcessor& processor, const juce::PropertySet& settings)
    {
        const auto path = settings.getValue ("projectFile");
        const juce::File file = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
        processor.setProjectFile (file.existsAsFile() ? file : juce::File());
    }

    inline void saveStandaloneProjectFile (const PluginProcessor& processor, juce::PropertySet& settings)
    {
        settings.setValue ("projectFile", processor.getProjectFile().getFullPathName());
    }

    inline void restoreStandaloneUiScale (PluginProcessor& processor, const juce::PropertySet& settings)
    {
        processor.setProjectUiScalePercent (settings.getIntValue ("uiScalePercent", 100));
    }

    inline void saveStandaloneUiScale (const PluginProcessor& processor, juce::PropertySet& settings)
    {
        settings.setValue ("uiScalePercent", processor.getProjectUiScalePercent());
    }
}
