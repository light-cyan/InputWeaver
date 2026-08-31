#pragma once

#include "expression_vm.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace inputweaver {

class ProgramRuntime final {
public:
    ProgramRuntime(
        RuntimeCapacities capacities,
        RuntimeControlPort& controlPort,
        RuntimeOutputPort& outputPort,
        RuntimeRoutePort& routePort,
        RuntimeProcessLauncher& processLauncher,
        RuntimeClock& clock,
        RuntimeDebugEventPort* debugPort = nullptr,
        support::CallbackRef<void() noexcept> fatalStopRequest = {});
    ~ProgramRuntime();

    ProgramRuntime(const ProgramRuntime&) = delete;
    ProgramRuntime& operator=(const ProgramRuntime&) = delete;

    [[nodiscard]] RuntimeActivationResult Activate(
        std::shared_ptr<const CompiledProgram> program,
        TargetSelectorKind targetKindOverride =
            TargetSelectorKind::Unspecified);
    void Deactivate() noexcept;

    [[nodiscard]] InputDecision HandleInput(
        const RuntimeInputEvent& event) noexcept;
    [[nodiscard]] bool SeedPhysicalState(
        ControlRefId control,
        bool down) noexcept;
    [[nodiscard]] bool MarkPhysicalStateUnsynchronized(
        ControlRefId control) noexcept;
    void SetTargetEligible(bool eligible) noexcept;
    [[nodiscard]] RuntimePumpResult Pump(
        std::size_t maximumSlices = 1024U) noexcept;

    [[nodiscard]] bool StartTaskThread();
    void StopTaskThread() noexcept;
    void NotifyTargetLost() noexcept;
    void RequestShutdown() noexcept;

    [[nodiscard]] bool HasActiveProgram() const noexcept;
    [[nodiscard]] bool TargetEligible() const noexcept;
    [[nodiscard]] bool PauseOn() const noexcept;
    [[nodiscard]] bool ExitRequested() const noexcept;
    [[nodiscard]] bool FatalShutdownRequested() const noexcept;
    [[nodiscard]] std::uint64_t Generation() const noexcept;
    [[nodiscard]] std::size_t ActiveTaskCount() const noexcept;
    [[nodiscard]] bool HasActiveMappings() const noexcept;
    [[nodiscard]] bool HasOwnedOutputs() const noexcept;
    [[nodiscard]] RuntimeMetrics Metrics() const noexcept;

    [[nodiscard]] RuntimeEvaluationResult EvaluateExpression(
        ExpressionId expression) noexcept;
    [[nodiscard]] bool ReadUserState(
        std::uint32_t index,
        bool& value) const noexcept;

    [[nodiscard]] bool TryPopDiagnostic(
        RuntimeDiagnosticRecord& record) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace inputweaver
