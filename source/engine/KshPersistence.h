#pragma once

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
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

inline std::optional<std::string> uriDecodePersistenceText (std::string_view encoded)
{
    std::string decoded;
    decoded.reserve (encoded.size());

    for (size_t i = 0; i < encoded.size(); ++i)
    {
        if (encoded[i] == '%' && i + 2 < encoded.size())
        {
            const auto hexByte = encoded.substr (i + 1, 2);
            char* end = nullptr;
            const auto value = std::strtoul (std::string (hexByte).c_str(), &end, 16);

            if (end != nullptr && *end == '\0')
            {
                decoded.push_back (static_cast<char> (value));
                i += 2;
                continue;
            }
        }

        decoded.push_back (encoded[i]);
    }

    return decoded;
}

inline std::optional<std::string> decodePatternStoreText (std::string_view value)
{
    const auto trimmed = trimPersistenceText (value);

    if (trimmed.empty())
        return std::nullopt;

    if (trimmed.front() == '{')
        return trimmed;

    return uriDecodePersistenceText (trimmed);
}

inline std::optional<std::string> decodeChunkedPersistenceJson (const nlohmann::json& array)
{
    if (! array.is_array() || array.empty())
        return std::nullopt;

    if (array[0].get<std::string>() != "ksh_json_chunks_v1")
        return std::nullopt;

    std::string encoded;

    for (size_t i = 1; i < array.size(); ++i)
        encoded += array[i].get<std::string>();

    return uriDecodePersistenceText (encoded);
}

inline std::optional<nlohmann::json> unwrapPatternDataWrapper (const nlohmann::json& parsed)
{
    if (! parsed.is_object() || ! parsed.contains ("ksh_pattern_data"))
        return std::nullopt;

    auto inner = parsed["ksh_pattern_data"];

    if (inner.is_array() && ! inner.empty())
        inner = inner[0];

    if (inner.is_object())
        return inner;

    if (inner.is_string())
    {
        const auto decoded = decodePatternStoreText (inner.get<std::string>());

        if (! decoded.has_value())
            return std::nullopt;

        try
        {
            return nlohmann::json::parse (*decoded);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

inline bool persistenceJsonLooksValid (const nlohmann::json& state)
{
    if (state.contains ("v") && state["v"].get<int>() == 1)
        return true;

    return state.contains ("stepCount") || state.contains ("sources");
}

inline std::optional<nlohmann::json> parsePersistencePayload (std::string_view bytes)
{
    const auto trimmed = trimPersistenceText (bytes);

    if (trimmed.empty() || trimmed == "get" || trimmed == "bang")
        return std::nullopt;

    try
    {
        auto parsed = nlohmann::json::parse (trimmed);

        if (parsed.is_array())
        {
            const auto decoded = decodeChunkedPersistenceJson (parsed);

            if (! decoded.has_value())
                return std::nullopt;

            parsed = nlohmann::json::parse (*decoded);
        }

        if (const auto unwrapped = unwrapPatternDataWrapper (parsed))
            parsed = *unwrapped;

        if (persistenceJsonLooksValid (parsed))
            return parsed;

        return std::nullopt;
    }
    catch (...)
    {
    }

    const auto decoded = decodePatternStoreText (trimmed);

    if (! decoded.has_value())
        return std::nullopt;

    try
    {
        auto parsed = nlohmann::json::parse (*decoded);

        if (const auto unwrapped = unwrapPatternDataWrapper (parsed))
            parsed = *unwrapped;

        if (persistenceJsonLooksValid (parsed))
            return parsed;
    }
    catch (...)
    {
    }

    return std::nullopt;
}

} // namespace ksh
