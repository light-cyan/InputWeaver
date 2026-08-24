#pragma once

#include <cstdint>

namespace inputweaver {

using InputCoordinate = std::int32_t;

struct ScreenPoint final {
    InputCoordinate x{};
    InputCoordinate y{};
};

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

} // namespace inputweaver
