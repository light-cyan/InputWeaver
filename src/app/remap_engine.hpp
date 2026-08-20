#pragma once

#include "core/fixed_rules.hpp"
#include "core/stop_request.hpp"
#include "diagnostics/diagnostic_log.hpp"
#include "platform/windows/low_level_hooks.hpp"

#include <atomic>
#include <cstdint>

namespace inputweaver {

class ActionScheduler;
struct RuntimeTestAccess;

struct RemapEngineOptions final {
    bool mappingMode{};
    bool traceInput{};
    SelfTag selfTag{};
};

struct RemapEngineMetrics final {
    std::uint64_t hookEvents{};
    std::uint64_t suppressedEvents{};
    std::uint64_t maximumHookMicroseconds{};
};

class RemapEngine final : public LowLevelInputSink {
public:
    RemapEngine(
        RemapEngineOptions options,
        TargetProcessContext* targetContext,
        DiagnosticLog& diagnosticLog,
        StopRequest stopRequest,
        std::atomic<bool>& shutdownRequested) noexcept;

    void AttachScheduler(ActionScheduler& scheduler) noexcept;
    void DisableNewCaptures() noexcept;
    [[nodiscard]] bool CanInject(const ActionBatch& batch) const noexcept;
    [[nodiscard]] bool NewCapturesEnabled() const noexcept;
    [[nodiscard]] RemapEngineMetrics Metrics() const noexcept;

    InputDecision HandleInput(
        const InputEvent& event,
        bool lowerIntegrityInjected,
        std::int64_t startCounter) noexcept override;
    void SeedPhysicalState(DeviceKind device, ControlCode code, bool down) noexcept override;
    [[nodiscard]] bool HasCapturedInputs() const noexcept override;
    void FlushDiagnostics(std::int64_t startCounter) noexcept override;

private:
    friend struct RuntimeTestAccess;

    struct CommitContext final {
        FixedRuleEngine* rules{};
        const RuleEvaluation* evaluation{};
    };

    static bool CommitCapture(void* context) noexcept;
    void PublishHookDiagnostic(
        HookDiagnosticRecord& record,
        std::int64_t startCounter) noexcept;
    void AccumulateMouseMoveDiagnostic(
        HookDiagnosticRecord record,
        std::int64_t startCounter) noexcept;
    void RecordMaximumHookDuration(std::int64_t startCounter) noexcept;
    [[nodiscard]] ProcessId TargetPid() const noexcept;
    [[nodiscard]] bool TargetPointerRouteIsSafe(
        const InputEvent& source,
        const ActionBatch& batch) const noexcept;

    RemapEngineOptions options_;
    TargetProcessContext* targetContext_;
    DiagnosticLog& diagnosticLog_;
    StopRequest stopRequest_;
    std::atomic<bool>& shutdownRequested_;
    ActionScheduler* scheduler_{nullptr};
    FixedRuleEngine rules_;
    std::uint64_t nextSequence_{1};
    std::int64_t performanceFrequency_{1};
    HookDiagnosticRecord pendingMouseMoveRecord_{};
    std::uint32_t pendingMouseMoveCount_{0};
    std::atomic<std::uint64_t> hookEvents_{0};
    std::atomic<std::uint64_t> suppressedEvents_{0};
    std::atomic<std::uint64_t> maximumHookMicroseconds_{0};
};

}  // namespace inputweaver
