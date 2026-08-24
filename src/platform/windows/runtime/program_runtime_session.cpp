#include "program_runtime_session.hpp"

#include "platform/windows/diagnostics/diagnostic_log.hpp"
#include "input/input_types.hpp"
#include "platform/windows/runtime/input_injector.hpp"
#include "platform/windows/runtime/low_level_hooks.hpp"
#include "platform/windows/runtime/process_context.hpp"
#include "platform/windows/runtime/runtime_control_catalog.hpp"
#include "platform/windows/runtime/runtime_process_launcher.hpp"
#include "platform/windows/runtime/runtime_route_adapter.hpp"
#include "program/compiled_program.hpp"
#include "runtime/action_queue.hpp"
#include "runtime/program_runtime.hpp"
#include "support/stop_request.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace inputweaver {
namespace {

[[nodiscard]] std::int64_t ReadPerformanceCounter() noexcept
{
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

[[nodiscard]] ProcessId ForegroundProcessId() noexcept
{
    const HWND window = GetForegroundWindow();
    if (window == nullptr) {
        return 0U;
    }
    DWORD processId = 0U;
    GetWindowThreadProcessId(window, &processId);
    return static_cast<ProcessId>(processId);
}

void PopulateInjectionIdentity(
    const ActionBatch& batch,
    InjectionDiagnosticRecord& record) noexcept
{
    record.sourceSequence = batch.sourceSequence;
    record.outputStateGeneration = batch.outputStateGeneration;
    record.targetPid = batch.targetPid;
    record.outputCode = batch.outputCode;
    record.outputDevice = batch.outputDevice;
    if (batch.actionCount != 0U) {
        record.outputTransition = batch.actions[0].transition;
    }
}

[[nodiscard]] bool IsReleaseBatch(const ActionBatch& batch) noexcept
{
    return batch.actionCount != 0U
        && batch.actions[0].transition == Transition::Up;
}

[[nodiscard]] std::wstring FormatActivationError(
    const RuntimeActivationError& error)
{
    const std::wstring policyDetail =
        error.code == RuntimeActivationErrorCode::ProcessLaunchDenied
        ? L" Process launch is denied; rerun compiled-program mode with --allow-exec to grant it."
        : L"";
    return L"Cannot activate the compiled program. code="
        + std::to_wstring(static_cast<unsigned int>(error.code))
        + L" subject=" + std::to_wstring(error.subject)
        + L" required=" + std::to_wstring(error.required)
        + L" available=" + std::to_wstring(error.available) + L"."
        + policyDetail;
}

} // namespace

struct WindowsProgramRuntimeSession::Impl final : LowLevelInputSink {
    Impl(
        WindowsProgramRuntimeSessionOptions sessionOptions,
        TargetProcessContext* sessionTargetContext,
        DiagnosticLog& sessionDiagnosticLog) noexcept
        : options(sessionOptions),
          targetContext(sessionTargetContext),
          diagnosticLog(sessionDiagnosticLog),
          injector(sessionOptions.selfTag),
          injectionCircuitBreaker(3U)
    {
        LARGE_INTEGER frequency{};
        if (QueryPerformanceFrequency(&frequency) != FALSE
            && frequency.QuadPart > 0) {
            performanceFrequency = frequency.QuadPart;
        }
    }

    ~Impl() override
    {
        RequestStop();
        Wait();
        lowLevelHooks.reset();
        inputAdapter.reset();
        runtime.reset();
        outputPort.reset();
        clock.reset();
        processLauncher.reset();
        routePort.reset();
        controlCatalog.reset();
        CloseEvents();
    }

    bool Start(
        std::shared_ptr<const CompiledProgram> program,
        std::wstring& errorMessage)
    {
        if (started.exchange(true, std::memory_order_acq_rel)) {
            errorMessage = L"The compiled-program runtime has already been started.";
            return false;
        }
        if (options.selfTag == 0U) {
            errorMessage = L"The self-injection tag must be nonzero.";
            return false;
        }
        if (program == nullptr) {
            errorMessage = L"The compiled program is missing.";
            return false;
        }
        if (!CreateEvents(errorMessage) || !CreateComponents(errorMessage)) {
            return false;
        }

        const RuntimeActivationResult activation = runtime->Activate(
            std::move(program),
            options.effectiveTargetKind);
        if (!activation.activated) {
            errorMessage = FormatActivationError(activation.error);
            return false;
        }
        currentGeneration.store(runtime->Generation(), std::memory_order_release);
        if (!StartOutputThread(errorMessage)) {
            return false;
        }
        if (!runtime->StartTaskThread()) {
            errorMessage = L"Cannot start the compiled-program task thread.";
            SignalStopOnly();
            return false;
        }
        if (!lowLevelHooks->Start(errorMessage)) {
            SignalStopOnly();
            return false;
        }
        return true;
    }

