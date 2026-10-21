#pragma once
#include "../source/PluginProcessor.h"
#include "../source/ui/KshUiScale.h"

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
        if (!settings.containsKey ("uiScalePercent"))
        {
            processor.setProjectUiScalePercent (100);
            return;
        }
        // Older settings measured zoom directly against the larger design size.
        // Convert once so an existing 85% window becomes the new 100% size.
        const auto reference = juce::jlimit (1, 1000, settings.getIntValue ("uiScaleReferencePercent", 100));
        const double percent = static_cast<double> (settings.getIntValue ("uiScalePercent", 100)) * reference / ui::UiScale::referencePercent;
        processor.setProjectUiScalePercent (juce::roundToInt (juce::jlimit (50.0, 120.0, percent)));
    }

    inline void saveStandaloneUiScale (const PluginProcessor& processor, juce::PropertySet& settings)
    {
        settings.setValue ("uiScalePercent", processor.getProjectUiScalePercent());
        settings.setValue ("uiScaleReferencePercent", ui::UiScale::referencePercent);
    }
}
