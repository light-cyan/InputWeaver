#pragma once

#include "input/input_types.hpp"

#include <cstddef>
#include <cstdint>

namespace inputweaver {

using WindowsSelfTag = std::uintptr_t;
using WindowsVirtualKey = std::uint32_t;
using WindowsProcessId = std::uint32_t;
using WindowsScanCode = std::uint32_t;
using WindowsHookFlags = std::uint32_t;
using WindowsMouseData = std::uint32_t;
using WindowsInputTimestamp = std::uint32_t;
using WindowsInputExtraInfo = std::uintptr_t;

struct WindowsNativeInputEvent final {
    DeviceKind device{};
    InputOrigin origin{};
    Transition transition{};
    WindowsVirtualKey virtualKey{};
    WindowsScanCode scanCode{};
    WindowsHookFlags hookFlags{};
    WindowsMouseData mouseData{};
    ScreenPoint position{};
    WindowsInputTimestamp timestamp{};
    WindowsInputExtraInfo extraInfo{};
};

enum class WindowsOutputKind : std::uint8_t {
    None,
    KeyboardVirtualKey,
    KeyboardScanCode,
    MouseButton,
};

enum class WindowsOutputTransition : std::uint8_t {
    Down,
    Up,
};

struct WindowsOutputRecipe final {
    WindowsOutputKind kind{WindowsOutputKind::None};
    WindowsVirtualKey virtualKey{};
    WindowsScanCode scanCode{};
    std::uint32_t mouseDownFlags{};
    std::uint32_t mouseUpFlags{};
    WindowsMouseData mouseData{};
    bool extendedScanCode{};
};

inline constexpr std::size_t kWindowsOutputQueueCapacity = 8192U;

struct WindowsOutputItem final {
    std::uint64_t sourceSequence{};
    std::uint64_t outputStateGeneration{};
    WindowsVirtualKey outputCode{};
    bool requiresPointerTarget{};
    WindowsOutputRecipe recipe{};
    WindowsOutputTransition transition{WindowsOutputTransition::Down};
};

} // namespace inputweaver
