#include "action_scheduler.hpp"

#include "remap_engine.hpp"
#include "support/producer_done_drain.hpp"

#include <algorithm>

namespace inputweaver {
namespace {

[[nodiscard]] std::int64_t ReadPerformanceCounter() noexcept {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

}  // namespace

ActionScheduler::ActionScheduler(
    SelfTag selfTag,
    TargetProcessContext* targetContext,
    DiagnosticLog& diagnosticLog,
    RemapEngine& remapEngine,
    StopRequest stopRequest,
    HANDLE shutdownEvent) noexcept
    : selfTag_(selfTag),
      targetContext_(targetContext),
      diagnosticLog_(diagnosticLog),
      remapEngine_(remapEngine),
      stopRequest_(stopRequest),
      shutdownEvent_(shutdownEvent),
      injector_(selfTag) {}

ActionScheduler::~ActionScheduler() {
    if (workerThread_.joinable()) {
        stopRequest_.Request();
        NotifyProducerDone();
        if (actionEvent_ != nullptr) {
            SetEvent(actionEvent_);
        }
        Wait();
    }
    CloseEvents();
}

bool ActionScheduler::Start(std::wstring& errorMessage) {
    if (started_.exchange(true, std::memory_order_acq_rel)) {
        errorMessage = L"The action scheduler has already been started.";
        return false;
    }
    if (selfTag_ == 0 || shutdownEvent_ == nullptr) {
        errorMessage = L"The action scheduler dependencies are invalid.";
        return false;
    }
    if (!CreateEvents(errorMessage)) {
        return false;
    }

    try {
        workerThread_ = std::thread(&ActionScheduler::WorkerMain, this);
    } catch (...) {
        errorMessage = L"Cannot create the action scheduler thread.";
        return false;
    }
    if (WaitForSingleObject(readyEvent_, 10000) != WAIT_OBJECT_0) {
        stopRequest_.Request();
        NotifyProducerDone();
        SetEvent(actionEvent_);
        Wait();
        errorMessage = L"The action scheduler did not become ready within ten seconds.";
        return false;
    }
    return true;
}

void ActionScheduler::Wait() noexcept {
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void ActionScheduler::NotifyProducerDone() noexcept {
    if (producerDoneEvent_ != nullptr) {
        SetEvent(producerDoneEvent_);
    }
}

ActionQueuePushResult ActionScheduler::TrySchedule(
    const ActionBatch& batch,
    CaptureCommit captureCommit) noexcept {
    const ActionQueuePushResult result = actionQueue_.TryPushWithCommit(
        batch,
        [this, captureCommit]() noexcept {
            return !CircuitBreakerOpen() && captureCommit.Commit();
        });
    if (result == ActionQueuePushResult::Accepted) {
        queuedBatches_.fetch_add(1, std::memory_order_relaxed);
        if (actionEvent_ != nullptr) {
            SetEvent(actionEvent_);
        }
    } else if (result == ActionQueuePushResult::Full &&
               actionQueueErrorEvent_ != nullptr &&
               !actionQueueErrorReported_.exchange(true, std::memory_order_acq_rel)) {
        SetEvent(actionQueueErrorEvent_);
    }
    return result;
}

bool ActionScheduler::CircuitBreakerOpen() const noexcept {
    return circuitBreakerOpen_.load(std::memory_order_acquire);
}

HANDLE ActionScheduler::ProducerDoneEvent() const noexcept {
    return producerDoneEvent_;
}

HANDLE ActionScheduler::ActionQueueErrorEvent() const noexcept {
    return actionQueueErrorEvent_;
}

ActionSchedulerMetrics ActionScheduler::Metrics() const noexcept {
    return {
        queuedBatches_.load(std::memory_order_relaxed),
        cancelledBatches_.load(std::memory_order_relaxed),
        actionQueue_.RejectedPushCount(),
        injectionFailures_.load(std::memory_order_relaxed),
        unresolvedSyntheticReleases_.load(std::memory_order_relaxed),
        circuitBreakerOpen_.load(std::memory_order_relaxed)};
}

bool ActionScheduler::CreateEvents(std::wstring& errorMessage) noexcept {
    if (readyEvent_ != nullptr && producerDoneEvent_ != nullptr &&
        actionEvent_ != nullptr && actionQueueErrorEvent_ != nullptr) {
        return true;
    }
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    producerDoneEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    actionEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    actionQueueErrorEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (readyEvent_ != nullptr && producerDoneEvent_ != nullptr &&
        actionEvent_ != nullptr && actionQueueErrorEvent_ != nullptr) {
        return true;
    }
    const DWORD error = GetLastError();
    CloseEvents();
    errorMessage = L"Cannot create action scheduler events. Win32 error " +
                   std::to_wstring(error) + L".";
    return false;
}

void ActionScheduler::CloseEvents() noexcept {
    HANDLE* events[] = {
        &readyEvent_,
        &producerDoneEvent_,
        &actionEvent_,
        &actionQueueErrorEvent_};
    for (HANDLE* event : events) {
        if (*event != nullptr) {
            CloseHandle(*event);
            *event = nullptr;
        }
    }
}

void ActionScheduler::WorkerMain() noexcept {
    SetEvent(readyEvent_);
    const HANDLE handles[] = {shutdownEvent_, actionEvent_};
    for (;;) {
        const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, 250);
        if (waitResult == WAIT_OBJECT_0) {
            DrainForShutdown();
            return;
        }
        if (waitResult == WAIT_FAILED) {
            OpenCircuit();
            stopRequest_.Request();
            DrainForShutdown();
            return;
        }

        ActionBatch batch{};
        while (actionQueue_.TryPop(batch)) {
            if (WaitForSingleObject(shutdownEvent_, 0) == WAIT_OBJECT_0) {
                RecordCancelledBatch(batch, CancellationReason::Shutdown);
                DrainForShutdown();
                return;
            }
            ProcessActionBatch(batch);
        }
    }
}

void ActionScheduler::ProcessActionBatch(const ActionBatch& batch) noexcept {
    InjectionDiagnosticRecord record{};
    record.sourceSequence = batch.sourceSequence;
    record.qpcTimestamp = ReadPerformanceCounter();
    record.targetPid = batch.targetPid;

    if (CircuitBreakerOpen()) {
        RecordCancelledBatch(batch, CancellationReason::CircuitBreaker);
        return;
    }
    if (targetContext_ == nullptr || batch.targetPid != TargetPid() ||
        !targetContext_->IsTargetForeground() ||
        (batch.requiresPointerTarget &&
         !targetContext_->IsTargetPointerTargetAtCursor())) {
        RecordCancelledBatch(batch, CancellationReason::Target);
        return;
    }
    if (!remapEngine_.CanInject(batch)) {
        record.cancelledForPhysicalState = true;
        cancelledBatches_.fetch_add(1, std::memory_order_relaxed);
        diagnosticLog_.TryPushInjection(record);
        return;
    }
    ExecuteEligibleActionBatch(batch, record);
}

void ActionScheduler::ExecuteEligibleActionBatch(
    const ActionBatch& batch,
    InjectionDiagnosticRecord& record) noexcept {
    const InjectionResult result = injector_.Inject(batch);
    record.requested = result.requested;
    record.sent = result.sent;
    record.win32Error = result.error;
    record.cleanupRequested = result.cleanupRequested;
    record.cleanupSent = result.cleanupSent;
    record.cleanupError = result.cleanupError;
    if (!result.Succeeded()) {
        injectionFailures_.fetch_add(1, std::memory_order_relaxed);
        if (result.cleanupRequested > result.cleanupSent) {
            RememberOwnedRelease(batch);
            ReleaseOwnedSyntheticState(1);
            if (ownedSyntheticReleaseCount_ != 0) {
                OpenCircuit();
                CancelQueuedActions(CancellationReason::CircuitBreaker);
                ReleaseOwnedSyntheticState(3);
                if (ownedSyntheticReleaseCount_ != 0) {
                    stopRequest_.Request();
                }
            }
        }
        if (injectionCircuitBreaker_.RecordFailure()) {
            OpenCircuit();
            CancelQueuedActions(CancellationReason::CircuitBreaker);
            ReleaseOwnedSyntheticState(3);
        }
    } else {
        injectionCircuitBreaker_.RecordSuccess();
        ForgetOwnedRelease(batch.outputDevice, batch.outputCode);
    }
    record.circuitBreakerOpen = CircuitBreakerOpen();
    diagnosticLog_.TryPushInjection(record);
}

void ActionScheduler::RecordCancelledBatch(
    const ActionBatch& batch,
    CancellationReason reason) noexcept {
    InjectionDiagnosticRecord record{};
    record.sourceSequence = batch.sourceSequence;
    record.qpcTimestamp = ReadPerformanceCounter();
    record.targetPid = batch.targetPid;
    record.cancelledForTarget = reason == CancellationReason::Target;
    record.cancelledForCircuitBreaker = reason == CancellationReason::CircuitBreaker;
    record.cancelledForShutdown = reason == CancellationReason::Shutdown;
    record.circuitBreakerOpen = CircuitBreakerOpen();
    cancelledBatches_.fetch_add(1, std::memory_order_relaxed);
    diagnosticLog_.TryPushInjection(record);
}

void ActionScheduler::CancelQueuedActions(CancellationReason reason) noexcept {
    ActionBatch batch{};
    while (actionQueue_.TryPop(batch)) {
        RecordCancelledBatch(batch, reason);
    }
}

void ActionScheduler::DrainForShutdown() noexcept {
    DrainUntilProducerDone(
        [this]() noexcept {
            CancelQueuedActions(CancellationReason::Shutdown);
        },
        [this]() noexcept {
            const HANDLE handles[] = {producerDoneEvent_, actionEvent_};
            const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, 100);
            if (waitResult == WAIT_OBJECT_0) {
                return ProducerDrainWaitResult::ProducerDone;
            }
            if (waitResult == WAIT_FAILED) {
                if (WaitForSingleObject(producerDoneEvent_, 0) == WAIT_OBJECT_0) {
                    return ProducerDrainWaitResult::ProducerDone;
                }
                Sleep(1);
            }
            return ProducerDrainWaitResult::Continue;
        });
    ReleaseOwnedSyntheticState(3);
}

void ActionScheduler::RememberOwnedRelease(const ActionBatch& batch) noexcept {
    if (batch.outputCode == 0 ||
        (batch.outputDevice != DeviceKind::Keyboard &&
         batch.outputDevice != DeviceKind::Mouse)) {
        return;
    }
    for (std::size_t index = 0; index < ownedSyntheticReleaseCount_; ++index) {
        if (ownedSyntheticReleases_[index].device == batch.outputDevice &&
            ownedSyntheticReleases_[index].code == batch.outputCode) {
            return;
        }
    }
    if (ownedSyntheticReleaseCount_ >= ownedSyntheticReleases_.size()) {
        unresolvedSyntheticReleases_.store(
            ownedSyntheticReleaseCount_ + 1U, std::memory_order_relaxed);
        OpenCircuit();
        return;
    }
    ownedSyntheticReleases_[ownedSyntheticReleaseCount_++] = {
        batch.outputDevice, Transition::Up, batch.outputCode, 0, 0};
    unresolvedSyntheticReleases_.store(
        ownedSyntheticReleaseCount_, std::memory_order_relaxed);
}

void ActionScheduler::ForgetOwnedRelease(
    DeviceKind device,
    ControlCode code) noexcept {
    for (std::size_t index = 0; index < ownedSyntheticReleaseCount_; ++index) {
        if (ownedSyntheticReleases_[index].device != device ||
            ownedSyntheticReleases_[index].code != code) {
            continue;
        }
        ownedSyntheticReleases_[index] =
            ownedSyntheticReleases_[ownedSyntheticReleaseCount_ - 1U];
        --ownedSyntheticReleaseCount_;
        unresolvedSyntheticReleases_.store(
            ownedSyntheticReleaseCount_, std::memory_order_relaxed);
        return;
    }
}

void ActionScheduler::ReleaseOwnedSyntheticState(
    unsigned int maximumAttempts) noexcept {
    for (unsigned int attempt = 0;
         attempt < maximumAttempts && ownedSyntheticReleaseCount_ != 0;
         ++attempt) {
        std::size_t retainedCount = 0;
        const std::size_t originalCount = ownedSyntheticReleaseCount_;
        for (std::size_t index = 0; index < originalCount; ++index) {
            ActionBatch releaseBatch{};
            releaseBatch.targetPid = TargetPid();
            releaseBatch.outputDevice = ownedSyntheticReleases_[index].device;
            releaseBatch.outputCode = ownedSyntheticReleases_[index].code;
            releaseBatch.actions[0] = ownedSyntheticReleases_[index];
            releaseBatch.actionCount = 1;

            const InjectionResult result = injector_.Inject(releaseBatch);
            InjectionDiagnosticRecord record{};
            record.qpcTimestamp = ReadPerformanceCounter();
            record.targetPid = releaseBatch.targetPid;
            record.requested = result.requested;
            record.sent = result.sent;
            record.win32Error = result.error;
            record.cleanupRequested = result.cleanupRequested;
            record.cleanupSent = result.cleanupSent;
            record.cleanupError = result.cleanupError;
            record.circuitBreakerOpen = CircuitBreakerOpen();
            diagnosticLog_.TryPushInjection(record);
            if (!result.Succeeded()) {
                injectionFailures_.fetch_add(1, std::memory_order_relaxed);
                ownedSyntheticReleases_[retainedCount++] =
                    ownedSyntheticReleases_[index];
            }
        }
        ownedSyntheticReleaseCount_ = retainedCount;
    }
    unresolvedSyntheticReleases_.store(
        ownedSyntheticReleaseCount_, std::memory_order_relaxed);
}

void ActionScheduler::OpenCircuit() noexcept {
    circuitBreakerOpen_.store(true, std::memory_order_release);
    remapEngine_.DisableNewCaptures();
}

ProcessId ActionScheduler::TargetPid() const noexcept {
    return targetContext_ == nullptr ? 0 : targetContext_->TargetPid();
}

}  // namespace inputweaver
