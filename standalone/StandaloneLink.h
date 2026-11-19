#pragma once

#include "../source/platform/StandaloneSync.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ksh
{
    class StandaloneLink : public StandaloneSync
    {
    public:
        static std::unique_ptr<StandaloneLink> create (double initialTempo, juce::PropertySet*);
        virtual void showSettings (juce::Component&) = 0;
        virtual StandaloneLinkStatus status() const = 0;
        virtual void setForeground (bool) = 0;
        virtual void setOutputLatency (double seconds) = 0;
    };
}
