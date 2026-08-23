#pragma once

#include "platform/windows/process_context.hpp"
#include "runtime/runtime_types.hpp"

namespace inputweaver::win32 {

class WindowsRuntimeRoutePort final : public RuntimeRoutePort {
public:
    explicit WindowsRuntimeRoutePort(
        TargetProcessContext* targetContext) noexcept;

    [[nodiscard]] bool ValidateTarget(
        TargetSelectorKind kind,
        std::string_view selector) noexcept override;
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
};

} // namespace inputweaver::win32
