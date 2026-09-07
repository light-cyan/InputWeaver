#pragma once

#include <array>
#include <string_view>

namespace inputweaver::language {

struct MouseFieldWord final {
    std::string_view name;
    bool mouseState{};
    bool meter{};
};

// Contextual property names are not reserved declaration names.
// Meter membership covers the whole family; source/view type checks belong to the compiler.
inline constexpr std::array<MouseFieldWord, 15> kMouseFieldWords{{
    {"x", true, true},
    {"y", true, true},
    {"dx", true, true},
    {"dy", true, true},
    {"wheel_x", true, true},
    {"wheel_y", true, true},
    {"moving", true, true},
    {"idle_time", true, false},
    {"start_x", false, true},
    {"start_y", false, true},
    {"distance", false, true},
    {"period", false, true},
    {"progress", false, true},
    {"remaining", false, true},
    {"valid", false, true},
}};

[[nodiscard]] constexpr const MouseFieldWord* FindMouseFieldWord(
    std::string_view name) noexcept
{
    for (const auto& word : kMouseFieldWords) {
        if (word.name == name) return &word;
    }
    return nullptr;
}

} // namespace inputweaver::language
