#include "KshEngineCommands.h"

#include <algorithm>
#include <cmath>

namespace ksh
{
namespace
{
int zeroBased (int value)
{
    return std::max (0, value - 1);
}

int argInt (const nlohmann::json& args, size_t index, int fallback = 0)
{
    if (! args.is_array() || index >= args.size())
        return fallback;

    const auto& value = args[index];

    if (value.is_number_integer())
        return value.get<int>();

    if (value.is_number_float())
        return static_cast<int> (value.get<double>());

    if (value.is_string())
        return std::stoi (value.get<std::string>());

    return fallback;
}

double argDouble (const nlohmann::json& args, size_t index, double fallback = 0.0)
{
    if (! args.is_array() || index >= args.size())
        return fallback;

    const auto& value = args[index];

    if (value.is_number())
        return value.get<double>();

    if (value.is_string())
        return std::stod (value.get<std::string>());

    return fallback;
}

bool argBool (const nlohmann::json& args, size_t index, bool fallback = false)
{
    if (! args.is_array() || index >= args.size())
        return fallback;

    const auto& value = args[index];

    if (value.is_boolean())
        return value.get<bool>();

    if (value.is_number())
        return value.get<int>() != 0;

    if (value.is_string())
        return value.get<std::string>() == "1" || value.get<std::string>() == "true";

    return fallback;
}

std::string argString (const nlohmann::json& args, size_t index)
{
    if (! args.is_array() || index >= args.size())
        return {};

    const auto& value = args[index];

    if (value.is_string())
        return value.get<std::string>();

    if (value.is_number_integer())
        return std::to_string (value.get<int>());

    if (value.is_number_float())
        return std::to_string (value.get<double>());

    return {};
}

std::string joinArgsFrom (const nlohmann::json& args, size_t startIndex)
{
    if (! args.is_array())
        return {};

    std::string joined;

    for (size_t i = startIndex; i < args.size(); ++i)
    {
        if (i > startIndex)
            joined.push_back (' ');

        joined += argString (args, i);
    }

    return joined;
}

int parseChannelLock (const nlohmann::json& args, size_t index)
{
    const auto lockText = argString (args, index);

    if (lockText.empty())
        return zeroBased (argInt (args, index, 1));

    if (lockText == "random" || lockText == "Random" || lockText == "R")
        return -1;

    return zeroBased (argInt (args, index, 1));
}

int parseStaticSource (const nlohmann::json& args, size_t index)
{
    const auto sourceText = argString (args, index);

    if (sourceText == "mute" || sourceText == "Mute" || sourceText == "M" || sourceText == "m")
        return Constants::mutedStaticSource;

    return zeroBased (argInt (args, index, 1));
}
} // namespace

bool dispatchEngineCommand (KickSnareHatEngine& engine,
                            std::string_view selector,
                            const nlohmann::json& args)
{
    if (selector == "steps")
    {
        engine.setStepCount (argInt (args, 0, engine.getStepCount()));
        return true;
    }

    if (selector == "source_steps")
    {
        const int source = zeroBased (argInt (args, 0, engine.getStaticSource() + 1));
        engine.setSourceStepCount (source, argInt (args, 1, engine.getSourceStepCount (source)));
        return true;
    }

    if (selector == "channels")
    {
        engine.setChannelCount (argInt (args, 0, engine.getChannelCount()));
        return true;
    }

    if (selector == "refresh_steps")
    {
        engine.setRefreshSteps (argInt (args, 0, engine.getRefreshSteps()));
        return true;
    }

    if (selector == "mode")
    {
        engine.setGenerationMode (normalizeGenerationMode (argString (args, 0)));
        return true;
    }

    if (selector == "static_source")
    {
        engine.setStaticSource (parseStaticSource (args, 0));
        return true;
    }

    if (selector == "rate")
    {
        engine.setRate (argString (args, 0));
        return true;
    }

    if (selector == "source_rate")
    {
        const int source = zeroBased (argInt (args, 0, engine.getStaticSource() + 1));
        engine.setSourceRate (source, argString (args, 1));
        return true;
    }

    if (selector == "tempo")
    {
        engine.setTempo (argDouble (args, 0, engine.getTempo()));
        return true;
    }

    if (selector == "swing")
    {
        engine.setSwing (argInt (args, 0, engine.getSwing()));
        return true;
    }

    if (selector == "velocity_humanize")
    {
        engine.setVelocityHumanize (argInt (args, 0, engine.getVelocityHumanize()));
        return true;
    }

    if (selector == "timing_humanize")
    {
        engine.setTimingHumanize (argInt (args, 0, engine.getTimingHumanize()));
        return true;
    }

    if (selector == "device_active")
    {
        engine.setDeviceActive (argBool (args, 0, engine.isDeviceActive()));
        return true;
    }

    if (selector == "channel_label")
    {
        engine.setChannelLabel (zeroBased (argInt (args, 0, 1)), joinArgsFrom (args, 1));
        return true;
    }

    if (selector == "channel_note")
    {
        engine.setChannelNote (zeroBased (argInt (args, 0, 1)), argInt (args, 1, 36));
        return true;
    }

    if (selector == "channel_audition")
    {
        [[maybe_unused]] const auto audition = engine.auditionChannel (zeroBased (argInt (args, 0, 1)));
        return true;
    }

    if (selector == "channel_lock")
    {
        engine.setChannelLock (zeroBased (argInt (args, 0, 1)), parseChannelLock (args, 1));
        return true;
    }

    if (selector == "channel_loop_length")
    {
        const int channel = zeroBased (argInt (args, 0, 1));
        const int fallbackStart = engine.channelAt (channel).loopStart + 1;
        engine.setChannelLoopLength (channel,
                                     argInt (args, 1, engine.getStepCount()),
                                     zeroBased (argInt (args, 2, fallbackStart)));
        return true;
    }

    if (selector == "channel_playback_mode")
    {
        engine.setChannelPlaybackMode (zeroBased (argInt (args, 0, 1)), normalizePlaybackMode (argString (args, 1)));
        return true;
    }

    if (selector == "source_channel_mute")
    {
        engine.setSourceChannelMute (zeroBased (argInt (args, 0, 1)),
                                     zeroBased (argInt (args, 1, 1)),
                                     argBool (args, 2, false));
        return true;
    }

    if (selector == "source_channel_reset")
    {
        engine.resetSourceChannel (zeroBased (argInt (args, 0, 1)), zeroBased (argInt (args, 1, 1)));
        return true;
    }

    if (selector == "source_pattern_copy")
    {
        engine.copySourcePattern (zeroBased (argInt (args, 0, 1)), zeroBased (argInt (args, 1, 1)));
        return true;
    }

    if (selector == "cell")
    {
        engine.setCell (zeroBased (argInt (args, 0, 1)),
                        zeroBased (argInt (args, 1, 1)),
                        zeroBased (argInt (args, 2, 1)),
                        argBool (args, 3, false),
                        argInt (args, 4, 100),
                        argInt (args, 5, 100),
                        argInt (args, 6, 1),
                        argInt (args, 7, 0),
                        argBool (args, 8, false),
                        argInt (args, 9, 1));
        return true;
    }

    if (selector == "cell_enabled")
    {
        engine.setCellEnabled (zeroBased (argInt (args, 0, 1)),
                               zeroBased (argInt (args, 1, 1)),
                               zeroBased (argInt (args, 2, 1)),
                               argBool (args, 3, false));
        return true;
    }

    if (selector == "cell_velocity")
    {
        engine.setCellVelocity (zeroBased (argInt (args, 0, 1)),
                                zeroBased (argInt (args, 1, 1)),
                                zeroBased (argInt (args, 2, 1)),
                                argInt (args, 3, 100));
        return true;
    }

    if (selector == "cell_probability")
    {
        engine.setCellProbability (zeroBased (argInt (args, 0, 1)),
                                   zeroBased (argInt (args, 1, 1)),
                                   zeroBased (argInt (args, 2, 1)),
                                   argInt (args, 3, 100));
        return true;
    }

    if (selector == "cell_cycle")
    {
        engine.setCellCycle (zeroBased (argInt (args, 0, 1)),
                             zeroBased (argInt (args, 1, 1)),
                             zeroBased (argInt (args, 2, 1)),
                             argInt (args, 3, 1));
        return true;
    }

    if (selector == "cell_cycle_offset")
    {
        engine.setCellCycleOffset (zeroBased (argInt (args, 0, 1)),
                                   zeroBased (argInt (args, 1, 1)),
                                   zeroBased (argInt (args, 2, 1)),
                                   argInt (args, 3, 0));
        return true;
    }

    if (selector == "cell_cycle_inverted")
    {
        engine.setCellCycleInverted (zeroBased (argInt (args, 0, 1)),
                                     zeroBased (argInt (args, 1, 1)),
                                     zeroBased (argInt (args, 2, 1)),
                                     argBool (args, 3, false));
        return true;
    }

    if (selector == "cell_roll")
    {
        engine.setCellRoll (zeroBased (argInt (args, 0, 1)),
                            zeroBased (argInt (args, 1, 1)),
                            zeroBased (argInt (args, 2, 1)),
                            argInt (args, 3, 1));
        return true;
    }

    if (selector == "reset")
    {
        engine.reset();
        return true;
    }

    return false;
}

} // namespace ksh
