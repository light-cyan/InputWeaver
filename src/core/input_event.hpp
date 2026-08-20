#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace inputweaver {

using SelfTag = std::uint32_t;
using ControlCode = std::uint32_t;
using ProcessId = std::uint32_t;
using ScanCode = std::uint32_t;
using RawInputFlags = std::uint32_t;
using MouseData = std::uint32_t;
using InputTimestamp = std::uint32_t;
using InputExtraInfo = std::uintptr_t;
using InputCoordinate = std::int32_t;

struct ScreenPoint final {
    InputCoordinate x{};
    InputCoordinate y{};
};

namespace control {

inline constexpr ControlCode kNone = 0x00U;
inline constexpr ControlCode kMouseLeft = 0x01U;
inline constexpr ControlCode kMouseRight = 0x02U;
inline constexpr ControlCode kControl = 0x11U;
inline constexpr ControlCode kShift = 0x10U;
inline constexpr ControlCode kMouseMiddle = 0x04U;
inline constexpr ControlCode kMouseX1 = 0x05U;
inline constexpr ControlCode kMouseX2 = 0x06U;
inline constexpr ControlCode kF6 = 0x75U;
inline constexpr ControlCode kF7 = 0x76U;
inline constexpr ControlCode kF8 = 0x77U;
inline constexpr ControlCode kF9 = 0x78U;
inline constexpr ControlCode kF10 = 0x79U;
inline constexpr ControlCode kF12 = 0x7BU;
inline constexpr ControlCode kLeftShift = 0xA0U;
inline constexpr ControlCode kRightShift = 0xA1U;
inline constexpr ControlCode kLeftControl = 0xA2U;
inline constexpr ControlCode kRightControl = 0xA3U;

}  // namespace control

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
    ControlCode code{};
    ScanCode scanCode{};
    RawInputFlags flags{};
    MouseData mouseData{};
    ScreenPoint position{};
    InputTimestamp timestamp{};
    InputExtraInfo extraInfo{};
};

struct Action {
    DeviceKind device{};
    Transition transition{};
    ControlCode code{};
    InputCoordinate valueX{};
    InputCoordinate valueY{};
};

struct ActionBatch {
    unsigned long long sourceSequence{};
    unsigned long long outputStateGeneration{};
    ProcessId targetPid{};
    DeviceKind outputDevice{};
    ControlCode outputCode{};
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

} // namespace inputweaver
