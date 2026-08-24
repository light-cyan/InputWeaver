#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "windows_input_types.hpp"

namespace inputweaver {

[[nodiscard]] inline InputOrigin ClassifyKeyboard(
    const KBDLLHOOKSTRUCT& event,
    WindowsSelfTag selfTag) noexcept
{
    if ((event.flags & LLKHF_INJECTED) == 0) {
        return InputOrigin::PhysicalCandidate;
    }

    return static_cast<WindowsSelfTag>(event.dwExtraInfo) == selfTag
        ? InputOrigin::SelfInjected
        : InputOrigin::ExternalInjected;
}

[[nodiscard]] inline InputOrigin ClassifyMouse(
    const MSLLHOOKSTRUCT& event,
    WindowsSelfTag selfTag) noexcept
{
    if ((event.flags & LLMHF_INJECTED) == 0) {
        return InputOrigin::PhysicalCandidate;
    }

    return static_cast<WindowsSelfTag>(event.dwExtraInfo) == selfTag
        ? InputOrigin::SelfInjected
        : InputOrigin::ExternalInjected;
}

} // namespace inputweaver
