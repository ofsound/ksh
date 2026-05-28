#pragma once

#include <cmath>

namespace ksh
{

[[nodiscard]] inline int gcdInt (int a, int b)
{
    a = std::abs (a);
    b = std::abs (b);

    while (b != 0)
    {
        const int t = b;
        b = a % b;
        a = t;
    }

    return a != 0 ? a : 1;
}

[[nodiscard]] inline int lcmInt (int a, int b)
{
    a = std::max (1, a);
    b = std::max (1, b);
    return (a / gcdInt (a, b)) * b;
}

} // namespace ksh
