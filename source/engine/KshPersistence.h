#pragma once

#include <nlohmann/json.hpp>

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace ksh
{

inline std::string trimPersistenceText (std::string_view text)
{
    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.front())))
        text.remove_prefix (1);

    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.back())))
        text.remove_suffix (1);

    return std::string { text };
}

inline bool persistenceJsonLooksValid (const nlohmann::json& state)
{
    return state.is_object()
           && state.contains ("v")
           && state["v"].is_number_integer()
           && state["v"].get<int>() == 1;
}

inline std::optional<nlohmann::json> parsePersistencePayload (std::string_view bytes)
{
    const auto trimmed = trimPersistenceText (bytes);

    if (trimmed.empty())
        return std::nullopt;

    try
    {
        auto parsed = nlohmann::json::parse (trimmed);

        if (persistenceJsonLooksValid (parsed))
            return parsed;
    }
    catch (...)
    {
    }

    return std::nullopt;
}

} // namespace ksh
