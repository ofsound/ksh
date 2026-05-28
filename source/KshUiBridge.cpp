#include "KshUiBridge.h"

#include "PluginProcessor.h"

#include "engine/KshEngineCommands.h"

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

ksh::KickSnareHatEngine& KshUiBridge::engine()
{
    return processor.getEngine();
}

void KshUiBridge::attachWebView (juce::WebBrowserComponent* browser)
{
    webView = browser;
}

void KshUiBridge::detachWebView()
{
    webView = nullptr;
    lastEmittedStep = 0;
}

void KshUiBridge::emitJsonEvent (const juce::Identifier& eventId, const nlohmann::json& payload)
{
    if (webView == nullptr)
        return;

    const auto jsonText = payload.dump();
    const auto parsed = juce::JSON::parse (jsonText);

    if (! parsed.isVoid())
    {
        webView->emitEventIfBrowserIsVisible (eventId, parsed);
        return;
    }

    juce::DynamicObject::Ptr wrapper { new juce::DynamicObject() };
    wrapper->setProperty ("json", juce::String { jsonText });
    webView->emitEventIfBrowserIsVisible (eventId, juce::var (wrapper.get()));
}

void KshUiBridge::emitEngineState()
{
    emitJsonEvent ("engine_state", engine().serializeForPersistence());
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

    auto* payload = new juce::DynamicObject();
    payload->setProperty ("channel", hit.uiChannel);
    payload->setProperty ("generatedStep", hit.uiGeneratedStep);
    payload->setProperty ("source", hit.uiSource);
    payload->setProperty ("sourceStep", hit.uiSourceStep);
    webView->emitEventIfBrowserIsVisible ("note_hit", juce::var (payload));
}

void KshUiBridge::emitCurrentStep (int stepOneBased)
{
    if (webView == nullptr)
        return;

    if (stepOneBased <= 0)
    {
        lastEmittedStep = 0;
        return;
    }

    if (stepOneBased == lastEmittedStep)
        return;

    lastEmittedStep = stepOneBased;
    webView->emitEventIfBrowserIsVisible ("current_step", stepOneBased);
}

void KshUiBridge::pollTransportUi()
{
    const int step = engine().playingStepOneBased;

    if (step > 0)
        emitCurrentStep (step);
}

void KshUiBridge::syncAll()
{
    emitEngineState();
    emitPreview (engine().snapshot());
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

    if (selector == "request_state")
    {
        requestState();
        return true;
    }

    if (selector == "open_editor")
        return true;

    if (selector == "export_generated_bars")
        return false;

    processor.suspendProcessing (true);
    const bool ok = ksh::dispatchEngineCommand (engine(), selector, args);

    if (ok && selector != "channel_audition")
        processor.getMidiPlayback().reset();

    processor.suspendProcessing (false);

    if (! ok)
        return false;

    if (selector == "reset")
    {
        syncAll();
        return true;
    }

    if (selector == "source_channel_reset")
    {
        emitEngineState();
        return true;
    }

    emitPreview (engine().snapshot());
    return true;
}
