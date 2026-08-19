#pragma once

#include "core/input_event.hpp"

namespace ukr {

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

} // namespace ukr
