#pragma once

#include "core/action_queue.hpp"
#include "core/fixed_rules.hpp"
#include "diagnostics/diagnostic_log.hpp"
#include "input_injector.hpp"
#include "process_context.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include <windows.h>

namespace ukr {

inline constexpr DWORD kCapturedReleaseGraceMilliseconds = 2000;
inline constexpr unsigned int kInjectionFailureThreshold = 3;

class ShutdownGraceWindow final {
public:
    explicit constexpr ShutdownGraceWindow(DWORD durationMilliseconds) noexcept
        : durationMilliseconds_(durationMilliseconds) {}

    constexpr void Begin(std::uint64_t nowMilliseconds) noexcept {
        deadlineMilliseconds_ = nowMilliseconds + durationMilliseconds_;
        active_ = true;
    }

    [[nodiscard]] constexpr bool Expired(std::uint64_t nowMilliseconds) const noexcept {
        return active_ && nowMilliseconds >= deadlineMilliseconds_;
    }

    [[nodiscard]] constexpr DWORD RemainingSlice(
        std::uint64_t nowMilliseconds,
        DWORD maximumSliceMilliseconds) const noexcept {
        if (!active_ || nowMilliseconds >= deadlineMilliseconds_) {
            return 0;
        }
        const std::uint64_t remaining = deadlineMilliseconds_ - nowMilliseconds;
        return static_cast<DWORD>((std::min)(
            remaining, static_cast<std::uint64_t>(maximumSliceMilliseconds)));
    }

private:
    DWORD durationMilliseconds_;
    std::uint64_t deadlineMilliseconds_{0};
    bool active_{false};
};

struct HookRuntimeOptions {
    bool mappingMode{};
    bool traceInput{};
    SelfTag selfTag{};
};

struct HookRuntimeMetrics {
    std::uint64_t hookEvents{};
    std::uint64_t suppressedEvents{};
    std::uint64_t queuedBatches{};
    std::uint64_t cancelledBatches{};
    std::uint64_t rejectedActionPushes{};
    std::uint64_t injectionFailures{};
    std::uint64_t maximumHookMicroseconds{};
    std::uint64_t unresolvedSyntheticReleases{};
    bool circuitBreakerOpen{};
};

struct HookRuntimeTestAccess;

class HookRuntime final {
public:
    HookRuntime(
        HookRuntimeOptions options,
        TargetProcessContext* targetContext,
        DiagnosticLog& diagnosticLog) noexcept;
    ~HookRuntime();

    HookRuntime(const HookRuntime&) = delete;
    HookRuntime& operator=(const HookRuntime&) = delete;

    bool Start(std::wstring& errorMessage);
    void RequestStop() noexcept;
    void Wait() noexcept;

    HANDLE StoppedEvent() const noexcept;
    HookRuntimeMetrics Metrics() const noexcept;

private:
    friend struct HookRuntimeTestAccess;

    enum class QueueCancellationReason : std::uint8_t {
        Target,
        CircuitBreaker,
        Shutdown
    };

    static LRESULT CALLBACK KeyboardHookProcedure(int code, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK MouseHookProcedure(int code, WPARAM wParam, LPARAM lParam) noexcept;

    LRESULT HandleKeyboardHook(int code, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT HandleMouseHook(int code, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT HandleNormalizedEvent(
        const InputEvent& event,
        bool lowerIntegrityInjected,
        std::int64_t startCounter,
        WPARAM originalWParam,
        LPARAM originalLParam,
        HHOOK hook) noexcept;

    void HookThreadMain() noexcept;
    void SeedObservedPhysicalState() noexcept;
    void ActionWorkerMain() noexcept;
    void ProcessActionBatch(const ActionBatch& batch) noexcept;
    void ExecuteEligibleActionBatch(
        const ActionBatch& batch,
        InjectionDiagnosticRecord& record) noexcept;
    void RecordCancelledBatch(
        const ActionBatch& batch,
        QueueCancellationReason reason) noexcept;
    void CancelQueuedActions(QueueCancellationReason reason) noexcept;
    void DrainActionsForShutdown() noexcept;
    void RememberOwnedRelease(const ActionBatch& batch) noexcept;
    void ForgetOwnedRelease(DeviceKind device, DWORD code) noexcept;
    void ReleaseOwnedSyntheticState(unsigned int maximumAttempts) noexcept;
    void SignalActionWorker() noexcept;
    void PublishHookDiagnostic(
        HookDiagnosticRecord& record,
        std::int64_t startCounter) noexcept;
    void AccumulateMouseMoveDiagnostic(
        HookDiagnosticRecord record,
        std::int64_t startCounter) noexcept;
    void FlushMouseMoveDiagnostic(std::int64_t startCounter) noexcept;
    void RecordMaximumHookDuration(std::int64_t startCounter) noexcept;

    bool CreateRuntimeEvents(std::wstring& errorMessage) noexcept;
    void CloseRuntimeEvents() noexcept;
    DWORD TargetPid() const noexcept;
    bool TargetIsReadyAndForeground() const noexcept;
    bool TargetPointerRouteIsSafe(const InputEvent& source, const ActionBatch& batch) const noexcept;

    HookRuntimeOptions options_;
    TargetProcessContext* targetContext_;
    DiagnosticLog& diagnosticLog_;
    InputInjector injector_;
    InjectionCircuitBreaker injectionCircuitBreaker_{kInjectionFailureThreshold};
    FixedRuleEngine rules_;
    ActionQueue actionQueue_;

    HANDLE readyEvent_{nullptr};
    HANDLE actionReadyEvent_{nullptr};
    HANDLE producerDoneEvent_{nullptr};
    HANDLE stoppedEvent_{nullptr};
    HANDLE shutdownEvent_{nullptr};
    HANDLE actionEvent_{nullptr};
    std::thread hookThread_;
    std::thread actionThread_;
    std::atomic<DWORD> hookThreadId_{0};
    std::atomic<DWORD> startupError_{ERROR_SUCCESS};
    std::atomic<bool> started_{false};
    std::atomic<bool> shutdownRequested_{false};
    std::atomic<bool> circuitBreakerOpen_{false};

    HHOOK keyboardHook_{nullptr};
    HHOOK mouseHook_{nullptr};
    std::uint64_t nextSequence_{1};
    std::int64_t performanceFrequency_{1};
    HookDiagnosticRecord pendingMouseMoveRecord_{};
    std::uint32_t pendingMouseMoveCount_{0};
    std::array<Action, kMaxActionsPerBatch> ownedSyntheticReleases_{};
    std::size_t ownedSyntheticReleaseCount_{0};

    std::atomic<std::uint64_t> hookEvents_{0};
    std::atomic<std::uint64_t> suppressedEvents_{0};
    std::atomic<std::uint64_t> queuedBatches_{0};
    std::atomic<std::uint64_t> cancelledBatches_{0};
    std::atomic<std::uint64_t> injectionFailures_{0};
    std::atomic<std::uint64_t> maximumHookMicroseconds_{0};
    std::atomic<std::uint64_t> unresolvedSyntheticReleases_{0};
};

}  // namespace ukr
