#include "hook_thread.hpp"

#include "input_classifier.hpp"
#include "core/producer_done_drain.hpp"

#include <algorithm>
#include <limits>

namespace ukr {
namespace {

inline constexpr UINT kWakeMessage = WM_APP + 1U;

std::atomic<HookRuntime*> gActiveRuntime{nullptr};

bool NormalizeKeyboardMessage(
    WPARAM message,
    const KBDLLHOOKSTRUCT& source,
    SelfTag selfTag,
    InputEvent& event) noexcept {
    Transition transition{};
    switch (message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            transition = Transition::Down;
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            transition = Transition::Up;
            break;
        default:
            return false;
    }

    event.device = DeviceKind::Keyboard;
    event.origin = ClassifyKeyboard(source, selfTag);
    event.transition = transition;
    event.code = source.vkCode;
    event.scanCode = source.scanCode;
    event.flags = source.flags;
    event.timestamp = source.time;
    event.extraInfo = source.dwExtraInfo;
    return true;
}

bool NormalizeMouseMessage(
    WPARAM message,
    const MSLLHOOKSTRUCT& source,
    SelfTag selfTag,
    InputEvent& event) noexcept {
    event.device = DeviceKind::Mouse;
    event.origin = ClassifyMouse(source, selfTag);
    event.scanCode = 0;
    event.flags = source.flags;
    event.mouseData = source.mouseData;
    event.position = source.pt;
    event.timestamp = source.time;
    event.extraInfo = source.dwExtraInfo;

    switch (message) {
        case WM_LBUTTONDOWN:
            event.transition = Transition::Down;
            event.code = VK_LBUTTON;
            return true;
        case WM_LBUTTONUP:
            event.transition = Transition::Up;
            event.code = VK_LBUTTON;
            return true;
        case WM_RBUTTONDOWN:
            event.transition = Transition::Down;
            event.code = VK_RBUTTON;
            return true;
        case WM_RBUTTONUP:
            event.transition = Transition::Up;
            event.code = VK_RBUTTON;
            return true;
        case WM_MBUTTONDOWN:
            event.transition = Transition::Down;
            event.code = VK_MBUTTON;
            return true;
        case WM_MBUTTONUP:
            event.transition = Transition::Up;
            event.code = VK_MBUTTON;
            return true;
        case WM_XBUTTONDOWN:
            event.transition = Transition::Down;
            event.code = HIWORD(source.mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
            return true;
        case WM_XBUTTONUP:
            event.transition = Transition::Up;
            event.code = HIWORD(source.mouseData) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
            return true;
        case WM_MOUSEMOVE:
            event.transition = Transition::Move;
            event.code = 0;
            return true;
        case WM_MOUSEWHEEL:
            event.transition = Transition::VerticalWheel;
            event.code = 0;
            return true;
        case WM_MOUSEHWHEEL:
            event.transition = Transition::HorizontalWheel;
            event.code = 0;
            return true;
        default:
            return false;
    }
}

std::int64_t ReadPerformanceCounter() noexcept {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

}  // namespace

HookRuntime::HookRuntime(
    HookRuntimeOptions options,
    TargetProcessContext* targetContext,
    DiagnosticLog& diagnosticLog) noexcept
    : options_(options),
      targetContext_(targetContext),
      diagnosticLog_(diagnosticLog),
      injector_(options.selfTag) {
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
        performanceFrequency_ = frequency.QuadPart;
    }
}

HookRuntime::~HookRuntime() {
    RequestStop();
    Wait();
    CloseRuntimeEvents();
}

bool HookRuntime::Start(std::wstring& errorMessage) {
    if (started_.exchange(true, std::memory_order_acq_rel)) {
        errorMessage = L"The hook runtime has already been started.";
        return false;
    }
    if (options_.selfTag == 0) {
        errorMessage = L"The self-injection tag must be nonzero.";
        return false;
    }
    if (options_.mappingMode &&
        (targetContext_ == nullptr || !targetContext_->IsValid())) {
        errorMessage = L"Mapping mode requires a validated target context.";
        return false;
    }
    if (!CreateRuntimeEvents(errorMessage)) {
        return false;
    }

    try {
        actionThread_ = std::thread(&HookRuntime::ActionWorkerMain, this);
        hookThread_ = std::thread(&HookRuntime::HookThreadMain, this);
    } catch (...) {
        RequestStop();
        if (!hookThread_.joinable() && producerDoneEvent_ != nullptr) {
            SetEvent(producerDoneEvent_);
        }
        Wait();
        errorMessage = L"Cannot create the Phase 1 runtime threads.";
        return false;
    }

    const HANDLE readinessEvents[] = {readyEvent_, actionReadyEvent_};
    const DWORD waitResult = WaitForMultipleObjects(
        2, readinessEvents, TRUE, 10000);
    if (waitResult != WAIT_OBJECT_0) {
        RequestStop();
        Wait();
        errorMessage = L"The hook thread did not become ready within ten seconds.";
        return false;
    }

    const DWORD startupError = startupError_.load(std::memory_order_acquire);
    if (startupError != ERROR_SUCCESS) {
        RequestStop();
        Wait();
        errorMessage = L"Cannot start the low-level hooks. Win32 error " +
                       std::to_wstring(startupError) + L".";
        return false;
    }
    return true;
}

void HookRuntime::RequestStop() noexcept {
    shutdownRequested_.store(true, std::memory_order_release);
    rules_.DisableNewCaptures();
    if (shutdownEvent_ != nullptr) {
        SetEvent(shutdownEvent_);
    }
    const DWORD threadId = hookThreadId_.load(std::memory_order_acquire);
    if (threadId != 0) {
        PostThreadMessageW(threadId, kWakeMessage, 0, 0);
    }
}

void HookRuntime::Wait() noexcept {
    if (hookThread_.joinable()) {
        hookThread_.join();
    }
    if (actionThread_.joinable()) {
        actionThread_.join();
    }
}

HANDLE HookRuntime::StoppedEvent() const noexcept {
    return stoppedEvent_;
}

HookRuntimeMetrics HookRuntime::Metrics() const noexcept {
    return {
        hookEvents_.load(std::memory_order_relaxed),
        suppressedEvents_.load(std::memory_order_relaxed),
        queuedBatches_.load(std::memory_order_relaxed),
        cancelledBatches_.load(std::memory_order_relaxed),
        actionQueue_.RejectedPushCount(),
        injectionFailures_.load(std::memory_order_relaxed),
        maximumHookMicroseconds_.load(std::memory_order_relaxed),
        unresolvedSyntheticReleases_.load(std::memory_order_relaxed),
        circuitBreakerOpen_.load(std::memory_order_relaxed)};
}

LRESULT CALLBACK HookRuntime::KeyboardHookProcedure(
    int code,
    WPARAM wParam,
    LPARAM lParam) noexcept {
    HookRuntime* runtime = gActiveRuntime.load(std::memory_order_acquire);
    if (runtime == nullptr || code != HC_ACTION) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
    return runtime->HandleKeyboardHook(code, wParam, lParam);
}

LRESULT CALLBACK HookRuntime::MouseHookProcedure(
    int code,
    WPARAM wParam,
    LPARAM lParam) noexcept {
    HookRuntime* runtime = gActiveRuntime.load(std::memory_order_acquire);
    if (runtime == nullptr || code != HC_ACTION) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
    return runtime->HandleMouseHook(code, wParam, lParam);
}

LRESULT HookRuntime::HandleKeyboardHook(int code, WPARAM wParam, LPARAM lParam) noexcept {
    const std::int64_t startCounter = ReadPerformanceCounter();
    const auto* source = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (source == nullptr) {
        return CallNextHookEx(keyboardHook_, code, wParam, lParam);
    }

    InputEvent event{};
    if (!NormalizeKeyboardMessage(wParam, *source, options_.selfTag, event)) {
        return CallNextHookEx(keyboardHook_, code, wParam, lParam);
    }
    const bool lowerIntegrity = (source->flags & LLKHF_LOWER_IL_INJECTED) != 0;
    return HandleNormalizedEvent(
        event, lowerIntegrity, startCounter, wParam, lParam, keyboardHook_);
}

LRESULT HookRuntime::HandleMouseHook(int code, WPARAM wParam, LPARAM lParam) noexcept {
    const std::int64_t startCounter = ReadPerformanceCounter();
    const auto* source = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
    if (source == nullptr) {
        return CallNextHookEx(mouseHook_, code, wParam, lParam);
    }

    InputEvent event{};
    if (!NormalizeMouseMessage(wParam, *source, options_.selfTag, event)) {
        return CallNextHookEx(mouseHook_, code, wParam, lParam);
    }
    const bool lowerIntegrity = (source->flags & LLMHF_LOWER_IL_INJECTED) != 0;
    return HandleNormalizedEvent(
        event, lowerIntegrity, startCounter, wParam, lParam, mouseHook_);
}

LRESULT HookRuntime::HandleNormalizedEvent(
    const InputEvent& event,
    bool lowerIntegrityInjected,
    std::int64_t startCounter,
    WPARAM originalWParam,
    LPARAM originalLParam,
    HHOOK hook) noexcept {
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
        return CallNextHookEx(hook, HC_ACTION, originalWParam, originalLParam);
    }
    FlushMouseMoveDiagnostic(startCounter);

