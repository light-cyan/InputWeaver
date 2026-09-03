#include "runtime_route_adapter.hpp"

namespace inputweaver::win32 {

WindowsRuntimeRoutePort::WindowsRuntimeRoutePort(
    TargetProcessContext* targetContext,
    const ForegroundProcessExclusion* processExclusion,
    std::mutex* targetContextMutex) noexcept
    : targetContext_(targetContext),
      processExclusion_(processExclusion),
      targetContextMutex_(targetContextMutex)
{
}

bool WindowsRuntimeRoutePort::ValidateTarget(
    TargetSelectorKind kind) noexcept
{
    if (kind == TargetSelectorKind::Global) {
        return targetContext_ == nullptr;
    }
    if (kind == TargetSelectorKind::Executable) {
        return targetContext_ != nullptr;
    }
    return false;
}

bool WindowsRuntimeRoutePort::TargetValid(
    TargetSelectorKind kind) noexcept
{
    if (kind == TargetSelectorKind::Global) {
        return true;
    }
    std::unique_lock<std::mutex> targetLock;
    if (targetContextMutex_ != nullptr) {
        targetLock = std::unique_lock<std::mutex>(*targetContextMutex_);
    }
    return kind == TargetSelectorKind::Executable
        && targetContext_ != nullptr
        && targetContext_->IsTargetAlive();
}

bool WindowsRuntimeRoutePort::CanDispatch(
    TargetSelectorKind kind,
    const RuntimeInputEvent& event) noexcept
{
    if (processExclusion_ != nullptr
        && processExclusion_->IsForegroundExcluded()) {
        return false;
    }
    if (kind == TargetSelectorKind::Global) {
        return true;
    }
    std::unique_lock<std::mutex> targetLock;
    if (targetContextMutex_ != nullptr) {
        targetLock = std::unique_lock<std::mutex>(*targetContextMutex_);
    }
    if (kind != TargetSelectorKind::Executable
        || targetContext_ == nullptr
        || !targetContext_->IsTargetAlive()
        || !targetContext_->IsTargetForeground()) {
        return false;
    }
    return event.device != DeviceKind::Mouse
        || targetContext_->IsTargetPointerTarget(event.position);
}

bool WindowsRuntimeRoutePort::CanInject(
    TargetSelectorKind kind,
    const ActivatedControl& control) noexcept
{
    if (processExclusion_ != nullptr
        && processExclusion_->IsForegroundExcluded()) {
        return false;
    }
    if (kind == TargetSelectorKind::Global) {
        return true;
    }
    std::unique_lock<std::mutex> targetLock;
    if (targetContextMutex_ != nullptr) {
        targetLock = std::unique_lock<std::mutex>(*targetContextMutex_);
    }
    if (kind != TargetSelectorKind::Executable
        || targetContext_ == nullptr
        || !targetContext_->IsTargetAlive()
        || !targetContext_->IsTargetForeground()) {
        return false;
    }
    return !control.requiresPointerTarget
        || targetContext_->IsTargetPointerTargetAtCursor();
}

} // namespace inputweaver::win32
