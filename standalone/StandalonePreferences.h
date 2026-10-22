#pragma once
#include "../source/PluginProcessor.h"
#include "../source/ui/KshUiScale.h"

namespace ksh
{
    inline void restoreStandaloneProjectFile (PluginProcessor& processor, const juce::PropertySet& settings)
    {
       #if JUCE_IOS
        auto library = processor.getIOSProjectLibrary();
        library->restoreAccess (settings);
        processor.setProjectFile (library->currentFile());
       #else
        const auto path = settings.getValue ("projectFile");
        const juce::File file = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
        processor.setProjectFile (file.existsAsFile() ? file : juce::File());
       #endif
    }

    inline void saveStandaloneProjectFile (PluginProcessor& processor, juce::PropertySet& settings)
    {
       #if JUCE_IOS
        // Access bookmarks belong only to local app preferences, never host/project state.
        if (processor.getProjectFile() == juce::File())
        {
            settings.removeValue ("projectBookmark");
            settings.removeValue ("projectContentDigest");
        }
        else
            processor.getIOSProjectLibrary()->saveAccess (settings);
        settings.removeValue ("projectFile");
       #else
        settings.setValue ("projectFile", processor.getProjectFile().getFullPathName());
       #endif
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
        processor.setProjectUiScalePercent (juce::roundToInt (juce::jlimit (static_cast<double> (ui::UiScale::minimumPercent), static_cast<double> (ui::UiScale::maximumPercent), percent)));
    }

    inline void saveStandaloneUiScale (const PluginProcessor& processor, juce::PropertySet& settings)
    {
        settings.setValue ("uiScalePercent", processor.getProjectUiScalePercent());
        settings.setValue ("uiScaleReferencePercent", ui::UiScale::referencePercent);
    }
}
