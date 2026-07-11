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
        { "cycleMask", cell.cycleMask },
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

    if (json.contains ("cycleMask"))
    {
        cell.cycleMask = json["cycleMask"].get<int>();
    }
    else if (json.contains ("cycleOffset"))
    {
        bool inverted = false;
        if (json.contains ("cycleInverted"))
        {
            if (json["cycleInverted"].is_boolean())
                inverted = json["cycleInverted"].get<bool>();
            else
                inverted = json["cycleInverted"].get<int>() != 0;
        }
        cell.cycleMask = cycleMaskFromLegacyOffset (json["cycleOffset"].get<int>(), cell.cycle, inverted);
    }

    if (json.contains ("roll"))
        cell.roll = json["roll"].get<int>();

    if (json.contains ("source"))
        cell.source = json["source"].get<int>();

    cell = cloneCell (cell);
}

} // namespace ksh
