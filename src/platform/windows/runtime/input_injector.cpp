#include "input_injector.hpp"

namespace inputweaver {
namespace {

[[nodiscard]] bool IsValidOutputTransition(
    WindowsOutputTransition transition) noexcept {
    return transition == WindowsOutputTransition::Down
        || transition == WindowsOutputTransition::Up;
}

[[nodiscard]] bool BuildKeyboardInput(
    const WindowsOutputItem& item,
    WindowsSelfTag selfTag,
    INPUT& input) noexcept {
    if (!IsValidOutputTransition(item.transition)) {
        return false;
    }
    input.type = INPUT_KEYBOARD;
    input.ki.dwExtraInfo = static_cast<ULONG_PTR>(selfTag);
    if (item.recipe.kind == WindowsOutputKind::KeyboardVirtualKey) {
        if (item.recipe.virtualKey == 0U
            || item.recipe.virtualKey > 0xffU) {
            return false;
        }
        input.ki.wVk = static_cast<WORD>(item.recipe.virtualKey);
        input.ki.wScan = 0;
        input.ki.dwFlags = item.transition == WindowsOutputTransition::Up
            ? KEYEVENTF_KEYUP
            : 0U;
        return true;
    }
    if (item.recipe.kind != WindowsOutputKind::KeyboardScanCode
        || item.recipe.scanCode == 0U
        || item.recipe.scanCode > 0xffU) {
        return false;
    }
    input.ki.wVk = 0;
    input.ki.wScan = static_cast<WORD>(item.recipe.scanCode);
    input.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (item.recipe.extendedScanCode) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    if (item.transition == WindowsOutputTransition::Up) {
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    return true;
}

[[nodiscard]] bool BuildMouseInput(
    const WindowsOutputItem& item,
    WindowsSelfTag selfTag,
    INPUT& input) noexcept {
    if (item.recipe.kind != WindowsOutputKind::MouseButton
        || !IsValidOutputTransition(item.transition)) {
        return false;
    }
    input.type = INPUT_MOUSE;
    input.mi.dwExtraInfo = static_cast<ULONG_PTR>(selfTag);
    input.mi.dwFlags = item.transition == WindowsOutputTransition::Down
        ? item.recipe.mouseDownFlags
        : item.recipe.mouseUpFlags;
    input.mi.mouseData = item.recipe.mouseData;
    return input.mi.dwFlags != 0U;
}

[[nodiscard]] bool BuildInput(
    const WindowsOutputItem& item,
    WindowsSelfTag selfTag,
    INPUT& input) noexcept {
    switch (item.recipe.kind) {
        case WindowsOutputKind::KeyboardVirtualKey:
        case WindowsOutputKind::KeyboardScanCode:
            return BuildKeyboardInput(item, selfTag, input);
        case WindowsOutputKind::MouseButton:
            return BuildMouseInput(item, selfTag, input);
        case WindowsOutputKind::None:
        case WindowsOutputKind::Pointer:
            return false;
    }
    return false;
}

}  // namespace

InputInjector::InputInjector(
    WindowsSelfTag selfTag,
    SendInputFunction sendInput,
    bool dryRun) noexcept
    : selfTag_(selfTag), sendInput_(sendInput), dryRun_(dryRun) {}

PreparedInput InputInjector::Prepare(
    const WindowsOutputItem& item) const noexcept {
    PreparedInput prepared{};
    if (selfTag_ == 0) {
        prepared.error = ERROR_INVALID_PARAMETER;
        return prepared;
    }
    if (!BuildInput(item, selfTag_, prepared.input)) {
        return prepared;
    }
    prepared.error = ERROR_SUCCESS;
    return prepared;
}

InjectionResult InputInjector::Inject(
    const WindowsOutputItem& item) const noexcept {
    return InjectPrepared(Prepare(item));
}

InjectionResult InputInjector::InjectPrepared(PreparedInput prepared) const noexcept {
    InjectionResult result{};
    if (selfTag_ == 0) {
        result.outcome = InjectionOutcome::InvalidSelfTag;
        result.error = ERROR_INVALID_PARAMETER;
        return result;
    }
    if (!prepared.Succeeded()) {
        result.outcome = InjectionOutcome::ConversionFailed;
        result.error = prepared.error;
        return result;
    }
    if (dryRun_ || !prepared.emit) {
        result.outcome = InjectionOutcome::Succeeded;
        return result;
    }
    if (sendInput_ == nullptr) {
        result.outcome = InjectionOutcome::InvalidOutput;
        result.error = ERROR_INVALID_DATA;
        return result;
    }

    if (prepared.input.type == INPUT_MOUSE) prepared.input.mi.dwExtraInfo = static_cast<ULONG_PTR>(selfTag_);
    result.requested = 1U;
    SetLastError(ERROR_SUCCESS);
    result.sent = sendInput_(
        1U,
        &prepared.input,
        static_cast<int>(sizeof(INPUT)));
    if (result.sent == result.requested) {
        result.outcome = InjectionOutcome::Succeeded;
        return result;
    }

    result.error = GetLastError();
    result.outcome = InjectionOutcome::SendFailed;
    return result;
}

}  // namespace inputweaver
