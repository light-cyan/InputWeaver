#include "input_injector.hpp"

#include <algorithm>
#include <cstddef>

namespace inputweaver {
namespace {

[[nodiscard]] bool BuildKeyboardInput(
    const Action& action,
    SelfTag selfTag,
    INPUT& input) noexcept {
    if (action.transition != Transition::Down &&
        action.transition != Transition::Up) {
        return false;
    }
    if (action.code == 0 || action.code > 0xFFU) {
        return false;
    }

    const UINT virtualKey = static_cast<UINT>(action.code);
    const UINT mappedScanCode =
        MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX);
    if (mappedScanCode == 0) {
        return false;
    }

    input.type = INPUT_KEYBOARD;
    input.ki.dwExtraInfo = static_cast<ULONG_PTR>(selfTag);

    const auto isExtendedVirtualKey = [](DWORD code) noexcept {
        switch (code) {
            case VK_RCONTROL:
            case VK_RMENU:
            case VK_INSERT:
            case VK_DELETE:
            case VK_HOME:
            case VK_END:
            case VK_PRIOR:
            case VK_NEXT:
            case VK_LEFT:
            case VK_UP:
            case VK_RIGHT:
            case VK_DOWN:
            case VK_NUMLOCK:
            case VK_CANCEL:
            case VK_SNAPSHOT:
            case VK_DIVIDE:
            case VK_LWIN:
            case VK_RWIN:
            case VK_APPS:
                return true;
            default:
                return false;
        }
    };

