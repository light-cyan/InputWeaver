#pragma once

#include "platform/windows/runtime/process_context.hpp"
#include "runtime/runtime_types.hpp"

#include <mutex>

namespace inputweaver::win32 {

class WindowsRuntimeRoutePort final : public RuntimeRoutePort {
public:
    explicit WindowsRuntimeRoutePort(
        TargetProcessContext* targetContext,
        const ForegroundProcessExclusion* processExclusion = nullptr,
        std::mutex* targetContextMutex = nullptr) noexcept;

    [[nodiscard]] bool ValidateTarget(
        TargetSelectorKind kind) noexcept override;
    [[nodiscard]] bool TargetValid(
        TargetSelectorKind kind) noexcept override;
    [[nodiscard]] bool CanDispatch(
        TargetSelectorKind kind,
        const RuntimeInputEvent& event) noexcept override;
    [[nodiscard]] bool CanInject(
        TargetSelectorKind kind,
        const ActivatedControl& control) noexcept override;

private:
    TargetProcessContext* targetContext_;
    const ForegroundProcessExclusion* processExclusion_;
    std::mutex* targetContextMutex_;
};

} // namespace inputweaver::win32
