#include "KshUiBridge.h"

#include "PluginProcessor.h"

#include <sstream>

namespace
{
std::vector<std::string> splitStatusMessage (const std::string& message)
{
    std::istringstream stream { message };
    std::vector<std::string> parts;
    std::string token;

    while (stream >> token)
        parts.push_back (token);

    return parts;
}
} // namespace

KshUiBridge::KshUiBridge (PluginProcessor& processorIn)
    : processor (processorIn)
{
}

void KshUiBridge::attachWebView (juce::WebBrowserComponent* browser)
{
    webView = browser;
}

void KshUiBridge::detachWebView()
{
    webView = nullptr;
    lastEmittedStep = 0;
    lastEmittedModifierMask = -1;
}

void KshUiBridge::emitJsonEvent (const juce::Identifier& eventId, const nlohmann::json& payload)
{
    if (webView == nullptr)
        return;

    // Always ship the raw nlohmann dump as a string. Round-tripping large
    // engine_state payloads through juce::var can drop or reshape the cells
    // array; the UI parses payload.json and applies a destructive cell replace.
    const auto jsonText = payload.dump();
    juce::DynamicObject::Ptr wrapper { new juce::DynamicObject() };
    wrapper->setProperty ("json", juce::String { jsonText });
    webView->emitEventIfBrowserIsVisible (eventId, juce::var (wrapper.get()));
}

void KshUiBridge::emitEngineState()
{
    auto state = processor.enginePersistenceState();
    state["standaloneTransportAvailable"] = processor.hasStandaloneTransport() ? 1 : 0;
    state["standaloneTransportPlaying"] = processor.isStandaloneTransportPlaying() ? 1 : 0;
    state["standaloneTempo"] = processor.getStandaloneTempoBpm();
    state["patternViewScale"] = processor.getPatternViewScale();
    state["projectUiScalePercent"] = processor.getProjectUiScalePercent();
    state["projectThemeMode"] = processor.getProjectThemeMode().toStdString();
    state["patternRecordingEnabled"] = processor.isPatternRecordingEnabled() ? 1 : 0;
    emitJsonEvent ("engine_state", state);
}

void KshUiBridge::emitPreview (const nlohmann::json& preview)
{
    emitJsonEvent ("preview", preview);
}

void KshUiBridge::emitStatusSelector (const std::string& message)
{
    if (webView == nullptr)
        return;

    const auto parts = splitStatusMessage (message);

    if (parts.empty())
        return;

    auto* payload = new juce::DynamicObject();
    payload->setProperty ("selector", juce::String { parts.front() });

    juce::Array<juce::var> args;

    for (size_t i = 1; i < parts.size(); ++i)
        args.add (juce::String { parts[i] });

    payload->setProperty ("args", args);
    webView->emitEventIfBrowserIsVisible ("status", juce::var (payload));
}

void KshUiBridge::emitStatus (const std::string& message)
{
    emitStatusSelector (message);
}

void KshUiBridge::emitNoteHit (const ksh::NativeHit& hit)
{
    if (webView == nullptr)
        return;

    // Use the raw-JSON path so integer fields (especially sourceStep=1 for the
    // first pattern cell) cannot be lost in juce::var round-trips.
    emitJsonEvent ("note_hit", nlohmann::json {
        { "channel", hit.uiChannel },
        { "generatedStep", hit.uiGeneratedStep },
        { "source", hit.uiSource },
        { "sourceStep", hit.uiSourceStep },
    });
}

void KshUiBridge::emitCurrentStep (int stepOneBased)
{
    if (webView == nullptr)
        return;

    if (stepOneBased <= 0)
    {
        if (lastEmittedStep != 0)
            webView->emitEventIfBrowserIsVisible ("current_step", 0);

        lastEmittedStep = 0;
        return;
    }

    if (stepOneBased == lastEmittedStep)
        return;

    lastEmittedStep = stepOneBased;
    webView->emitEventIfBrowserIsVisible ("current_step", stepOneBased);
}

void KshUiBridge::pollModifierKeys()
{
    if (webView == nullptr)
        return;

    const auto modifiers = juce::ModifierKeys::getCurrentModifiersRealtime();
    const auto shiftDown = modifiers.isShiftDown();
    const auto altDown = modifiers.isAltDown();
    const auto commandDown = modifiers.isCommandDown();
    const int mask = (shiftDown ? 1 : 0) | (altDown ? 2 : 0) | (commandDown ? 4 : 0);

    if (mask == lastEmittedModifierMask)
        return;

    lastEmittedModifierMask = mask;

    auto payload = nlohmann::json::object();
    payload["shiftKey"] = shiftDown;
    payload["altKey"] = altDown;
    payload["metaKey"] = commandDown;
    emitJsonEvent ("modifier_keys", payload);
}

void KshUiBridge::pollTransportUi()
{
    pollModifierKeys();
    processor.emitPendingNoteHitsForUi();

    const int step = processor.getCurrentStepForUi();
    emitCurrentStep (step);
}

void KshUiBridge::syncAll()
{
    emitEngineState();
    emitPreview (processor.enginePreviewState());
}

void KshUiBridge::requestState()
{
    emitEngineState();
}

bool KshUiBridge::handleCommand (const juce::String& commandJson)
{
    nlohmann::json command;

    try
    {
        command = nlohmann::json::parse (commandJson.toStdString());
    }
    catch (...)
    {
        return false;
    }

    if (! command.is_object() || ! command.contains ("selector"))
        return false;

    const auto selector = command["selector"].get<std::string>();
    const auto args = command.contains ("args") ? command["args"] : nlohmann::json::array();

    if (selector == "sync_all")
    {
        syncAll();
        return true;
    }

    if (selector == "apply_persistence")
    {
        if (! args.is_array() || args.empty() || ! args[0].is_object())
            return false;

        if (! processor.applyPersistenceFromUi (args[0]))
            return false;

        syncAll();
        return true;
    }

    if (selector == "request_state")
    {
        requestState();
        return true;
    }

    if (selector == "export_generated_bars")
        return false;

    const bool ok = processor.dispatchUiEngineCommand (selector, args);

    if (! ok)
        return false;

    if (selector == "reset")
    {
        syncAll();
        return true;
    }

    // Audition / row triggers must not push preview JSON back through the WebView.
    if (selector == "channel_audition" || selector == "pattern_record_row")
        return true;

    emitPreview (processor.enginePreviewState());
    return true;
}
