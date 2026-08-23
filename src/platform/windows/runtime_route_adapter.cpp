#include "runtime_route_adapter.hpp"

namespace inputweaver::win32 {

WindowsRuntimeRoutePort::WindowsRuntimeRoutePort(
    TargetProcessContext* targetContext) noexcept
    : targetContext_(targetContext)
{
}

bool WindowsRuntimeRoutePort::ValidateTarget(
    TargetSelectorKind kind,
    std::string_view selector) noexcept
{
    if (kind == TargetSelectorKind::Global) {
        return selector.empty();
    }
    if (kind == TargetSelectorKind::Executable) {
        return !selector.empty()
            && targetContext_ != nullptr
            && targetContext_->IsValid();
    }
    return false;
}

bool WindowsRuntimeRoutePort::TargetValid(
    TargetSelectorKind kind) noexcept
{
    if (kind == TargetSelectorKind::Global) {
        return true;
    }
    return kind == TargetSelectorKind::Executable
        && targetContext_ != nullptr
        && targetContext_->IsTargetAlive();
}

bool WindowsRuntimeRoutePort::CanDispatch(
    TargetSelectorKind kind,
    const RuntimeInputEvent& event) noexcept
{
    if (kind == TargetSelectorKind::Global) {
        return true;
    }
    if (!TargetValid(kind) || !targetContext_->IsTargetForeground()) {
        return false;
    }
    return event.device != DeviceKind::Mouse
        || targetContext_->IsTargetPointerTarget(event.position);
}

bool WindowsRuntimeRoutePort::CanInject(
    TargetSelectorKind kind,
    const ActivatedControl& control) noexcept
{
    if (kind == TargetSelectorKind::Global) {
        return true;
    }
    if (!TargetValid(kind) || !targetContext_->IsTargetForeground()) {
        return false;
    }
    return !control.requiresPointerTarget
        || targetContext_->IsTargetPointerTargetAtCursor();
}

} // namespace inputweaver::win32
