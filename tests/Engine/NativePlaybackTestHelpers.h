#pragma once

#include <engine/KickSnareHatEngine.h>
#include <engine/KshNativePlayback.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace ksh::test
{

inline void requireNativeRow (const NativePlaybackRow& row, const std::vector<double>& expected)
{
    INFO ("row = " << flattenNativeRow (row).size() << " fields");
    REQUIRE (flattenNativeRow (row) == expected);
}

inline std::vector<double> concatNativeRows (const std::vector<double>& a, const std::vector<double>& b)
{
    auto combined = a;
    combined.insert (combined.end(), b.begin(), b.end());
    return combined;
}

} // namespace ksh::test
