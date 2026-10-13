#include "KshUiBridge.h"
#include "PluginProcessor.h"

bool KshUiBridge::handleCommand (const juce::String& commandJson)
{
    nlohmann::json command;

    try
    {
        command = nlohmann::json::parse (commandJson.toStdString());
    } catch (...)
    {
        return false;
    }

    if (!command.is_object() || !command.contains ("selector"))
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
        if (!args.is_array() || args.empty() || !args[0].is_object())
            return false;

        if (!processor.applyPersistenceFromUi (args[0]))
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

    if (!ok)
        return false;

    if (selector == "reset")
    {
        syncAll();
        return true;
    }

    // Audition only queues realtime work; it does not require a preview rebuild.
    if (selector == "channel_audition" || selector == "pattern_record_row")
        return true;

    // Coalesce preview updates outside the command turn. Snapshot publish already happened in
    // dispatchUiEngineCommand; flush the dirty preview on the next async turn.
    processor.scheduleUiPreviewFlush();
    return true;
}
