#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_IOS
namespace ksh
{
    // Message-thread API. Cloud discovery and coordinated file IO run off the audio thread.
    // The processor owns this object so closing an AUv3 editor retains document access.
    class IOSProjectLibrary
    {
    public:
        struct Result
        {
            juce::File file;
            juce::MemoryBlock data;
            juce::String error;
            bool cancelled = false;
        };
        using Completion = std::function<void (Result)>;
        IOSProjectLibrary();
        ~IOSProjectLibrary();
        juce::File directory() const;
        juce::File currentFile() const;
        juce::Array<juce::File> files() const;
        bool busy() const;
        void cancel();
        void acceptFile (const juce::File&);
        void chooseOpen (juce::Component&, Completion);
        void read (const juce::File&, Completion);
        void save (juce::Component&, const juce::File&, const juce::String& name,
                   juce::MemoryBlock, bool saveAs, Completion);
        void share (juce::Component&, const juce::String& name, juce::MemoryBlock, Completion);
        void restoreAccess (const juce::PropertySet&);
        void saveAccess (juce::PropertySet&) const;

    private:
        struct State;
        std::shared_ptr<State> state;
    };
}
#endif
