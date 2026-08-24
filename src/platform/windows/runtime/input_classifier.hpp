#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "input/input_types.hpp"

namespace inputweaver {

[[nodiscard]] inline InputOrigin ClassifyKeyboard(
    const KBDLLHOOKSTRUCT& event,
    SelfTag selfTag) noexcept
{
    if ((event.flags & LLKHF_INJECTED) == 0) {
        return InputOrigin::PhysicalCandidate;
    }

    return static_cast<SelfTag>(event.dwExtraInfo) == selfTag
        ? InputOrigin::SelfInjected
        : InputOrigin::ExternalInjected;
}

[[nodiscard]] inline InputOrigin ClassifyMouse(
    const MSLLHOOKSTRUCT& event,
    SelfTag selfTag) noexcept
{
    if ((event.flags & LLMHF_INJECTED) == 0) {
        return InputOrigin::PhysicalCandidate;
    }

    return static_cast<SelfTag>(event.dwExtraInfo) == selfTag
        ? InputOrigin::SelfInjected
        : InputOrigin::ExternalInjected;
}

} // namespace inputweaver