    const UINT prefix = mappedScanCode & 0xFF00U;
    if (prefix == 0 || prefix == 0xE000U) {
        input.ki.wVk = 0;
        input.ki.wScan = static_cast<WORD>(mappedScanCode & 0x00FFU);
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        if (prefix == 0xE000U || isExtendedVirtualKey(action.code)) {
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
    } else if (prefix == 0xE100U) {
        input.ki.wVk = static_cast<WORD>(virtualKey);
        input.ki.wScan = 0;
    } else {
        return false;
    }

    if (action.transition == Transition::Up) {
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    return true;
}

[[nodiscard]] bool GetMouseButtonFlags(
    ControlCode code,
    Transition transition,
    DWORD& flags,
    DWORD& mouseData) noexcept {
    const bool isDown = transition == Transition::Down;
    const bool isUp = transition == Transition::Up;
    if (!isDown && !isUp) {
        return false;
    }

    mouseData = 0;
    switch (code) {
        case control::kMouseLeft:
            flags = isDown ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            return true;
        case control::kMouseRight:
            flags = isDown ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            return true;
        case control::kMouseMiddle:
            flags = isDown ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            return true;
        case control::kMouseX1:
            flags = isDown ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            mouseData = XBUTTON1;
            return true;
        case control::kMouseX2:
            flags = isDown ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            mouseData = XBUTTON2;
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool BuildMouseInput(
    const Action& action,
    SelfTag selfTag,
    INPUT& input) noexcept {
    input.type = INPUT_MOUSE;
    input.mi.dwExtraInfo = static_cast<ULONG_PTR>(selfTag);

    switch (action.transition) {
        case Transition::Down:
        case Transition::Up:
            return GetMouseButtonFlags(
                action.code,
                action.transition,
                input.mi.dwFlags,
                input.mi.mouseData);
        case Transition::Move:
            if (action.code != 0) {
                return false;
            }
            input.mi.dx = static_cast<LONG>(action.valueX);
            input.mi.dy = static_cast<LONG>(action.valueY);
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            return true;
        case Transition::VerticalWheel:
            if (action.code != 0) {
                return false;
            }
            input.mi.mouseData = static_cast<DWORD>(action.valueY);
            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
            return true;
        case Transition::HorizontalWheel:
            if (action.code != 0) {
                return false;
            }
            input.mi.mouseData = static_cast<DWORD>(action.valueX);
            input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
            return true;
    }
    return false;
}

[[nodiscard]] bool BuildInput(
    const Action& action,
    SelfTag selfTag,
    INPUT& input) noexcept {
    switch (action.device) {
        case DeviceKind::Keyboard:
            return BuildKeyboardInput(action, selfTag, input);
        case DeviceKind::Mouse:
            return BuildMouseInput(action, selfTag, input);
    }
    return false;
}

[[nodiscard]] bool IsStateDownInput(const INPUT& input) noexcept {
    if (input.type == INPUT_KEYBOARD) {
        return (input.ki.dwFlags & KEYEVENTF_KEYUP) == 0;
    }
    if (input.type != INPUT_MOUSE) {
        return false;
    }
    constexpr DWORD downFlags =
        MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_RIGHTDOWN |
        MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_XDOWN;
    return (input.mi.dwFlags & downFlags) != 0;
}

[[nodiscard]] bool IsMatchingRelease(
    const INPUT& down,
    const INPUT& candidate) noexcept {
    if (down.type != candidate.type) {
        return false;
    }
    if (down.type == INPUT_KEYBOARD) {
        if ((candidate.ki.dwFlags & KEYEVENTF_KEYUP) == 0) {
            return false;
        }
        constexpr DWORD identityFlags =
            KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY;
        return down.ki.wVk == candidate.ki.wVk &&
               down.ki.wScan == candidate.ki.wScan &&
               (down.ki.dwFlags & identityFlags) ==
                   (candidate.ki.dwFlags & identityFlags);
    }
    if (down.type != INPUT_MOUSE) {
        return false;
    }

    if ((down.mi.dwFlags & MOUSEEVENTF_LEFTDOWN) != 0) {
        return (candidate.mi.dwFlags & MOUSEEVENTF_LEFTUP) != 0;
    }
    if ((down.mi.dwFlags & MOUSEEVENTF_RIGHTDOWN) != 0) {
        return (candidate.mi.dwFlags & MOUSEEVENTF_RIGHTUP) != 0;
    }
    if ((down.mi.dwFlags & MOUSEEVENTF_MIDDLEDOWN) != 0) {
        return (candidate.mi.dwFlags & MOUSEEVENTF_MIDDLEUP) != 0;
    }
    if ((down.mi.dwFlags & MOUSEEVENTF_XDOWN) != 0) {
        return (candidate.mi.dwFlags & MOUSEEVENTF_XUP) != 0 &&
               down.mi.mouseData == candidate.mi.mouseData;
    }
    return false;
}

[[nodiscard]] INPUT BuildReleaseInput(const INPUT& down) noexcept {
    INPUT release = down;
    if (release.type == INPUT_KEYBOARD) {
        release.ki.dwFlags |= KEYEVENTF_KEYUP;
        return release;
    }

    DWORD flags = release.mi.dwFlags;
    if ((flags & MOUSEEVENTF_LEFTDOWN) != 0) {
        flags = (flags & ~static_cast<DWORD>(MOUSEEVENTF_LEFTDOWN)) |
            MOUSEEVENTF_LEFTUP;
    } else if ((flags & MOUSEEVENTF_RIGHTDOWN) != 0) {
        flags = (flags & ~static_cast<DWORD>(MOUSEEVENTF_RIGHTDOWN)) |
            MOUSEEVENTF_RIGHTUP;
    } else if ((flags & MOUSEEVENTF_MIDDLEDOWN) != 0) {
        flags = (flags & ~static_cast<DWORD>(MOUSEEVENTF_MIDDLEDOWN)) |
            MOUSEEVENTF_MIDDLEUP;
    } else if ((flags & MOUSEEVENTF_XDOWN) != 0) {
        flags = (flags & ~static_cast<DWORD>(MOUSEEVENTF_XDOWN)) |
            MOUSEEVENTF_XUP;
    }
    release.mi.dwFlags = flags;
    return release;
}

[[nodiscard]] UINT BuildCleanupInputs(
    const PreparedInputBatch& prepared,
    UINT sent,
    std::array<INPUT, kMaximumPreparedInputs>& cleanup) noexcept {
    const UINT prefixCount = (std::min)(sent, prepared.count);
    std::array<bool, kMaximumPreparedInputs> usedReleases{};
    UINT cleanupCount = 0;

    for (UINT downIndex = 0; downIndex < prefixCount; ++downIndex) {
        const INPUT& down = prepared.inputs[downIndex];
        if (!IsStateDownInput(down)) {
            continue;
        }

        bool matched = false;
        for (UINT releaseIndex = downIndex + 1U;
             releaseIndex < prefixCount;
             ++releaseIndex) {
            if (!usedReleases[releaseIndex] &&
                IsMatchingRelease(down, prepared.inputs[releaseIndex])) {
                usedReleases[releaseIndex] = true;
                matched = true;
                break;
            }
        }
        if (!matched) {
            cleanup[cleanupCount++] = BuildReleaseInput(down);
        }
    }

    std::reverse(
        cleanup.begin(),
        cleanup.begin() + static_cast<std::ptrdiff_t>(cleanupCount));
    return cleanupCount;
}

}  // namespace

InputInjector::InputInjector(
    inputweaver::SelfTag selfTag,
    SendInputFunction sendInput) noexcept
    : selfTag_(selfTag), sendInput_(sendInput) {}

inputweaver::SelfTag InputInjector::Tag() const noexcept {
    return selfTag_;
}

PreparedInputBatch InputInjector::Prepare(const ActionBatch& batch) const noexcept {
    PreparedInputBatch prepared{};
    if (selfTag_ == 0) {
        prepared.error = ERROR_INVALID_PARAMETER;
        return prepared;
    }
    if (batch.actionCount == 0 ||
        batch.actionCount > batch.actions.size() ||
        batch.actionCount > kMaximumPreparedInputs) {
        prepared.error = ERROR_INVALID_DATA;
        return prepared;
    }

    for (std::size_t index = 0; index < batch.actionCount; ++index) {
        if (!BuildInput(batch.actions[index], selfTag_, prepared.inputs[index])) {
            prepared.error = ERROR_INVALID_DATA;
            prepared.count = 0;
            return prepared;
        }
    }

    prepared.count = static_cast<UINT>(batch.actionCount);
    return prepared;
}

InjectionResult InputInjector::Inject(const ActionBatch& batch) const noexcept {
    InjectionResult result{};
    if (selfTag_ == 0) {
        result.outcome = InjectionOutcome::InvalidSelfTag;
        result.error = ERROR_INVALID_PARAMETER;
        return result;
    }
    if (batch.actionCount == 0 ||
        batch.actionCount > batch.actions.size() ||
        batch.actionCount > kMaximumPreparedInputs ||
        sendInput_ == nullptr) {
        result.outcome = InjectionOutcome::InvalidBatch;
        result.error = ERROR_INVALID_DATA;
        return result;
    }

    PreparedInputBatch prepared = Prepare(batch);
    if (!prepared.Succeeded()) {
        result.outcome = InjectionOutcome::ConversionFailed;
        result.error = prepared.error;
        return result;
    }

    result.requested = prepared.count;
    SetLastError(ERROR_SUCCESS);
    result.sent = sendInput_(
        prepared.count,
        prepared.inputs.data(),
        static_cast<int>(sizeof(INPUT)));
    if (result.sent == result.requested) {
        result.outcome = InjectionOutcome::Succeeded;
        return result;
    }

    result.error = GetLastError();
    result.outcome = result.sent == 0
        ? InjectionOutcome::SendFailed
        : InjectionOutcome::SendPartial;

    std::array<INPUT, kMaximumPreparedInputs> cleanup{};
    result.cleanupRequested =
        BuildCleanupInputs(prepared, result.sent, cleanup);
    if (result.cleanupRequested == 0) {
        return result;
    }

    result.cleanupAttempted = true;
    SetLastError(ERROR_SUCCESS);
    result.cleanupSent = sendInput_(
        result.cleanupRequested,
        cleanup.data(),
        static_cast<int>(sizeof(INPUT)));
    if (result.cleanupSent != result.cleanupRequested) {
        result.cleanupError = GetLastError();
    }
    return result;
}

}  // namespace inputweaver
