#pragma once

#include <cstdint>

namespace inputweaver::support {

[[nodiscard]] constexpr std::uint64_t Mix64(std::uint64_t value) noexcept
{
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return value;
}

} // namespace inputweaver::support
