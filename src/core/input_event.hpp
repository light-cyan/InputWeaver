#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ukr {

using SelfTag = std::uint32_t;

inline constexpr std::size_t kMaxActionsPerBatch = 8;
inline constexpr std::size_t kActionQueueCapacity = 256;

enum class DeviceKind : unsigned char {
    Keyboard,
    Mouse
};

enum class InputOrigin : unsigned char {
    PhysicalCandidate,
    SelfInjected,
    ExternalInjected
};

enum class Transition : unsigned char {
    Down,
    Up,
    Move,
    VerticalWheel,
    HorizontalWheel
};

enum class InputDecision : unsigned char {
    Forward,
    Suppress
};

struct InputEvent {
    DeviceKind device{};
    InputOrigin origin{};
    Transition transition{};
    DWORD code{};
    DWORD scanCode{};
    DWORD flags{};
    DWORD mouseData{};
    POINT position{};
    DWORD timestamp{};
    ULONG_PTR extraInfo{};
};

struct Action {
    DeviceKind device{};
    Transition transition{};
    DWORD code{};
    LONG valueX{};
    LONG valueY{};
};

struct ActionBatch {
    unsigned long long sourceSequence{};
    unsigned long long outputStateGeneration{};
    DWORD targetPid{};
    DeviceKind outputDevice{};
    DWORD outputCode{};
    bool requiresPointerTarget{};
    std::array<Action, kMaxActionsPerBatch> actions{};
    std::size_t actionCount{};
};

inline constexpr unsigned long long kPhysicalOutputDownMask = 1ULL;

[[nodiscard]] constexpr unsigned long long PackPhysicalOutputState(
    unsigned long long generation,
    bool down) noexcept
{
    return (generation << 1U) | (down ? kPhysicalOutputDownMask : 0ULL);
}

[[nodiscard]] constexpr unsigned long long PhysicalOutputGeneration(
    unsigned long long packedState) noexcept
{
    return packedState >> 1U;
}

[[nodiscard]] constexpr bool PhysicalOutputIsDown(unsigned long long packedState) noexcept
{
    return (packedState & kPhysicalOutputDownMask) != 0;
}

} // namespace ukr
