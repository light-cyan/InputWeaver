#include "program_runtime_session.hpp"

#include "platform/windows/diagnostics/diagnostic_log.hpp"
#include "platform/windows/runtime/input_injector.hpp"
#include "platform/windows/runtime/low_level_hooks.hpp"
#include "platform/windows/runtime/process_context.hpp"
#include "platform/windows/runtime/runtime_control_catalog.hpp"
#include "platform/windows/runtime/runtime_process_launcher.hpp"
#include "platform/windows/runtime/runtime_route_adapter.hpp"
#include "platform/windows/runtime/windows_output_queue.hpp"
#include "program/compiled_program.hpp"
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

[[nodiscard]] WindowsProcessId ForegroundProcessId() noexcept
{
    const HWND window = GetForegroundWindow();
    if (window == nullptr) {
        return 0U;
    }
    DWORD processId = 0U;
    GetWindowThreadProcessId(window, &processId);
    return static_cast<WindowsProcessId>(processId);
}

void PopulateInjectionIdentity(
    const WindowsOutputItem& item,
    WindowsProcessId targetPid,
    InjectionDiagnosticRecord& record) noexcept
{
    record.sourceSequence = item.sourceSequence;
    record.outputStateGeneration = item.outputStateGeneration;
    record.targetPid = targetPid;
    record.outputCode = item.outputCode;
    record.outputDevice = item.recipe.kind == WindowsOutputKind::MouseButton
        ? DeviceKind::Mouse
        : DeviceKind::Keyboard;
    record.outputTransition = item.transition == WindowsOutputTransition::Up
        ? Transition::Up
        : Transition::Down;
}

