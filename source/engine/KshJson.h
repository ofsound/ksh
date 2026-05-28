#pragma once

#include "KshTypes.h"

#include <nlohmann/json.hpp>

namespace ksh
{

inline void to_json (nlohmann::json& json, const Cell& cell)
{
    json = {
        { "enabled", cell.enabled ? 1 : 0 },
        { "velocity", cell.velocity },
        { "probability", cell.probability },
        { "cycle", cell.cycle },
        { "cycleOffset", cell.cycleOffset },
        { "cycleInverted", cell.cycleInverted ? 1 : 0 },
        { "roll", cell.roll },
        { "source", cell.source }
    };
}

inline void from_json (const nlohmann::json& json, Cell& cell)
{
    if (json.contains ("enabled"))
    {
        if (json["enabled"].is_boolean())
            cell.enabled = json["enabled"].get<bool>();
        else
            cell.enabled = json["enabled"].get<int>() != 0;
    }

    if (json.contains ("velocity"))
        cell.velocity = json["velocity"].get<int>();

    if (json.contains ("probability"))
        cell.probability = json["probability"].get<int>();

    if (json.contains ("cycle"))
        cell.cycle = json["cycle"].get<int>();

    if (json.contains ("cycleOffset"))
        cell.cycleOffset = json["cycleOffset"].get<int>();

    if (json.contains ("cycleInverted"))
    {
        if (json["cycleInverted"].is_boolean())
            cell.cycleInverted = json["cycleInverted"].get<bool>();
        else
            cell.cycleInverted = json["cycleInverted"].get<int>() != 0;
    }

    if (json.contains ("roll"))
        cell.roll = json["roll"].get<int>();

    if (json.contains ("source"))
        cell.source = json["source"].get<int>();

    cell = cloneCell (cell);
}

} // namespace ksh