    void SignalStopOnly() noexcept
    {
        shutdownRequested.store(true, std::memory_order_release);
        if (shutdownEvent != nullptr) {
            SetEvent(shutdownEvent);
        }
        if (outputWakeEvent != nullptr) {
            SetEvent(outputWakeEvent);
        }
        if (lowLevelHooks != nullptr) {
            lowLevelHooks->Wake();
        }
    }

    void RequestStop() noexcept
    {
        SignalStopOnly();
        std::unique_lock runtimeLock(runtimeCallMutex, std::try_to_lock);
        if (runtimeLock.owns_lock()) {
            NotifyRuntimeShutdownLocked();
        }
    }

    void NotifyRuntimeShutdownLocked() noexcept
    {
        bool expected = false;
        if (runtime != nullptr
            && runtimeShutdownNotified.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            runtime->RequestShutdown();
            currentGeneration.store(
                runtime->Generation(),
                std::memory_order_release);
            DrainRuntimeDiagnostics();
        }
    }

    void Wait() noexcept
    {
        if (waitCompleted.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        RequestStop();
        if (lowLevelHooks != nullptr) {
            lowLevelHooks->Wait();
        } else if (hookProducerDoneEvent != nullptr) {
            SetEvent(hookProducerDoneEvent);
        }

        if (runtime != nullptr) {
            const std::lock_guard runtimeLock(runtimeCallMutex);
            NotifyRuntimeShutdownLocked();
            runtime->StopTaskThread();
            for (unsigned int attempt = 0U; attempt < 3U; ++attempt) {
                (void)runtime->Pump(1024U);
            }
            metricsSnapshot.runtime = runtime->Metrics();
            DrainRuntimeDiagnostics();
            runtime->Deactivate();
            DrainRuntimeDiagnostics();
        }
        if (outputProducerDoneEvent != nullptr) {
            SetEvent(outputProducerDoneEvent);
        }
        if (outputWakeEvent != nullptr) {
            SetEvent(outputWakeEvent);
        }
        if (outputThread.joinable()) {
            outputThread.join();
        }
        CaptureSessionMetrics();
    }

    [[nodiscard]] HANDLE StoppedEvent() const noexcept
    {
        return lowLevelHooks == nullptr ? nullptr : lowLevelHooks->StoppedEvent();
    }

    [[nodiscard]] WindowsProgramRuntimeSessionMetrics Metrics() const noexcept
    {
        WindowsProgramRuntimeSessionMetrics result = metricsSnapshot;
        if (!waitCompleted.load(std::memory_order_acquire) && runtime != nullptr) {
            result.runtime = runtime->Metrics();
        }
        result.hookEvents = hookEvents.load(std::memory_order_relaxed);
        result.queuedBatches = queuedBatches.load(std::memory_order_relaxed);
        result.cancelledBatches = cancelledBatches.load(std::memory_order_relaxed);
        result.injectionFailures = injectionFailures.load(std::memory_order_relaxed);
        result.maximumHookMicroseconds = maximumHookMicroseconds.load(std::memory_order_relaxed);
        result.forwardedOutsideTarget = forwardedOutsideTarget.load(std::memory_order_relaxed);
        result.circuitBreakerOpen = circuitBreakerOpen.load(std::memory_order_acquire);
        return result;
    }

    InputDecision HandleInput(
        const InputEvent& event,
        bool lowerIntegrityInjected,
        std::int64_t startCounter) noexcept override
    {
        HookDiagnosticRecord record{};
        record.sequence = nextHookSequence.fetch_add(1U, std::memory_order_relaxed);
        record.qpcTimestamp = startCounter;
        record.foregroundPid = ForegroundProcessId();
        record.rawFlags = event.flags;
        record.code = event.code;
        record.scanCode = event.scanCode;
        record.mouseData = event.mouseData;
        record.device = event.device;
        record.origin = event.origin;
        record.transition = event.transition;
        record.extraInfo = CategorizeExtraInfo(event.extraInfo, options.selfTag);
        record.lowerIntegrityInjected = lowerIntegrityInjected;
        hookEvents.fetch_add(1U, std::memory_order_relaxed);

        RuntimeInputEvent normalized = inputAdapter->Normalize(event);
        InputDecision decision = InputDecision::Forward;
        const bool outsideExecutableTarget =
            event.origin == InputOrigin::PhysicalCandidate
            && !normalized.forceStopRequested
            && targetContext != nullptr
            && targetContext->IsValid()
            && !targetContext->IsTargetForeground();
        if (outsideExecutableTarget) {
            forwardedOutsideTarget.fetch_add(1U, std::memory_order_relaxed);
        }
        decision = runtime->HandleInput(normalized);
        currentGeneration.store(
            runtime->Generation(),
            std::memory_order_release);
        record.suppressed = decision == InputDecision::Suppress;
        DrainRuntimeDiagnostics();
        if (normalized.forceStopRequested || runtime->FatalShutdownRequested()) {
            RequestStop();
        }
        PublishHookDiagnostic(
            record,
            startCounter,
            normalized.control.IsValid());
        return decision;
    }

    void SeedPhysicalState(
        DeviceKind device,
        ControlCode code,
        bool down) noexcept override
    {
        InputEvent event{};
        event.device = device;
        event.origin = InputOrigin::PhysicalCandidate;
        event.transition = down ? Transition::Down : Transition::Up;
        event.code = code;
        const RuntimeInputEvent normalized = inputAdapter->Normalize(event);
        if (normalized.control.IsValid()) {
            (void)runtime->SeedPhysicalState(normalized.control, down);
        }
    }

    void SetTargetEligible(bool eligible) noexcept override
    {
        if (shutdownRequested.load(std::memory_order_acquire)) {
            return;
        }
        const std::lock_guard runtimeLock(runtimeCallMutex);
        if (runtime != nullptr
            && !shutdownRequested.load(std::memory_order_acquire)) {
            runtime->SetTargetEligible(eligible);
            currentGeneration.store(
                runtime->Generation(),
                std::memory_order_release);
            DrainRuntimeDiagnostics();
        }
    }

    [[nodiscard]] bool SeedActivatedPhysicalState() noexcept override
    {
        return SeedActivatedKeyboardState();
    }

    [[nodiscard]] bool RequiresTargetEligibilityNotifications() const noexcept override
    {
        return targetContext != nullptr;
    }

    [[nodiscard]] bool HasCapturedInputs() const noexcept override
    {
        return runtime != nullptr
            && (runtime->HasActiveMappings() || runtime->HasOwnedOutputs());
    }

    void FlushDiagnostics(std::int64_t startCounter) noexcept override
    {
        (void)startCounter;
        DrainRuntimeDiagnostics();
    }

    static void RequestStopThunk(void* context) noexcept
    {
        if (context != nullptr) {
            static_cast<Impl*>(context)->RequestStop();
        }
    }

    static RuntimeOutputResult PublishThunk(
        void* context,
        const ActionBatch& batch) noexcept
    {
        if (context == nullptr) {
            return RuntimeOutputResult::Failed;
        }
        Impl& session = *static_cast<Impl*>(context);
        if (!session.actionQueue.TryPush(batch)) {
            session.SignalStopOnly();
            return RuntimeOutputResult::CapacityRejected;
        }
        session.queuedBatches.fetch_add(1U, std::memory_order_relaxed);
        SetEvent(session.outputWakeEvent);
        return RuntimeOutputResult::Accepted;
    }

    bool SeedActivatedKeyboardState() noexcept
    {
        for (std::size_t token = 0U;
             token < controlCatalog->BindingCount();
             ++token) {
            const win32::WindowsControlBinding* const binding =
                controlCatalog->Binding(token);
            if (binding == nullptr
                || binding->kind != win32::WindowsControlKind::Keyboard
                || (binding->requiredUses
                    & (ToControlUseBits(ControlUse::EventSource)
                        | ToControlUseBits(ControlUse::PhysicalState))) == 0U) {
                continue;
            }
            bool initialized = false;
            if (binding->initialStateQueryable && binding->virtualKey != 0U) {
                initialized = runtime->SeedPhysicalState(
                    binding->control,
                    (GetAsyncKeyState(
                        static_cast<int>(binding->virtualKey)) & 0x8000) != 0);
            } else {
                initialized = runtime->MarkPhysicalStateUnsynchronized(
                    binding->control);
            }
            if (!initialized) {
                return false;
            }
        }
        return true;
    }

    void NotifyTargetLostFromOutput() noexcept
    {
        if (shutdownRequested.load(std::memory_order_acquire)) {
            return;
        }
        const std::lock_guard runtimeLock(runtimeCallMutex);
        if (runtime == nullptr
            || shutdownRequested.load(std::memory_order_acquire)) {
            return;
        }
        runtime->NotifyTargetLost();
        currentGeneration.store(
            runtime->Generation(),
            std::memory_order_release);
        DrainRuntimeDiagnostics();
    }

    bool CreateEvents(std::wstring& errorMessage) noexcept
    {
        shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        hookProducerDoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        outputWakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        outputReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        outputProducerDoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        outputStoppedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (shutdownEvent != nullptr
            && hookProducerDoneEvent != nullptr
            && outputWakeEvent != nullptr
            && outputReadyEvent != nullptr
            && outputProducerDoneEvent != nullptr
            && outputStoppedEvent != nullptr) {
            return true;
        }
        const DWORD error = GetLastError();
        errorMessage = L"Cannot create compiled-program runtime events. Win32 error "
            + std::to_wstring(error) + L".";
        CloseEvents();
        return false;
    }

    void CloseEvents() noexcept
    {
        HANDLE* handles[] = {
            &shutdownEvent,
            &hookProducerDoneEvent,
            &outputWakeEvent,
            &outputReadyEvent,
            &outputProducerDoneEvent,
            &outputStoppedEvent};
        for (HANDLE* handle : handles) {
            if (*handle != nullptr) {
                CloseHandle(*handle);
                *handle = nullptr;
            }
        }
    }

    bool CreateComponents(std::wstring& errorMessage)
    {
        try {
            controlCatalog = std::make_unique<win32::WindowsControlCatalog>();
            routePort = std::make_unique<win32::WindowsRuntimeRoutePort>(
                targetContext);
            processLauncher = std::make_unique<win32::WindowsProcessLauncher>(
                options.permitProcessLaunch);
            clock = std::make_unique<SteadyRuntimeClock>();
            outputPort = std::make_unique<win32::WindowsRuntimeOutputPort>(
                *controlCatalog,
                targetContext == nullptr ? 0U : targetContext->TargetPid(),
                this,
                &Impl::PublishThunk);
            RuntimeCapacities capacities{};
            capacities.permitProcessLaunch = options.permitProcessLaunch;
            runtime = std::make_unique<ProgramRuntime>(
                capacities,
                *controlCatalog,
                *outputPort,
                *routePort,
                *processLauncher,
                *clock);
            inputAdapter = std::make_unique<win32::WindowsRuntimeInputAdapter>(
                *controlCatalog);
            const StopRequest stopRequest{this, &Impl::RequestStopThunk};
            lowLevelHooks = std::make_unique<LowLevelHooks>(
                options.selfTag,
                *this,
                targetContext,
                stopRequest,
                shutdownRequested,
                shutdownEvent,
                hookProducerDoneEvent);
        } catch (const std::exception&) {
            errorMessage = L"Cannot allocate the compiled-program runtime components.";
            return false;
        } catch (...) {
            errorMessage = L"Cannot create the compiled-program runtime components.";
            return false;
        }
        return true;
    }

    bool StartOutputThread(std::wstring& errorMessage)
    {
        try {
            outputThread = std::thread(&Impl::OutputThreadMain, this);
        } catch (...) {
            errorMessage = L"Cannot create the compiled-program output thread.";
            return false;
        }
        if (WaitForSingleObject(outputReadyEvent, 5000U) != WAIT_OBJECT_0) {
            SignalStopOnly();
            SetEvent(outputProducerDoneEvent);
            SetEvent(outputWakeEvent);
            outputThread.join();
            errorMessage = L"The compiled-program output thread did not become ready within five seconds.";
            return false;
        }
        return true;
    }

    void OutputThreadMain() noexcept
    {
        SetEvent(outputReadyEvent);
        const HANDLE waitHandles[] = {outputProducerDoneEvent, outputWakeEvent};
        for (;;) {
            DrainOutputQueue();
            if (WaitForSingleObject(outputProducerDoneEvent, 0U) == WAIT_OBJECT_0
                && actionQueue.Empty()) {
                break;
            }
            (void)WaitForMultipleObjects(2U, waitHandles, FALSE, INFINITE);
        }
        SetEvent(outputStoppedEvent);
    }

    void DrainOutputQueue() noexcept
    {
        ActionBatch batch{};
        while (actionQueue.TryPop(batch)) {
            ProcessOutputBatch(batch);
        }
    }

    void ProcessOutputBatch(const ActionBatch& batch) noexcept
    {
        InjectionDiagnosticRecord record{};
        PopulateInjectionIdentity(batch, record);
        record.qpcTimestamp = ReadPerformanceCounter();
        const bool release = IsReleaseBatch(batch);
        const std::uint64_t liveGeneration = runtime == nullptr
            ? currentGeneration.load(std::memory_order_acquire)
            : runtime->Generation();
        currentGeneration.store(liveGeneration, std::memory_order_release);
        if (!release && shutdownRequested.load(std::memory_order_acquire)) {
            record.cancelledForShutdown = true;
        } else if (!release && circuitBreakerOpen.load(std::memory_order_acquire)) {
            record.cancelledForCircuitBreaker = true;
        } else if (!release
            && batch.outputStateGeneration
                != liveGeneration) {
            record.cancelledForGeneration = true;
        } else if (!release && !TargetRouteAllows(batch)) {
            record.cancelledForTarget = true;
            if (targetContext != nullptr && !targetContext->IsTargetAlive()) {
                NotifyTargetLostFromOutput();
                SignalStopOnly();
            } else if (targetContext != nullptr
                && !targetContext->IsTargetForeground()) {
                SetTargetEligible(false);
            }
        } else if (!release && runtime != nullptr
            && batch.outputStateGeneration != runtime->Generation()) {
            record.cancelledForGeneration = true;
        } else {
            ExecuteOutputBatch(batch, record);
            return;
        }
        cancelledBatches.fetch_add(1U, std::memory_order_relaxed);
        record.circuitBreakerOpen = circuitBreakerOpen.load(std::memory_order_acquire);
        (void)diagnosticLog.TryPushInjection(record);
    }

    [[nodiscard]] bool TargetRouteAllows(const ActionBatch& batch) const noexcept
    {
        if (targetContext == nullptr) {
            return true;
        }
        return batch.targetPid == targetContext->TargetPid()
            && targetContext->IsTargetAlive()
            && targetContext->IsTargetForeground()
            && (!batch.requiresPointerTarget
                || targetContext->IsTargetPointerTargetAtCursor());
    }

    void ExecuteOutputBatch(
        const ActionBatch& batch,
        InjectionDiagnosticRecord& record) noexcept
    {
        const InjectionResult result = injector.Inject(batch);
        record.requested = result.requested;
        record.sent = result.sent;
        record.win32Error = result.error;
        record.cleanupRequested = result.cleanupRequested;
        record.cleanupSent = result.cleanupSent;
        record.cleanupError = result.cleanupError;
        if (result.Succeeded()) {
            injectionCircuitBreaker.RecordSuccess();
        } else {
            injectionFailures.fetch_add(1U, std::memory_order_relaxed);
            if (injectionCircuitBreaker.RecordFailure()) {
                circuitBreakerOpen.store(true, std::memory_order_release);
                SignalStopOnly();
            }
        }
        record.circuitBreakerOpen = circuitBreakerOpen.load(std::memory_order_acquire);
        (void)diagnosticLog.TryPushInjection(record);
    }

    void PublishHookDiagnostic(
        HookDiagnosticRecord& record,
        std::int64_t startCounter,
        bool activatedControl) noexcept
    {
        const std::int64_t elapsed = (std::max)(
            std::int64_t{0}, ReadPerformanceCounter() - startCounter);
        const std::uint64_t microseconds = static_cast<std::uint64_t>(
            (elapsed * 1000000LL) / performanceFrequency);
        record.processingMicroseconds = static_cast<std::uint32_t>((std::min)(
            microseconds,
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::uint32_t>::max)())));
        std::uint64_t maximum = maximumHookMicroseconds.load(
            std::memory_order_relaxed);
        while (microseconds > maximum
            && !maximumHookMicroseconds.compare_exchange_weak(
                maximum,
                microseconds,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
        }
        if (ShouldPublishProgramHookDiagnostic(
                record,
                options.traceInput,
                activatedControl)) {
            (void)diagnosticLog.TryPushHook(record);
        }
    }