    if (event.origin != InputOrigin::PhysicalCandidate) {
        PublishHookDiagnostic(record, startCounter);
        return CallNextHookEx(hook, HC_ACTION, originalWParam, originalLParam);
    }

    const bool mappingActive = options_.mappingMode &&
                               !shutdownRequested_.load(std::memory_order_acquire) &&
                               !circuitBreakerOpen_.load(std::memory_order_acquire);
    const RuleEvaluation evaluation =
        rules_.Evaluate(event, mappingActive, TargetPid(), record.sequence);
    record.ruleId = static_cast<std::uint32_t>(evaluation.rule);

    if (evaluation.kind == RuleEvaluationKind::EmergencyStop) {
        RequestStop();
        PublishHookDiagnostic(record, startCounter);
        return CallNextHookEx(hook, HC_ACTION, originalWParam, originalLParam);
    }

    if (evaluation.kind == RuleEvaluationKind::SuppressCaptured) {
        record.suppressed = true;
        suppressedEvents_.fetch_add(1, std::memory_order_relaxed);
        PublishHookDiagnostic(record, startCounter);
        return 1;
    }

    if (evaluation.kind != RuleEvaluationKind::ActionReady) {
        PublishHookDiagnostic(record, startCounter);
        return CallNextHookEx(hook, HC_ACTION, originalWParam, originalLParam);
    }

