#pragma once

#include "platform/windows/runtime/process_context.hpp"
#include "runtime/runtime_types.hpp"

namespace inputweaver::win32 {

class WindowsRuntimeRoutePort final : public RuntimeRoutePort {
public:
    explicit WindowsRuntimeRoutePort(
        TargetProcessContext* targetContext,
        WindowsProcessId excludedProcessId = 0U) noexcept;

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
    [[nodiscard]] bool ExcludedProcessForeground() const noexcept;

    TargetProcessContext* targetContext_;
    WindowsProcessId excludedProcessId_;
};

} // namespace inputweaver::win32
