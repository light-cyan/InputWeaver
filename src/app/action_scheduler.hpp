#pragma once

#include "core/action_queue.hpp"
#include "core/stop_request.hpp"
#include "diagnostics/diagnostic_log.hpp"
#include "platform/windows/input_injector.hpp"
#include "platform/windows/process_context.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

#include <windows.h>

namespace ukr {

class RemapEngine;
class AppRuntime;
struct RuntimeTestAccess;

inline constexpr unsigned int kInjectionFailureThreshold = 3;

struct CaptureCommit final {
    void* context{};
    bool (*invoke)(void*) noexcept{};

    [[nodiscard]] bool Commit() const noexcept {
        return invoke != nullptr && invoke(context);
    }
};

struct ActionSchedulerMetrics final {
    std::uint64_t queuedBatches{};
    std::uint64_t cancelledBatches{};
    std::uint64_t rejectedActionPushes{};
    std::uint64_t injectionFailures{};
    std::uint64_t unresolvedSyntheticReleases{};
    bool circuitBreakerOpen{};
};

class ActionScheduler final {
public:
    ActionScheduler(
        SelfTag selfTag,
        TargetProcessContext* targetContext,
        DiagnosticLog& diagnosticLog,
        RemapEngine& remapEngine,
        StopRequest stopRequest,
        HANDLE shutdownEvent) noexcept;
    ~ActionScheduler();

    ActionScheduler(const ActionScheduler&) = delete;
    ActionScheduler& operator=(const ActionScheduler&) = delete;

    bool Start(std::wstring& errorMessage);
    void Wait() noexcept;
    void NotifyProducerDone() noexcept;

    [[nodiscard]] ActionQueuePushResult TrySchedule(
        const ActionBatch& batch,
        CaptureCommit captureCommit) noexcept;
    [[nodiscard]] bool CircuitBreakerOpen() const noexcept;
    [[nodiscard]] HANDLE ProducerDoneEvent() const noexcept;
    [[nodiscard]] HANDLE ActionQueueErrorEvent() const noexcept;
    [[nodiscard]] ActionSchedulerMetrics Metrics() const noexcept;

private:
    friend class AppRuntime;
    friend struct RuntimeTestAccess;

    enum class CancellationReason : std::uint8_t {
        Target,
        CircuitBreaker,
        Shutdown
    };

    bool CreateEvents(std::wstring& errorMessage) noexcept;
    void CloseEvents() noexcept;
    void WorkerMain() noexcept;
    void ProcessActionBatch(const ActionBatch& batch) noexcept;
    void ExecuteEligibleActionBatch(
        const ActionBatch& batch,
        InjectionDiagnosticRecord& record) noexcept;
    void RecordCancelledBatch(
        const ActionBatch& batch,
        CancellationReason reason) noexcept;
    void CancelQueuedActions(CancellationReason reason) noexcept;
    void DrainForShutdown() noexcept;
    void RememberOwnedRelease(const ActionBatch& batch) noexcept;
    void ForgetOwnedRelease(DeviceKind device, ControlCode code) noexcept;
    void ReleaseOwnedSyntheticState(unsigned int maximumAttempts) noexcept;
    void OpenCircuit() noexcept;
    [[nodiscard]] ProcessId TargetPid() const noexcept;

    SelfTag selfTag_;
    TargetProcessContext* targetContext_;
    DiagnosticLog& diagnosticLog_;
    RemapEngine& remapEngine_;
    StopRequest stopRequest_;
    HANDLE shutdownEvent_;
    InputInjector injector_;
    InjectionCircuitBreaker injectionCircuitBreaker_{kInjectionFailureThreshold};
    ActionQueue actionQueue_;

    HANDLE readyEvent_{nullptr};
    HANDLE producerDoneEvent_{nullptr};
    HANDLE actionEvent_{nullptr};
    HANDLE actionQueueErrorEvent_{nullptr};
    std::thread workerThread_;
    std::atomic<bool> started_{false};
    std::atomic<bool> circuitBreakerOpen_{false};
    std::atomic<bool> actionQueueErrorReported_{false};
    std::array<Action, kMaxActionsPerBatch> ownedSyntheticReleases_{};
    std::size_t ownedSyntheticReleaseCount_{0};

    std::atomic<std::uint64_t> queuedBatches_{0};
    std::atomic<std::uint64_t> cancelledBatches_{0};
    std::atomic<std::uint64_t> injectionFailures_{0};
    std::atomic<std::uint64_t> unresolvedSyntheticReleases_{0};
};

}  // namespace ukr
