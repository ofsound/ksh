#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ksh
{
    class IOSFileDrop final : private juce::ComponentMovementWatcher
    {
    public:
        using FileReady = std::function<void (std::shared_ptr<juce::TemporaryFile>)>;
        IOSFileDrop (juce::Component&, std::function<FileReady()>);
        ~IOSFileDrop() override;

    private:
        void componentPeerChanged() override;
        void componentMovedOrResized (bool, bool) override {}
        void componentVisibilityChanged() override { componentPeerChanged(); }

        struct Native;
        std::unique_ptr<Native> native;
    };
}