    void DrainRuntimeDiagnostics() noexcept
    {
        if (runtime == nullptr) {
            return;
        }
        RuntimeDiagnosticRecord record{};
        while (runtime->TryPopDiagnostic(record)) {
            (void)diagnosticLog.TryPushRuntime(record);
        }
    }

    void CaptureSessionMetrics() noexcept
    {
        metricsSnapshot.hookEvents = hookEvents.load(std::memory_order_relaxed);
        metricsSnapshot.queuedBatches = queuedBatches.load(std::memory_order_relaxed);
        metricsSnapshot.cancelledBatches = cancelledBatches.load(std::memory_order_relaxed);
        metricsSnapshot.injectionFailures = injectionFailures.load(std::memory_order_relaxed);
        metricsSnapshot.maximumHookMicroseconds = maximumHookMicroseconds.load(std::memory_order_relaxed);
        metricsSnapshot.forwardedOutsideTarget = forwardedOutsideTarget.load(std::memory_order_relaxed);
        metricsSnapshot.circuitBreakerOpen = circuitBreakerOpen.load(std::memory_order_acquire);
    }

    WindowsProgramRuntimeSessionOptions options;
    TargetProcessContext* targetContext;
    DiagnosticLog& diagnosticLog;
    HANDLE shutdownEvent{};
    HANDLE hookProducerDoneEvent{};
    HANDLE outputWakeEvent{};
    HANDLE outputReadyEvent{};
    HANDLE outputProducerDoneEvent{};
    HANDLE outputStoppedEvent{};
    std::atomic<bool> started{false};
    std::atomic<bool> shutdownRequested{false};
    std::atomic<bool> runtimeShutdownNotified{false};
    std::mutex runtimeCallMutex;
    std::atomic<bool> waitCompleted{false};
    std::atomic<std::uint64_t> currentGeneration{0U};
    std::atomic<std::uint64_t> nextHookSequence{1U};
    std::atomic<std::uint64_t> hookEvents{0U};
    std::atomic<std::uint64_t> queuedBatches{0U};
    std::atomic<std::uint64_t> cancelledBatches{0U};
    std::atomic<std::uint64_t> injectionFailures{0U};
    std::atomic<std::uint64_t> maximumHookMicroseconds{0U};
    std::atomic<std::uint64_t> forwardedOutsideTarget{0U};
    std::atomic<bool> circuitBreakerOpen{false};
    std::int64_t performanceFrequency{1};
    WindowsProgramRuntimeSessionMetrics metricsSnapshot{};
    ActionQueue actionQueue;
    InputInjector injector;
    InjectionCircuitBreaker injectionCircuitBreaker;
    std::thread outputThread;
    std::unique_ptr<win32::WindowsControlCatalog> controlCatalog;
    std::unique_ptr<win32::WindowsRuntimeRoutePort> routePort;
    std::unique_ptr<win32::WindowsProcessLauncher> processLauncher;
    std::unique_ptr<SteadyRuntimeClock> clock;
    std::unique_ptr<win32::WindowsRuntimeOutputPort> outputPort;
    std::unique_ptr<ProgramRuntime> runtime;
    std::unique_ptr<win32::WindowsRuntimeInputAdapter> inputAdapter;
    std::unique_ptr<LowLevelHooks> lowLevelHooks;
};

WindowsProgramRuntimeSession::WindowsProgramRuntimeSession(
    WindowsProgramRuntimeSessionOptions options,
    TargetProcessContext* targetContext,
    DiagnosticLog& diagnosticLog)
    : impl_(std::make_unique<Impl>(options, targetContext, diagnosticLog))
{
}

WindowsProgramRuntimeSession::~WindowsProgramRuntimeSession() = default;

bool WindowsProgramRuntimeSession::Start(
    std::shared_ptr<const CompiledProgram> program,
    std::wstring& errorMessage)
{
    return impl_->Start(std::move(program), errorMessage);
}

void WindowsProgramRuntimeSession::RequestStop() noexcept
{
    impl_->RequestStop();
}

void WindowsProgramRuntimeSession::Wait() noexcept
{
    impl_->Wait();
}

HANDLE WindowsProgramRuntimeSession::StoppedEvent() const noexcept
{
    return impl_->StoppedEvent();
}

WindowsProgramRuntimeSessionMetrics WindowsProgramRuntimeSession::Metrics() const noexcept
{
    return impl_->Metrics();
}

} // namespace inputweaver