[[nodiscard]] bool IsReleaseOutput(const WindowsOutputItem& item) noexcept
{
    return item.transition == WindowsOutputTransition::Up;
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
        result.queuedOutputs = queuedOutputs.load(std::memory_order_relaxed);
        result.cancelledOutputs = cancelledOutputs.load(std::memory_order_relaxed);
        result.injectionFailures = injectionFailures.load(std::memory_order_relaxed);
        result.maximumHookMicroseconds = maximumHookMicroseconds.load(std::memory_order_relaxed);
        result.forwardedOutsideTarget = forwardedOutsideTarget.load(std::memory_order_relaxed);
        result.circuitBreakerOpen = circuitBreakerOpen.load(std::memory_order_acquire);
        return result;
    }

    InputDecision HandleInput(
        const WindowsNativeInputEvent& event,
        bool lowerIntegrityInjected,
        std::int64_t startCounter) noexcept override
    {
        HookDiagnosticRecord record{};
        record.sequence = nextHookSequence.fetch_add(1U, std::memory_order_relaxed);
        record.qpcTimestamp = startCounter;
        record.foregroundPid = ForegroundProcessId();
        record.rawFlags = event.hookFlags;
        record.code = event.virtualKey;
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
            && targetContext != nullptr
            && targetContext->IsValid()
            && !targetContext->IsTargetForeground();
        decision = runtime->HandleInput(normalized);
        if (outsideExecutableTarget && decision == InputDecision::Forward) {
            forwardedOutsideTarget.fetch_add(1U, std::memory_order_relaxed);
        }
        currentGeneration.store(
            runtime->Generation(),
            std::memory_order_release);
        record.suppressed = decision == InputDecision::Suppress;
        DrainRuntimeDiagnostics();
        if (runtime->ExitRequested() || runtime->FatalShutdownRequested()) {
            RequestStop();
        }
        PublishHookDiagnostic(
            record,
            startCounter,
            normalized.control.IsValid());
        return decision;
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

    void FlushDiagnostics() noexcept override
    {
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
        const WindowsOutputItem& item) noexcept
    {
        if (context == nullptr) {
            return RuntimeOutputResult::Failed;
        }
        Impl& session = *static_cast<Impl*>(context);
        if (!session.outputQueue.TryPush(item)) {
            session.SignalStopOnly();
            return RuntimeOutputResult::CapacityRejected;
        }
        session.queuedOutputs.fetch_add(1U, std::memory_order_relaxed);
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
                && outputQueue.Empty()) {
                break;
            }
            (void)WaitForMultipleObjects(2U, waitHandles, FALSE, INFINITE);
        }
        SetEvent(outputStoppedEvent);
    }

    void DrainOutputQueue() noexcept
    {
        WindowsOutputItem item{};
        while (outputQueue.TryPop(item)) {
            ProcessOutput(item);
        }
    }

    void ProcessOutput(const WindowsOutputItem& item) noexcept
    {
        InjectionDiagnosticRecord record{};
        PopulateInjectionIdentity(
            item,
            targetContext == nullptr ? 0U : targetContext->TargetPid(),
            record);
        record.qpcTimestamp = ReadPerformanceCounter();
        const bool release = IsReleaseOutput(item);
        const std::uint64_t liveGeneration = runtime == nullptr
            ? currentGeneration.load(std::memory_order_acquire)
            : runtime->Generation();
        currentGeneration.store(liveGeneration, std::memory_order_release);
        if (!release && shutdownRequested.load(std::memory_order_acquire)) {
            record.cancelledForShutdown = true;
        } else if (!release && circuitBreakerOpen.load(std::memory_order_acquire)) {
            record.cancelledForCircuitBreaker = true;
        } else if (!release
            && item.outputStateGeneration
                != liveGeneration) {
            record.cancelledForGeneration = true;
        } else if (!release && !TargetRouteAllows(item)) {
            record.cancelledForTarget = true;
            if (targetContext != nullptr && !targetContext->IsTargetAlive()) {
                NotifyTargetLostFromOutput();
                SignalStopOnly();
            } else if (targetContext != nullptr
                && !targetContext->IsTargetForeground()) {
                SetTargetEligible(false);
            }
        } else if (!release && runtime != nullptr
            && item.outputStateGeneration != runtime->Generation()) {
            record.cancelledForGeneration = true;
        } else {
            ExecuteOutput(item, record);
            return;
        }
        cancelledOutputs.fetch_add(1U, std::memory_order_relaxed);
        record.circuitBreakerOpen = circuitBreakerOpen.load(std::memory_order_acquire);
        (void)diagnosticLog.TryPushInjection(record);
    }

    [[nodiscard]] bool TargetRouteAllows(
        const WindowsOutputItem& item) const noexcept
    {
        if (targetContext == nullptr) {
            return true;
        }
        return targetContext->IsTargetAlive()
            && targetContext->IsTargetForeground()
            && (!item.requiresPointerTarget
                || targetContext->IsTargetPointerTargetAtCursor());
    }

    void ExecuteOutput(
        const WindowsOutputItem& item,
        InjectionDiagnosticRecord& record) noexcept
    {
        const InjectionResult result = injector.Inject(item);
        record.requested = result.requested;
        record.sent = result.sent;
        record.win32Error = result.error;
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
        metricsSnapshot.queuedOutputs = queuedOutputs.load(std::memory_order_relaxed);
        metricsSnapshot.cancelledOutputs = cancelledOutputs.load(std::memory_order_relaxed);
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
    std::atomic<std::uint64_t> queuedOutputs{0U};
    std::atomic<std::uint64_t> cancelledOutputs{0U};
    std::atomic<std::uint64_t> injectionFailures{0U};
    std::atomic<std::uint64_t> maximumHookMicroseconds{0U};
    std::atomic<std::uint64_t> forwardedOutsideTarget{0U};
    std::atomic<bool> circuitBreakerOpen{false};
    std::int64_t performanceFrequency{1};
    WindowsProgramRuntimeSessionMetrics metricsSnapshot{};
    WindowsOutputQueue outputQueue;
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
