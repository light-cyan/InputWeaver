#include "remap_engine.hpp"

#include "action_scheduler.hpp"

#include <algorithm>
#include <limits>

namespace ukr {
namespace {

[[nodiscard]] std::int64_t ReadPerformanceCounter() noexcept {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

}  // namespace

RemapEngine::RemapEngine(
    RemapEngineOptions options,
    TargetProcessContext* targetContext,
    DiagnosticLog& diagnosticLog,
    StopRequest stopRequest,
    std::atomic<bool>& shutdownRequested) noexcept
    : options_(options),
      targetContext_(targetContext),
      diagnosticLog_(diagnosticLog),
      stopRequest_(stopRequest),
      shutdownRequested_(shutdownRequested) {
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
        performanceFrequency_ = frequency.QuadPart;
    }
}

void RemapEngine::AttachScheduler(ActionScheduler& scheduler) noexcept {
    scheduler_ = &scheduler;
}

void RemapEngine::DisableNewCaptures() noexcept {
    rules_.DisableNewCaptures();
}

bool RemapEngine::CanInject(const ActionBatch& batch) const noexcept {
    return rules_.CanInject(batch);
}

bool RemapEngine::NewCapturesEnabled() const noexcept {
    return rules_.NewCapturesEnabled();
}

RemapEngineMetrics RemapEngine::Metrics() const noexcept {
    return {
        hookEvents_.load(std::memory_order_relaxed),
        suppressedEvents_.load(std::memory_order_relaxed),
        maximumHookMicroseconds_.load(std::memory_order_relaxed)};
}

InputDecision RemapEngine::HandleInput(
    const InputEvent& event,
    bool lowerIntegrityInjected,
    std::int64_t startCounter) noexcept {
    HookDiagnosticRecord record{};
    record.sequence = nextSequence_++;
    record.qpcTimestamp = startCounter;
    record.rawFlags = event.flags;
    record.code = event.code;
    record.scanCode = event.scanCode;
    record.mouseData = event.mouseData;
    record.device = event.device;
    record.origin = event.origin;
    record.transition = event.transition;
    record.extraInfo = CategorizeExtraInfo(event.extraInfo, options_.selfTag);
    record.lowerIntegrityInjected = lowerIntegrityInjected;
    hookEvents_.fetch_add(1, std::memory_order_relaxed);

    if (event.device == DeviceKind::Mouse && event.transition == Transition::Move) {
        if (options_.traceInput || event.origin == InputOrigin::SelfInjected) {
            AccumulateMouseMoveDiagnostic(record, startCounter);
        } else {
            RecordMaximumHookDuration(startCounter);
        }
        return InputDecision::Forward;
    }
    FlushDiagnostics(startCounter);

    if (event.origin != InputOrigin::PhysicalCandidate) {
        PublishHookDiagnostic(record, startCounter);
        return InputDecision::Forward;
    }

    const bool mappingActive = options_.mappingMode && scheduler_ != nullptr &&
                               !shutdownRequested_.load(std::memory_order_acquire) &&
                               !scheduler_->CircuitBreakerOpen();
    const RuleEvaluation evaluation =
        rules_.Evaluate(event, mappingActive, TargetPid(), record.sequence);
    record.ruleId = static_cast<std::uint32_t>(evaluation.rule);

    if (evaluation.kind == RuleEvaluationKind::EmergencyStop) {
        stopRequest_.Request();
        PublishHookDiagnostic(record, startCounter);
        return InputDecision::Forward;
    }
    if (evaluation.kind == RuleEvaluationKind::SuppressCaptured) {
        record.suppressed = true;
        suppressedEvents_.fetch_add(1, std::memory_order_relaxed);
        PublishHookDiagnostic(record, startCounter);
        return InputDecision::Suppress;
    }
    if (evaluation.kind != RuleEvaluationKind::ActionReady) {
        PublishHookDiagnostic(record, startCounter);
        return InputDecision::Forward;
    }

    const HWND foregroundWindow = GetForegroundWindow();
    if (foregroundWindow != nullptr) {
        GetWindowThreadProcessId(foregroundWindow, &record.foregroundPid);
    }
    if (targetContext_ == nullptr || !targetContext_->IsTargetForeground() ||
        !TargetPointerRouteIsSafe(event, evaluation.batch)) {
        PublishHookDiagnostic(record, startCounter);
        return InputDecision::Forward;
    }
    record.foregroundPid = TargetPid();

    CommitContext commitContext{&rules_, &evaluation};
    const ActionQueuePushResult pushResult = scheduler_->TrySchedule(
        evaluation.batch,
        {&commitContext, &RemapEngine::CommitCapture});
    if (pushResult != ActionQueuePushResult::Accepted) {
        record.queueResult = pushResult == ActionQueuePushResult::Full
            ? QueueResult::Rejected
            : QueueResult::CommitRejected;
        PublishHookDiagnostic(record, startCounter);
        return InputDecision::Forward;
    }

    record.queueResult = QueueResult::Accepted;
    record.suppressed = true;
    suppressedEvents_.fetch_add(1, std::memory_order_relaxed);
    PublishHookDiagnostic(record, startCounter);
    return InputDecision::Suppress;
}

void RemapEngine::SeedPhysicalState(
    DeviceKind device,
    DWORD code,
    bool down) noexcept {
    rules_.SeedPhysicalState(device, code, down);
}

bool RemapEngine::HasCapturedInputs() const noexcept {
    return rules_.HasCapturedInputs();
}

void RemapEngine::FlushDiagnostics(std::int64_t startCounter) noexcept {
    if (pendingMouseMoveCount_ == 0) {
        return;
    }
    pendingMouseMoveRecord_.aggregateCount = pendingMouseMoveCount_;
    PublishHookDiagnostic(pendingMouseMoveRecord_, startCounter);
    pendingMouseMoveRecord_ = {};
    pendingMouseMoveCount_ = 0;
}

bool RemapEngine::CommitCapture(void* context) noexcept {
    const auto* commit = static_cast<const CommitContext*>(context);
    return commit != nullptr && commit->rules != nullptr &&
           commit->evaluation != nullptr &&
           commit->rules->CommitCapture(*commit->evaluation);
}

void RemapEngine::PublishHookDiagnostic(
    HookDiagnosticRecord& record,
    std::int64_t startCounter) noexcept {
    const std::int64_t elapsed = (std::max)(
        static_cast<std::int64_t>(0), ReadPerformanceCounter() - startCounter);
    const std::uint64_t microseconds = static_cast<std::uint64_t>(
        (elapsed * 1000000LL) / performanceFrequency_);
    record.processingMicroseconds = static_cast<std::uint32_t>((std::min)(
        microseconds,
        static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
    const std::uint64_t previous =
        maximumHookMicroseconds_.load(std::memory_order_relaxed);
    if (microseconds > previous) {
        maximumHookMicroseconds_.store(microseconds, std::memory_order_relaxed);
    }
    if (ShouldPublishHookDiagnostic(record, options_.traceInput)) {
        diagnosticLog_.TryPushHook(record);
    }
}

void RemapEngine::AccumulateMouseMoveDiagnostic(
    HookDiagnosticRecord record,
    std::int64_t startCounter) noexcept {
    constexpr std::uint32_t kMouseMoveAggregationCount = 64;
    if (record.origin == InputOrigin::SelfInjected) {
        FlushDiagnostics(startCounter);
        record.aggregateCount = 1;
        PublishHookDiagnostic(record, startCounter);
        return;
    }

    const bool compatible = pendingMouseMoveCount_ != 0 &&
                            pendingMouseMoveRecord_.origin == record.origin &&
                            pendingMouseMoveRecord_.extraInfo == record.extraInfo &&
                            pendingMouseMoveRecord_.rawFlags == record.rawFlags &&
                            pendingMouseMoveRecord_.lowerIntegrityInjected ==
                                record.lowerIntegrityInjected;
    if (!compatible) {
        FlushDiagnostics(startCounter);
        pendingMouseMoveRecord_ = record;
        pendingMouseMoveCount_ = 1;
    } else {
        ++pendingMouseMoveCount_;
    }
    if (pendingMouseMoveCount_ >= kMouseMoveAggregationCount) {
        FlushDiagnostics(startCounter);
    }
    RecordMaximumHookDuration(startCounter);
}

void RemapEngine::RecordMaximumHookDuration(
    std::int64_t startCounter) noexcept {
    const std::int64_t elapsed = (std::max)(
        static_cast<std::int64_t>(0), ReadPerformanceCounter() - startCounter);
    const std::uint64_t microseconds = static_cast<std::uint64_t>(
        (elapsed * 1000000LL) / performanceFrequency_);
    const std::uint64_t previous =
        maximumHookMicroseconds_.load(std::memory_order_relaxed);
    if (microseconds > previous) {
        maximumHookMicroseconds_.store(microseconds, std::memory_order_relaxed);
    }
}

DWORD RemapEngine::TargetPid() const noexcept {
    return targetContext_ == nullptr ? 0 : targetContext_->TargetPid();
}

bool RemapEngine::TargetPointerRouteIsSafe(
    const InputEvent& source,
    const ActionBatch& batch) const noexcept {
    if (targetContext_ == nullptr) {
        return false;
    }
    if (source.device == DeviceKind::Mouse && source.transition == Transition::Down &&
        !targetContext_->IsTargetPointerTarget(source.position)) {
        return false;
    }
    return !batch.requiresPointerTarget ||
           targetContext_->IsTargetPointerTargetAtCursor();
}

}  // namespace ukr
