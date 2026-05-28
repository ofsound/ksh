#pragma once

#include "KickSnareHatEngine.h"

#include <nlohmann/json.hpp>
#include <string_view>

namespace ksh
{

/** UI → engine dispatch (1-based indexes at the JSON boundary, M4L parity). */
[[nodiscard]] bool dispatchEngineCommand (KickSnareHatEngine& engine,
                                          std::string_view selector,
                                          const nlohmann::json& args);

} // namespace ksh
