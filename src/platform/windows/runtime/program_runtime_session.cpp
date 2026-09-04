#include "program_runtime_session.hpp"

#include "platform/windows/debug/debug_server.hpp"
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

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace inputweaver {
namespace {

static_assert(
    kWindowsOutputQueueCapacity
    >= static_cast<std::size_t>(RuntimeCapacities{}.maximumControls)
        + RuntimeCapacities{}.maximumTaskOutputsWithoutSuspension);

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

[[nodiscard]] bool IsDebugInputTransition(Transition transition) noexcept
{
    return transition == Transition::Down || transition == Transition::Up;
}

[[nodiscard]] bool IsMouseButtonVirtualKey(
    WindowsVirtualKey virtualKey) noexcept
{
    return virtualKey == VK_LBUTTON
        || virtualKey == VK_RBUTTON
        || virtualKey == VK_MBUTTON
        || virtualKey == VK_XBUTTON1
        || virtualKey == VK_XBUTTON2;
}

[[nodiscard]] bool IsAliasedModifierVirtualKey(
    WindowsVirtualKey virtualKey) noexcept
{
    return virtualKey == VK_SHIFT
        || virtualKey == VK_CONTROL
        || virtualKey == VK_MENU;
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
        DiagnosticLog& sessionDiagnosticLog)
        : options(std::move(sessionOptions)),
          targetContext(
              options.effectiveTargetKind == TargetSelectorKind::Executable
                  ? &ownedTargetContext
                  : nullptr),
          diagnosticLog(sessionDiagnosticLog),
          processExclusion(options.excludedProcessSelector),
          injector(options.selfTag, &::SendInput, options.dryRun),
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
        activeProgram.reset();
        outputPort.reset();
        clock.reset();
        processLauncher.reset();
        routePort.reset();
        controlCatalog.reset();
        debugServer.reset();
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
        if (!CreateEvents(errorMessage)
            || !StartDebugServer(program, errorMessage)
            || !CreateComponents(errorMessage)) {
            return false;
        }

        if (debugServer != nullptr) {
            activeProgram = program;
        }
        const RuntimeActivationResult activation = runtime->Activate(
            std::move(program),
            options.effectiveTargetKind);
        if (!activation.activated) {
            RuntimeDiagnosticRecord record{};
            record.kind = RuntimeDiagnosticKind::ActivationFailure;
            record.subject = activation.error.subject;
            record.detail = static_cast<std::uint32_t>(activation.error.code);
            record.required = activation.error.required;
            record.available = activation.error.available;
            (void)diagnosticLog.TryPushRuntime(record);
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
        if (controlRequestEvent != nullptr) {
            SetEvent(controlRequestEvent);
        }
    }

    void RequestStop() noexcept
    {
        SignalStopOnly();
        const std::lock_guard runtimeLock(runtimeCallMutex);
        if (runtime == nullptr || runtimeShutdownNotified) {
            return;
        }
        runtimeShutdownNotified = true;
        runtime->RequestShutdown();
        currentGeneration.store(
            runtime->Generation(),
            std::memory_order_release);
        DrainRuntimeDiagnostics();
    }

    void Wait() noexcept
    {
        std::call_once(waitOnce, [this] {
            const std::lock_guard metricsLock(metricsMutex);
            RequestStop();
            if (lowLevelHooks != nullptr) {
                lowLevelHooks->Wait();
            } else if (hookProducerDoneEvent != nullptr) {
                SetEvent(hookProducerDoneEvent);
            }

            if (runtime != nullptr) {
                const std::lock_guard runtimeLock(runtimeCallMutex);
                runtime->StopTaskThread();
                for (unsigned int attempt = 0U; attempt < 3U; ++attempt) {
                    (void)runtime->Pump(1024U);
                }
                metricsSnapshot.runtime = runtime->Metrics();
                metricsSnapshot.fatalShutdown =
                    fatalShutdownObserved.load(std::memory_order_acquire)
                    || runtime->FatalShutdownRequested();
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
            if (debugServer != nullptr) {
                debugServer->Stop();
            }
            CaptureSessionMetrics();
            waitCompleted = true;
        });
    }

    [[nodiscard]] HANDLE StoppedEvent() const noexcept
    {
        return lowLevelHooks == nullptr ? nullptr : lowLevelHooks->StoppedEvent();
    }

    [[nodiscard]] HANDLE TargetLostEvent() const noexcept
    {
        return targetLostEvent;
    }

    [[nodiscard]] WindowsProgramRuntimeSessionMetrics Metrics() const noexcept
    {
        const std::lock_guard metricsLock(metricsMutex);
        WindowsProgramRuntimeSessionMetrics result = metricsSnapshot;
        if (!waitCompleted && runtime != nullptr) {
            result.runtime = runtime->Metrics();
            result.fatalShutdown =
                fatalShutdownObserved.load(std::memory_order_acquire)
                || runtime->FatalShutdownRequested();
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
        const win32::DebugInputCorrelation debugCorrelation =
            debugServer != nullptr && IsDebugInputTransition(event.transition)
            ? debugServer->BeginInput()
            : win32::DebugInputCorrelation{};
        normalized.debugCaptureEpoch = debugCorrelation.captureEpoch;
        normalized.debugInputSequence = debugCorrelation.inputSequence;
        InputDecision decision = InputDecision::Forward;
        bool outsideExecutableTarget = false;
        if (event.origin == InputOrigin::PhysicalCandidate
            && targetContext != nullptr) {
            const std::lock_guard targetLock(targetContextMutex);
            outsideExecutableTarget = targetContext->IsValid()
                && !targetContext->IsTargetForeground();
        }
        decision = runtime->HandleInput(normalized);
        if (outsideExecutableTarget && decision == InputDecision::Forward) {
            forwardedOutsideTarget.fetch_add(1U, std::memory_order_relaxed);
        }
        currentGeneration.store(
            runtime->Generation(),
            std::memory_order_release);
        record.suppressed = decision == InputDecision::Suppress;
        if (debugServer != nullptr && debugCorrelation.Active()) {
            debug::InputEventPayload input{};
            PopulateDebugInput(
                event,
                normalized,
                decision == InputDecision::Suppress
                    ? debug::InputDisposition::Suppress
                    : debug::InputDisposition::Forward,
                input);
            (void)debugServer->PublishInput(debugCorrelation, input);
        }
        DrainRuntimeDiagnostics();
        if (runtime->FatalShutdownRequested()) {
            fatalShutdownObserved.store(true, std::memory_order_release);
            RequestStop();
        } else if (runtime->ExitRequested()) {
            RequestStop();
        }
        PublishHookDiagnostic(
            record,
            startCounter,
            normalized.control.IsValid());
        return options.dryRun ? InputDecision::Forward : decision;
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

    void TargetLost() noexcept override
    {
        if (targetContext == nullptr
            || shutdownRequested.load(std::memory_order_acquire)) {
            return;
        }
        {
            const std::lock_guard targetLock(targetContextMutex);
            if (!targetContext->IsValid()) {
                return;
            }
            targetContext->Reset();
        }
        {
            const std::lock_guard runtimeLock(runtimeCallMutex);
            if (runtime != nullptr
                && !shutdownRequested.load(std::memory_order_acquire)) {
                runtime->NotifyTargetLost();
                currentGeneration.store(
                    runtime->Generation(),
                    std::memory_order_release);
                DrainRuntimeDiagnostics();
            }
        }
        if (targetLostEvent != nullptr) {
            SetEvent(targetLostEvent);
        }
    }

    [[nodiscard]] bool SeedActivatedPhysicalState() noexcept override
    {
        return SeedActivatedPhysicalControls();
    }

    void ProcessControlRequests() noexcept override
    {
        if (debugServer == nullptr) {
            return;
        }
        const win32::DebugCaptureRequest request =
            debugServer->TakeCaptureRequest();
        if (request == win32::DebugCaptureRequest::Stop) {
            debugServer->EndCapture();
            return;
        }
        if (request != win32::DebugCaptureRequest::Start) {
            return;
        }
        std::array<debug::InputEventPayload, 256U> initialInputs{};
        const std::size_t count = SampleDebugInitialInputs(initialInputs);
        (void)debugServer->BeginCapture(
            std::span<const debug::InputEventPayload>{initialInputs}.first(
                count));
    }

    [[nodiscard]] bool RequiresTargetEligibilityNotifications() const noexcept override
    {
        return targetContext != nullptr || processExclusion.Enabled();
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

    static void WakeDebugInputThreadThunk(void* context) noexcept
    {
        if (context == nullptr) {
            return;
        }
        const HANDLE event = static_cast<Impl*>(context)->controlRequestEvent;
        if (event != nullptr) {
            SetEvent(event);
        }
    }

    static void SignalStopThunk(void* context) noexcept
    {
        if (context != nullptr) {
            static_cast<Impl*>(context)->SignalStopOnly();
        }
    }

    static void FatalStopThunk(void* context) noexcept
    {
        if (context == nullptr) {
            return;
        }
        Impl& session = *static_cast<Impl*>(context);
        session.fatalShutdownObserved.store(true, std::memory_order_release);
        session.SignalStopOnly();
    }

    [[nodiscard]] bool AttachTarget(
        TargetProcessContext&& replacement) noexcept
    {
        if (targetContext == nullptr
            || !replacement.IsValid()
            || shutdownRequested.load(std::memory_order_acquire)
            || lowLevelHooks == nullptr) {
            return false;
        }
        {
            const std::lock_guard targetLock(targetContextMutex);
            if (shutdownRequested.load(std::memory_order_acquire)) {
                return false;
            }
            if (targetContext->IsValid()) {
                return false;
            }
            *targetContext = std::move(replacement);
        }
        const bool excluded = processExclusion.IsForegroundExcluded();
        SetTargetEligible(!excluded && TargetIsForeground());
        lowLevelHooks->Wake();
        return true;
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

    bool SeedActivatedPhysicalControls() noexcept
    {
        for (std::size_t token = 0U;
             token < controlCatalog->BindingCount();
             ++token) {
            const win32::WindowsControlBinding* const binding =
                controlCatalog->Binding(token);
            if (binding == nullptr
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

    void PopulateDebugInput(
        const WindowsNativeInputEvent& event,
        const RuntimeInputEvent& normalized,
        debug::InputDisposition disposition,
        debug::InputEventPayload& input) const noexcept
    {
        input.device = event.device;
        input.transition = event.transition;
        input.origin = event.origin;
        input.disposition = disposition;
        input.virtualKey = event.virtualKey;
        input.scanCode = event.scanCode;
        if (event.device == DeviceKind::Keyboard) {
            input.nativeQualifier = (event.hookFlags & LLKHF_EXTENDED) != 0U
                ? kWindowsScanCodeQualifierE0
                : event.virtualKey == VK_PAUSE && event.scanCode == 0x45U
                    ? kWindowsScanCodeQualifierE1
                    : kControlQualifierNone;
        }
        input.mouseData = event.mouseData;
        if (normalized.control.IsValid()
            && activeProgram != nullptr
            && normalized.control.value < activeProgram->Controls().size()) {
            input.hasCompiledControl = true;
            input.compiledIdentity =
                activeProgram->Controls()[normalized.control.value];
        }
    }

    [[nodiscard]] std::size_t SampleDebugInitialInputs(
        std::array<debug::InputEventPayload, 256U>& inputs) const noexcept
    {
        std::size_t count = 0U;
        for (WindowsVirtualKey virtualKey = 1U;
             virtualKey <= 0xffU;
             ++virtualKey) {
            if (IsAliasedModifierVirtualKey(virtualKey)
                || (GetAsyncKeyState(static_cast<int>(virtualKey)) & 0x8000)
                    == 0) {
                continue;
            }
            WindowsNativeInputEvent event{};
            event.device = IsMouseButtonVirtualKey(virtualKey)
                ? DeviceKind::Mouse
                : DeviceKind::Keyboard;
            event.origin = InputOrigin::InitialSample;
            event.transition = Transition::Down;
            event.virtualKey = virtualKey;
            if (event.device == DeviceKind::Keyboard) {
                const UINT mapped = MapVirtualKeyW(
                    virtualKey,
                    MAPVK_VK_TO_VSC_EX);
                event.scanCode = mapped & 0xffU;
                if ((mapped & 0xff00U) == 0xe000U) {
                    event.hookFlags = LLKHF_EXTENDED;
                }
            } else if (virtualKey == VK_XBUTTON1) {
                event.mouseData = XBUTTON1;
            } else if (virtualKey == VK_XBUTTON2) {
                event.mouseData = XBUTTON2;
            }
            const RuntimeInputEvent normalized = inputAdapter->Normalize(event);
            debug::InputEventPayload& input = inputs[count++];
            PopulateDebugInput(
                event,
                normalized,
                debug::InputDisposition::NotApplicable,
                input);
        }
        return count;
    }

    bool CreateEvents(std::wstring& errorMessage) noexcept
    {
        shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        targetLostEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        hookProducerDoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        controlRequestEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        outputWakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        outputReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        outputProducerDoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        outputStoppedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (shutdownEvent != nullptr
            && targetLostEvent != nullptr
            && hookProducerDoneEvent != nullptr
            && controlRequestEvent != nullptr
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
            &targetLostEvent,
            &hookProducerDoneEvent,
            &controlRequestEvent,
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

    bool StartDebugServer(
        const std::shared_ptr<const CompiledProgram>& program,
        std::wstring& errorMessage)
    {
        if (options.debugSessionToken.empty()) {
            return true;
        }
        try {
            debugServer = std::make_unique<win32::WindowsDebugServer>();
        } catch (...) {
            errorMessage = L"Cannot allocate the input debug server.";
            return false;
        }
        if (!debugServer->Start(
                options.debugSessionToken,
                program,
                {
                    {this, &Impl::WakeDebugInputThreadThunk},
                    {this, &Impl::SignalStopThunk}},
                errorMessage)) {
            debugServer.reset();
            return false;
        }
        return true;
    }

    bool CreateComponents(std::wstring& errorMessage)
    {
        try {
            controlCatalog = std::make_unique<win32::WindowsControlCatalog>();
            routePort = std::make_unique<win32::WindowsRuntimeRoutePort>(
                targetContext,
                &processExclusion,
                &targetContextMutex);
            processLauncher = std::make_unique<win32::WindowsProcessLauncher>(
                options.permitProcessLaunch,
                options.dryRun);
            clock = std::make_unique<SteadyRuntimeClock>();
            outputPort = std::make_unique<win32::WindowsRuntimeOutputPort>(
                *controlCatalog,
                this,
                &Impl::PublishThunk);
            RuntimeCapacities capacities{};
            capacities.maximumArrayBytes = 64U * 1024U * 1024U;
            capacities.permitProcessLaunch = options.permitProcessLaunch;
            runtime = std::make_unique<ProgramRuntime>(
                capacities,
                *controlCatalog,
                *outputPort,
                *routePort,
                *processLauncher,
                *clock,
                debugServer.get(),
                support::CallbackRef<void() noexcept>{this, &Impl::FatalStopThunk});
            inputAdapter = std::make_unique<win32::WindowsRuntimeInputAdapter>(
                *controlCatalog);
            const StopRequest stopRequest{this, &Impl::RequestStopThunk};
            lowLevelHooks = std::make_unique<LowLevelHooks>(
                options.selfTag,
                *this,
                targetContext,
                &targetContextMutex,
                &processExclusion,
                stopRequest,
                shutdownRequested,
                shutdownEvent,
                controlRequestEvent,
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
            TargetProcessId(),
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
            if (processExclusion.IsForegroundExcluded()) {
                SetTargetEligible(false);
            } else if (targetContext != nullptr
                && !TargetIsAlive()) {
                lowLevelHooks->Wake();
            } else if (targetContext != nullptr
                && !TargetIsForeground()) {
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
        if (processExclusion.IsForegroundExcluded()) {
            return false;
        }
        if (targetContext == nullptr) {
            return true;
        }
        const std::lock_guard targetLock(targetContextMutex);
        return targetContext->IsTargetAlive()
            && targetContext->IsTargetForeground()
            && (!item.requiresPointerTarget
                || targetContext->IsTargetPointerTargetAtCursor());
    }

    [[nodiscard]] WindowsProcessId TargetProcessId() const noexcept
    {
        if (targetContext == nullptr) {
            return 0U;
        }
        const std::lock_guard targetLock(targetContextMutex);
        return targetContext->TargetPid();
    }

    [[nodiscard]] bool TargetIsAlive() const noexcept
    {
        const std::lock_guard targetLock(targetContextMutex);
        return targetContext != nullptr && targetContext->IsTargetAlive();
    }

    [[nodiscard]] bool TargetIsForeground() const noexcept
    {
        const std::lock_guard targetLock(targetContextMutex);
        return targetContext != nullptr && targetContext->IsTargetForeground();
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
            if (debugServer != nullptr) {
                RuntimeDebugEvent issue{};
                issue.kind = RuntimeDebugEventKind::RuntimeIssue;
                issue.issue.kind = RuntimeDiagnosticKind::OutputFailure;
                issue.issue.subject = item.outputCode;
                issue.issue.platformError = result.error;
                (void)debugServer->Publish(issue);
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
    TargetProcessContext ownedTargetContext;
    TargetProcessContext* targetContext;
    DiagnosticLog& diagnosticLog;
    ForegroundProcessExclusion processExclusion;
    HANDLE shutdownEvent{};
    HANDLE targetLostEvent{};
    HANDLE hookProducerDoneEvent{};
    HANDLE controlRequestEvent{};
    HANDLE outputWakeEvent{};
    HANDLE outputReadyEvent{};
    HANDLE outputProducerDoneEvent{};
    HANDLE outputStoppedEvent{};
    std::atomic<bool> started{false};
    std::atomic<bool> shutdownRequested{false};
    bool runtimeShutdownNotified{};
    std::mutex runtimeCallMutex;
    mutable std::mutex targetContextMutex;
    mutable std::mutex metricsMutex;
    std::once_flag waitOnce;
    bool waitCompleted{};
    std::atomic<bool> fatalShutdownObserved{false};
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
    std::unique_ptr<win32::WindowsDebugServer> debugServer;
    std::unique_ptr<win32::WindowsRuntimeInputAdapter> inputAdapter;
    std::unique_ptr<LowLevelHooks> lowLevelHooks;
    std::shared_ptr<const CompiledProgram> activeProgram;
};

WindowsProgramRuntimeSession::WindowsProgramRuntimeSession(
    WindowsProgramRuntimeSessionOptions options,
    DiagnosticLog& diagnosticLog)
    : impl_(std::make_unique<Impl>(
          std::move(options),
          diagnosticLog))
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

bool WindowsProgramRuntimeSession::AttachTarget(
    TargetProcessContext&& targetContext) noexcept
{
    return impl_->AttachTarget(std::move(targetContext));
}

HANDLE WindowsProgramRuntimeSession::StoppedEvent() const noexcept
{
    return impl_->StoppedEvent();
}

HANDLE WindowsProgramRuntimeSession::TargetLostEvent() const noexcept
{
    return impl_->TargetLostEvent();
}

WindowsProgramRuntimeSessionMetrics WindowsProgramRuntimeSession::Metrics() const noexcept
{
    return impl_->Metrics();
}

} // namespace inputweaver
