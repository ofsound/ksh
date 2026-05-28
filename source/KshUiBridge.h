#pragma once

#include "engine/KickSnareHatEngine.h"
#include "engine/KshNativePlayback.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <nlohmann/json.hpp>

class PluginProcessor;

/** Message-thread bridge between the WebView UI and {@link PluginProcessor}. */
class KshUiBridge
{
public:
    explicit KshUiBridge (PluginProcessor& processorIn);

    void attachWebView (juce::WebBrowserComponent* browser);
    void detachWebView();

    [[nodiscard]] bool handleCommand (const juce::String& commandJson);

    void syncAll();
    void requestState();
    void emitEngineState();
    void emitPreview (const nlohmann::json& preview);
    void emitStatus (const std::string& message);
    void emitNoteHit (const ksh::NativeHit& hit);
    void emitCurrentStep (int stepOneBased);
    void pollTransportUi();

private:
    PluginProcessor& processor;
    juce::Component::SafePointer<juce::WebBrowserComponent> webView;
    int lastEmittedStep = 0;

    ksh::KickSnareHatEngine& engine();

    void emitJsonEvent (const juce::Identifier& eventId, const nlohmann::json& payload);
    void emitStatusSelector (const std::string& message);
};
