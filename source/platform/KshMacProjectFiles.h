#pragma once

#include <juce_core/juce_core.h>

#if JUCE_MAC
namespace ksh
{
    class MacProjectFiles
    {
    public:
        static juce::File documentsDirectory();
        static juce::File iCloudDriveDirectory();
        static juce::String makeBookmark (const juce::File&);
        static juce::Result replace (const juce::File&, const juce::MemoryBlock&);

        juce::File restoreBookmark (const juce::String&);
        void releaseAccessUnless (const juce::File&);

    private:
        juce::File scopedFile;
        std::shared_ptr<void> scopedURL;
    };
}
#endif