    const HWND foregroundWindow = GetForegroundWindow();
    if (foregroundWindow != nullptr) {
        GetWindowThreadProcessId(foregroundWindow, &record.foregroundPid);
    }
    if (!TargetIsReadyAndForeground() ||
        !TargetPointerRouteIsSafe(event, evaluation.batch)) {
        PublishHookDiagnostic(record, startCounter);
        return CallNextHookEx(hook, HC_ACTION, originalWParam, originalLParam);
    }
    record.foregroundPid = TargetPid();

    const ActionQueuePushResult pushResult = actionQueue_.TryPushWithCommit(
        evaluation.batch,
        [this, &evaluation]() noexcept {
            return rules_.CommitCapture(evaluation);
        });
    if (pushResult != ActionQueuePushResult::Accepted) {
        record.queueResult = pushResult == ActionQueuePushResult::Full
            ? QueueResult::Rejected
            : QueueResult::CommitRejected;
        PublishHookDiagnostic(record, startCounter);
        return CallNextHookEx(hook, HC_ACTION, originalWParam, originalLParam);
    }

    record.queueResult = QueueResult::Accepted;
    SignalActionWorker();
    queuedBatches_.fetch_add(1, std::memory_order_relaxed);
    record.suppressed = true;
    suppressedEvents_.fetch_add(1, std::memory_order_relaxed);
    PublishHookDiagnostic(record, startCounter);
    return 1;
}

void HookRuntime::HookThreadMain() noexcept {
    hookThreadId_.store(GetCurrentThreadId(), std::memory_order_release);
    MSG initialMessage{};
    PeekMessageW(&initialMessage, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    HookRuntime* expected = nullptr;
    if (!gActiveRuntime.compare_exchange_strong(
            expected, this, std::memory_order_acq_rel, std::memory_order_acquire)) {
        startupError_.store(ERROR_ALREADY_EXISTS, std::memory_order_release);
        SetEvent(readyEvent_);
        SetEvent(producerDoneEvent_);
        SetEvent(stoppedEvent_);
        return;
    }

    const HINSTANCE module = GetModuleHandleW(nullptr);
    keyboardHook_ = SetWindowsHookExW(
        WH_KEYBOARD_LL, &HookRuntime::KeyboardHookProcedure, module, 0);
    if (keyboardHook_ == nullptr) {
        startupError_.store(GetLastError(), std::memory_order_release);
    } else {
        mouseHook_ = SetWindowsHookExW(
            WH_MOUSE_LL, &HookRuntime::MouseHookProcedure, module, 0);
        if (mouseHook_ == nullptr) {
            startupError_.store(GetLastError(), std::memory_order_release);
        }
    }

    if (startupError_.load(std::memory_order_acquire) == ERROR_SUCCESS) {
        SeedObservedPhysicalState();
    }
    SetEvent(readyEvent_);

    bool shuttingDown =
        startupError_.load(std::memory_order_acquire) != ERROR_SUCCESS;
    ShutdownGraceWindow shutdownGrace(kCapturedReleaseGraceMilliseconds);
    if (shuttingDown) {
        shutdownRequested_.store(true, std::memory_order_release);
        rules_.DisableNewCaptures();
        SetEvent(shutdownEvent_);
        shutdownGrace.Begin(GetTickCount64());
    }

    while (!shuttingDown || rules_.HasCapturedInputs()) {
        if (!shuttingDown) {
            HANDLE handles[2] = {shutdownEvent_, nullptr};
            DWORD handleCount = 1;
            if (targetContext_ != nullptr && targetContext_->IsValid()) {
                handles[handleCount++] = targetContext_->TargetHandle();
            }
            const DWORD waitResult = MsgWaitForMultipleObjectsEx(
                handleCount, handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (waitResult == WAIT_OBJECT_0 ||
                (handleCount == 2 && waitResult == WAIT_OBJECT_0 + 1)) {
                shuttingDown = true;
                shutdownRequested_.store(true, std::memory_order_release);
                rules_.DisableNewCaptures();
                SetEvent(shutdownEvent_);
                shutdownGrace.Begin(GetTickCount64());
            } else if (waitResult == WAIT_FAILED) {
                shuttingDown = true;
                shutdownRequested_.store(true, std::memory_order_release);
                rules_.DisableNewCaptures();
                SetEvent(shutdownEvent_);
                shutdownGrace.Begin(GetTickCount64());
            }
        } else {
            const ULONGLONG now = GetTickCount64();
            if (shutdownGrace.Expired(now)) {
                break;
            }
            const DWORD remaining = shutdownGrace.RemainingSlice(now, 50);
            MsgWaitForMultipleObjectsEx(
                0, nullptr, remaining, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT || message.message == kWakeMessage) {
                if (shutdownRequested_.load(std::memory_order_acquire) && !shuttingDown) {
                    shuttingDown = true;
                    rules_.DisableNewCaptures();
                    shutdownGrace.Begin(GetTickCount64());
                }
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (!shuttingDown && shutdownRequested_.load(std::memory_order_acquire)) {
            shuttingDown = true;
            rules_.DisableNewCaptures();
            shutdownGrace.Begin(GetTickCount64());
        }
    }

    FlushMouseMoveDiagnostic(ReadPerformanceCounter());
    if (mouseHook_ != nullptr) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
    if (keyboardHook_ != nullptr) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    expected = this;
    gActiveRuntime.compare_exchange_strong(
        expected, nullptr, std::memory_order_acq_rel, std::memory_order_acquire);
    hookThreadId_.store(0, std::memory_order_release);
    SetEvent(producerDoneEvent_);
    SetEvent(stoppedEvent_);
}

void HookRuntime::ActionWorkerMain() noexcept {
    SetEvent(actionReadyEvent_);
    const HANDLE handles[] = {shutdownEvent_, actionEvent_};
    for (;;) {
        const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, 250);
        if (waitResult == WAIT_OBJECT_0) {
            DrainActionsForShutdown();
            return;
        }
        if (waitResult == WAIT_FAILED) {
            circuitBreakerOpen_.store(true, std::memory_order_release);
            rules_.DisableNewCaptures();
            RequestStop();
            DrainActionsForShutdown();
            return;
        }

        ActionBatch batch{};
        while (actionQueue_.TryPop(batch)) {
            if (WaitForSingleObject(shutdownEvent_, 0) == WAIT_OBJECT_0) {
                RecordCancelledBatch(batch, QueueCancellationReason::Shutdown);
                DrainActionsForShutdown();
                return;
            }

            ProcessActionBatch(batch);
        }
    }
}

void HookRuntime::SeedObservedPhysicalState() noexcept {
    constexpr DWORD keyboardControls[] = {
        VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F12,
        VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT};
    for (const DWORD code : keyboardControls) {
        rules_.SeedPhysicalState(
            DeviceKind::Keyboard,
            code,
            (GetAsyncKeyState(static_cast<int>(code)) & 0x8000) != 0);
    }
    rules_.SeedPhysicalState(
        DeviceKind::Mouse,
        VK_MBUTTON,
        (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
}

void HookRuntime::ProcessActionBatch(const ActionBatch& batch) noexcept {
    InjectionDiagnosticRecord record{};
    record.sourceSequence = batch.sourceSequence;
    record.qpcTimestamp = ReadPerformanceCounter();
    record.targetPid = batch.targetPid;

    if (circuitBreakerOpen_.load(std::memory_order_acquire)) {
        record.cancelledForCircuitBreaker = true;
        cancelledBatches_.fetch_add(1, std::memory_order_relaxed);
        diagnosticLog_.TryPushInjection(record);
        return;
    }
    if (targetContext_ == nullptr || batch.targetPid != TargetPid() ||
         !TargetIsReadyAndForeground() ||
         (batch.requiresPointerTarget &&
          !targetContext_->IsTargetPointerTargetAtCursor())) {
        record.cancelledForTarget = true;
        cancelledBatches_.fetch_add(1, std::memory_order_relaxed);
        diagnosticLog_.TryPushInjection(record);
        return;
    }
    if (!rules_.CanInject(batch)) {
        record.cancelledForPhysicalState = true;
        cancelledBatches_.fetch_add(1, std::memory_order_relaxed);
        diagnosticLog_.TryPushInjection(record);
        return;
    }

    ExecuteEligibleActionBatch(batch, record);
}

void HookRuntime::ExecuteEligibleActionBatch(
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
                circuitBreakerOpen_.store(true, std::memory_order_release);
                rules_.DisableNewCaptures();
                CancelQueuedActions(QueueCancellationReason::CircuitBreaker);
                ReleaseOwnedSyntheticState(3);
                if (ownedSyntheticReleaseCount_ != 0) {
                    RequestStop();
                }
            }
        }
        if (injectionCircuitBreaker_.RecordFailure()) {
            circuitBreakerOpen_.store(true, std::memory_order_release);
            rules_.DisableNewCaptures();
            CancelQueuedActions(QueueCancellationReason::CircuitBreaker);
            ReleaseOwnedSyntheticState(3);
        }
    } else {
        injectionCircuitBreaker_.RecordSuccess();
        ForgetOwnedRelease(batch.outputDevice, batch.outputCode);
    }
    record.circuitBreakerOpen = circuitBreakerOpen_.load(std::memory_order_acquire);
    diagnosticLog_.TryPushInjection(record);
}

void HookRuntime::RecordCancelledBatch(
    const ActionBatch& batch,
    QueueCancellationReason reason) noexcept {
    InjectionDiagnosticRecord record{};
    record.sourceSequence = batch.sourceSequence;
    record.qpcTimestamp = ReadPerformanceCounter();
    record.targetPid = batch.targetPid;
    record.cancelledForTarget = reason == QueueCancellationReason::Target;
    record.cancelledForCircuitBreaker =
        reason == QueueCancellationReason::CircuitBreaker;
    record.cancelledForShutdown = reason == QueueCancellationReason::Shutdown;
    record.circuitBreakerOpen =
        circuitBreakerOpen_.load(std::memory_order_acquire);
    cancelledBatches_.fetch_add(1, std::memory_order_relaxed);
    diagnosticLog_.TryPushInjection(record);
}

void HookRuntime::CancelQueuedActions(QueueCancellationReason reason) noexcept {
    ActionBatch batch{};
    while (actionQueue_.TryPop(batch)) {
        RecordCancelledBatch(batch, reason);
    }
}

void HookRuntime::DrainActionsForShutdown() noexcept {
    DrainUntilProducerDone(
        [this]() noexcept {
            CancelQueuedActions(QueueCancellationReason::Shutdown);
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

void HookRuntime::RememberOwnedRelease(const ActionBatch& batch) noexcept {
    if (batch.outputCode == 0 ||
        (batch.outputDevice != DeviceKind::Keyboard && batch.outputDevice != DeviceKind::Mouse)) {
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
            ownedSyntheticReleaseCount_ + 1, std::memory_order_relaxed);
        circuitBreakerOpen_.store(true, std::memory_order_release);
        rules_.DisableNewCaptures();
        return;
    }
    ownedSyntheticReleases_[ownedSyntheticReleaseCount_++] = {
        batch.outputDevice, Transition::Up, batch.outputCode, 0, 0};
    unresolvedSyntheticReleases_.store(
        ownedSyntheticReleaseCount_, std::memory_order_relaxed);
}

void HookRuntime::ForgetOwnedRelease(DeviceKind device, DWORD code) noexcept {
    for (std::size_t index = 0; index < ownedSyntheticReleaseCount_; ++index) {
        if (ownedSyntheticReleases_[index].device != device ||
            ownedSyntheticReleases_[index].code != code) {
            continue;
        }
        ownedSyntheticReleases_[index] =
            ownedSyntheticReleases_[ownedSyntheticReleaseCount_ - 1];
        --ownedSyntheticReleaseCount_;
        unresolvedSyntheticReleases_.store(
            ownedSyntheticReleaseCount_, std::memory_order_relaxed);
        return;
    }
}

void HookRuntime::ReleaseOwnedSyntheticState(unsigned int maximumAttempts) noexcept {
    for (unsigned int attempt = 0;
         attempt < maximumAttempts && ownedSyntheticReleaseCount_ != 0;
         ++attempt) {
        std::size_t retainedCount = 0;
        const std::size_t originalCount = ownedSyntheticReleaseCount_;
        for (std::size_t index = 0; index < originalCount; ++index) {
            ActionBatch releaseBatch{};
            releaseBatch.sourceSequence = 0;
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
            record.circuitBreakerOpen =
                circuitBreakerOpen_.load(std::memory_order_acquire);
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

void HookRuntime::SignalActionWorker() noexcept {
    if (actionEvent_ != nullptr) {
        SetEvent(actionEvent_);
    }
}

void HookRuntime::PublishHookDiagnostic(
    HookDiagnosticRecord& record,
    std::int64_t startCounter) noexcept {
    const std::int64_t elapsed = (std::max)(
        static_cast<std::int64_t>(0), ReadPerformanceCounter() - startCounter);
    const std::uint64_t microseconds = static_cast<std::uint64_t>(
        (elapsed * 1000000LL) / performanceFrequency_);
    record.processingMicroseconds = static_cast<std::uint32_t>((std::min)(
        microseconds,
        static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
    const std::uint64_t previous = maximumHookMicroseconds_.load(std::memory_order_relaxed);
    if (microseconds > previous) {
        maximumHookMicroseconds_.store(microseconds, std::memory_order_relaxed);
    }
    if (ShouldPublishHookDiagnostic(record, options_.traceInput)) {
        diagnosticLog_.TryPushHook(record);
    }
}

void HookRuntime::AccumulateMouseMoveDiagnostic(
    HookDiagnosticRecord record,
    std::int64_t startCounter) noexcept {
    constexpr std::uint32_t kMouseMoveAggregationCount = 64;
    if (record.origin == InputOrigin::SelfInjected) {
        FlushMouseMoveDiagnostic(startCounter);
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
        FlushMouseMoveDiagnostic(startCounter);
        pendingMouseMoveRecord_ = record;
        pendingMouseMoveCount_ = 1;
    } else {
        ++pendingMouseMoveCount_;
    }
    if (pendingMouseMoveCount_ >= kMouseMoveAggregationCount) {
        FlushMouseMoveDiagnostic(startCounter);
    }
    RecordMaximumHookDuration(startCounter);
}

void HookRuntime::FlushMouseMoveDiagnostic(std::int64_t startCounter) noexcept {
    if (pendingMouseMoveCount_ == 0) {
        return;
    }
    pendingMouseMoveRecord_.aggregateCount = pendingMouseMoveCount_;
    PublishHookDiagnostic(pendingMouseMoveRecord_, startCounter);
    pendingMouseMoveRecord_ = {};
    pendingMouseMoveCount_ = 0;
}

void HookRuntime::RecordMaximumHookDuration(
    std::int64_t startCounter) noexcept {
    const std::int64_t elapsed = (std::max)(
        static_cast<std::int64_t>(0), ReadPerformanceCounter() - startCounter);
    const std::uint64_t microseconds = static_cast<std::uint64_t>(
        (elapsed * 1000000LL) / performanceFrequency_);
    const std::uint64_t previous = maximumHookMicroseconds_.load(std::memory_order_relaxed);
    if (microseconds > previous) {
        maximumHookMicroseconds_.store(microseconds, std::memory_order_relaxed);
    }
}

bool HookRuntime::CreateRuntimeEvents(std::wstring& errorMessage) noexcept {
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    actionReadyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    producerDoneEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    stoppedEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    shutdownEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    actionEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (readyEvent_ != nullptr && actionReadyEvent_ != nullptr && producerDoneEvent_ != nullptr &&
        stoppedEvent_ != nullptr &&
        shutdownEvent_ != nullptr && actionEvent_ != nullptr) {
        return true;
    }

    const DWORD error = GetLastError();
    CloseRuntimeEvents();
    errorMessage = L"Cannot create runtime synchronization events. Win32 error " +
                   std::to_wstring(error) + L".";
    return false;
}

void HookRuntime::CloseRuntimeEvents() noexcept {
    HANDLE* events[] = {
        &readyEvent_, &actionReadyEvent_, &producerDoneEvent_, &stoppedEvent_,
        &shutdownEvent_, &actionEvent_};
    for (HANDLE* event : events) {
        if (*event != nullptr) {
            CloseHandle(*event);
            *event = nullptr;
        }
    }
}

DWORD HookRuntime::TargetPid() const noexcept {
    return targetContext_ == nullptr ? 0 : targetContext_->TargetPid();
}

bool HookRuntime::TargetIsReadyAndForeground() const noexcept {
    return targetContext_ != nullptr && targetContext_->IsTargetForeground();
}

bool HookRuntime::TargetPointerRouteIsSafe(
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
