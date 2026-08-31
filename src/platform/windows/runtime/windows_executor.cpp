#include "windows_executor.hpp"

#include "compiled_target_resolver.hpp"
#include "platform/windows/debug/debug_server.hpp"
#include "platform/windows/diagnostics/diagnostic_log.hpp"
#include "platform/windows/runtime/process_context.hpp"
#include "platform/windows/runtime/process_locator.hpp"
#include "program_runtime_session.hpp"
#include "runtime/artifact_loader.hpp"
#include "support/bit_mix.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <windows.h>

namespace {

using inputweaver::win32::WindowsExecutorOptions;

HANDLE gConsoleStopEvent = nullptr;

class RuntimeDebugBinding final {
public:
    void Attach(inputweaver::WindowsProgramRuntimeSession& runtime)
    {
        const std::lock_guard lock(mutex_);
        runtime_ = &runtime;
    }

    void Detach(inputweaver::WindowsProgramRuntimeSession& runtime)
    {
        const std::lock_guard lock(mutex_);
        if (runtime_ == &runtime) {
            runtime_ = nullptr;
        }
    }

    static void WakeRuntime(void* context) noexcept
    {
        if (context != nullptr) {
            static_cast<RuntimeDebugBinding*>(context)->WakeAttachedRuntime();
        }
    }

private:
    void WakeAttachedRuntime() noexcept
    {
        const std::lock_guard lock(mutex_);
        if (runtime_ != nullptr) {
            runtime_->WakeDebugInputThread();
        }
    }

    std::mutex mutex_;
    inputweaver::WindowsProgramRuntimeSession* runtime_{};
};

BOOL WINAPI ConsoleControlHandler(DWORD controlType) noexcept {
    switch (controlType) {
        case CTRL_C_EVENT:
            return TRUE;
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (gConsoleStopEvent != nullptr) {
                SetEvent(gConsoleStopEvent);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

inputweaver::WindowsSelfTag GenerateSelfTag() noexcept {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    std::uint64_t seed = static_cast<std::uint64_t>(counter.QuadPart);
    seed ^= static_cast<std::uint64_t>(GetTickCount64());
    seed ^= static_cast<std::uint64_t>(GetCurrentProcessId()) << 32U;
    seed ^= static_cast<std::uint64_t>(GetCurrentThreadId()) << 16U;
    const std::uint64_t mixed = inputweaver::support::Mix64(
        seed + 0x9E3779B97F4A7C15ULL);
    const auto tag = static_cast<inputweaver::WindowsSelfTag>(mixed);
    return tag == 0U
        ? static_cast<inputweaver::WindowsSelfTag>(0x49575631U)
        : tag;
}

void PrintProgramMetrics(
    const inputweaver::WindowsProgramRuntimeSessionMetrics& metrics,
    const inputweaver::DiagnosticLog& diagnosticLog) {
    std::wcout << L"Compiled-program session stopped. dispatched="
               << metrics.runtime.dispatchedEvents
               << L" suppressed=" << metrics.runtime.suppressedEvents
               << L" tasks_started=" << metrics.runtime.startedTasks
               << L" tasks_completed=" << metrics.runtime.completedTasks
               << L" tasks_cancelled=" << metrics.runtime.cancelledTasks
               << L" transaction_rejections=" << metrics.runtime.transactionRejections
               << L" runtime_diagnostic_drops=" << metrics.runtime.droppedDiagnostics
               << L" output_transitions=" << metrics.runtime.outputTransitions
               << L" scheduler_backoffs=" << metrics.runtime.schedulerBackoffs
               << L" current_array_bytes=" << metrics.runtime.currentArrayBytes
               << L" peak_array_bytes=" << metrics.runtime.peakArrayBytes
               << L" rejected_array_growth=" << metrics.runtime.rejectedArrayGrowth
               << L" queued_outputs=" << metrics.queuedOutputs
               << L" cancelled_outputs=" << metrics.cancelledOutputs
               << L" injection_failures=" << metrics.injectionFailures
               << L" outside_target_forwarded=" << metrics.forwardedOutsideTarget
               << L" max_hook_us=" << metrics.maximumHookMicroseconds
               << L" circuit_breaker="
               << (metrics.circuitBreakerOpen ? L"true" : L"false")
               << L" fatal_shutdown="
               << (metrics.fatalShutdown ? L"true" : L"false")
               << L" hook_log_drops=" << diagnosticLog.DroppedHookRecords()
               << L" injection_log_drops=" << diagnosticLog.DroppedInjectionRecords()
               << L" runtime_log_drops=" << diagnosticLog.DroppedRuntimeRecords()
               << L" jsonl_bytes=" << diagnosticLog.JsonlBytesWritten()
               << L" jsonl_truncated=" << (diagnosticLog.JsonlTruncated() ? L"true" : L"false")
               << L"\n" << std::flush;
}

void PrintFinalDiagnosticMetrics(const inputweaver::DiagnosticLog& diagnosticLog) {
    std::wcout << L"Diagnostic log stopped. hook_log_drops="
               << diagnosticLog.DroppedHookRecords()
               << L" injection_log_drops="
               << diagnosticLog.DroppedInjectionRecords()
               << L" runtime_log_drops="
               << diagnosticLog.DroppedRuntimeRecords()
               << L" jsonl_bytes=" << diagnosticLog.JsonlBytesWritten()
               << L" jsonl_truncated="
               << (diagnosticLog.JsonlTruncated() ? L"true" : L"false")
               << L"\n" << std::flush;
}

bool StopWasRequested() noexcept {
    return gConsoleStopEvent != nullptr &&
           WaitForSingleObject(gConsoleStopEvent, 0) == WAIT_OBJECT_0;
}

void RequestExecutorStopFromDebug(void*) noexcept
{
    if (gConsoleStopEvent != nullptr) {
        SetEvent(gConsoleStopEvent);
    }
}

bool WaitForRetry(HANDLE runtimeStoppedEvent = nullptr) noexcept {
    HANDLE waitHandles[] = {gConsoleStopEvent, runtimeStoppedEvent};
    const DWORD count = runtimeStoppedEvent == nullptr ? 1U : 2U;
    const DWORD result = WaitForMultipleObjects(
        count,
        waitHandles,
        FALSE,
        1000U);
    return result != WAIT_TIMEOUT;
}

DWORD WaitForProgramRuntime(
    inputweaver::WindowsProgramRuntimeSession& runtime,
    DWORD& waitError) {
    const HANDLE waitHandles[] = {
        runtime.StoppedEvent(),
        runtime.TargetLostEvent(),
        gConsoleStopEvent};
    const DWORD result = WaitForMultipleObjects(3U, waitHandles, FALSE, INFINITE);
    waitError = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    return result;
}

enum class TargetWaitResult : unsigned char {
    Found,
    Stopped,
    Error
};

TargetWaitResult WaitForTarget(
    const std::wstring& targetSelector,
    HANDLE runtimeStoppedEvent,
    inputweaver::TargetProcessContext& targetContext,
    inputweaver::win32::LocatedProcess& selected) {
    bool waitingMessagePrinted = false;
    bool ambiguousMessagePrinted = false;
    while (!StopWasRequested()
        && (runtimeStoppedEvent == nullptr
            || WaitForSingleObject(runtimeStoppedEvent, 0U) != WAIT_OBJECT_0)) {
        const inputweaver::win32::LocateResult located =
            inputweaver::win32::LocateExecutable(targetSelector);
        if (located.status == inputweaver::win32::LocateStatus::Error) {
            std::wcerr << L"Error: target search failed with Win32 error "
                       << located.win32Error << L".\n";
            return TargetWaitResult::Error;
        }
        if (located.status == inputweaver::win32::LocateStatus::None) {
            if (!waitingMessagePrinted) {
                std::wcout << L"Waiting for target " << targetSelector << L"...\n"
                           << std::flush;
                waitingMessagePrinted = true;
            }
            if (WaitForRetry(runtimeStoppedEvent)) {
                break;
            }
            continue;
        }

        if (!inputweaver::win32::SelectLocatedProcess(located, selected)) {
            if (!ambiguousMessagePrinted) {
                std::wcout << L"Multiple target processes match; focus the intended instance:\n";
                for (const auto& candidate : located.matches) {
                    std::wcout << L"  PID " << candidate.processId << L"  "
                               << candidate.imagePath << L"\n";
                }
                std::wcout << std::flush;
                ambiguousMessagePrinted = true;
            }
            if (WaitForRetry(runtimeStoppedEvent)) {
                break;
            }
            continue;
        }

        const inputweaver::ProcessContextResult targetResult =
            targetContext.Initialize(selected.processId, selected.imagePath);
        if (targetResult.error == inputweaver::ProcessContextError::TargetExited
            || targetResult.error
                == inputweaver::ProcessContextError::TargetImageMismatch) {
            continue;
        }
        if (!targetResult.Succeeded()) {
            std::cerr << "Target validation failed: "
                      << inputweaver::ProcessContextErrorName(targetResult.error)
                      << " (Win32 error " << targetResult.win32Error << ").\n";
            return TargetWaitResult::Error;
        }
        return TargetWaitResult::Found;
    }
    return TargetWaitResult::Stopped;
}

void PrintArtifactReadError(
    const inputweaver::RuntimeArtifactReadResult& result) {
    std::cerr << "Cannot read the compiled program. code="
              << static_cast<unsigned int>(result.error) << ".\n";
    if (result.decodeError.has_value()) {
        std::cerr << "Decode error at byte " << result.decodeError->byteOffset
                  << ": " << result.decodeError->message << "\n";
    }
    for (const inputweaver::ProgramValidationError& error : result.validationErrors) {
        std::cerr << error.location << ": " << error.message << "\n";
    }
}

int RunCompiledInstance(
    const WindowsExecutorOptions& options,
    inputweaver::WindowsSelfTag selfTag,
    inputweaver::DiagnosticLog& diagnosticLog,
    const std::shared_ptr<const inputweaver::CompiledProgram>& program,
    inputweaver::TargetSelectorKind effectiveTargetKind,
    const std::wstring& targetSelector,
    inputweaver::TargetProcessContext* targetContext,
    inputweaver::win32::WindowsDebugServer* debugServer,
    RuntimeDebugBinding& debugBinding) {
    inputweaver::WindowsProgramRuntimeSession runtime(
        {
            options.traceInput,
            options.dryRun,
            options.allowExec,
            selfTag,
            effectiveTargetKind,
            options.excludedProcessSelector,
            debugServer},
        targetContext,
        diagnosticLog);
    debugBinding.Attach(runtime);
    std::wstring errorMessage;
    if (!runtime.Start(program, errorMessage)) {
        debugBinding.Detach(runtime);
        std::wcerr << L"Error: " << errorMessage << L"\n";
        return 6;
    }

    std::wcout << L"Compiled program is active"
               << (targetContext == nullptr
                    ? L" globally.\n"
                    : L" only while the selected process is foreground.\n")
               << (options.dryRun
                    ? L"Dry-run is active; physical input is forwarded and output effects are simulated.\n"
                    : L"")
               << L"Use the configured physical exit event to stop.\n" << std::flush;
    int sessionResult = 0;
    for (;;) {
        DWORD waitError = ERROR_SUCCESS;
        const DWORD waitResult = WaitForProgramRuntime(runtime, waitError);
        if (waitResult == WAIT_OBJECT_0 + 1U) {
            std::wcout << L"Target exited; waiting for it to restart.\n"
                       << std::flush;
            inputweaver::TargetProcessContext replacement;
            inputweaver::win32::LocatedProcess selected{};
            const TargetWaitResult targetResult = WaitForTarget(
                targetSelector,
                runtime.StoppedEvent(),
                replacement,
                selected);
            if (targetResult == TargetWaitResult::Error) {
                sessionResult = 3;
                runtime.RequestStop();
                break;
            }
            if (targetResult == TargetWaitResult::Stopped) {
                if (StopWasRequested()) {
                    runtime.RequestStop();
                }
                break;
            }
            if (!runtime.AttachTarget(std::move(replacement))) {
                if (WaitForSingleObject(runtime.StoppedEvent(), 0U)
                    != WAIT_OBJECT_0) {
                    std::wcerr << L"Error: cannot attach the replacement target.\n";
                    sessionResult = 7;
                    runtime.RequestStop();
                }
                break;
            }
            std::wcout << L"Attached to PID " << selected.processId << L": "
                       << selected.imagePath << L"\n" << std::flush;
            continue;
        }
        if (waitResult == WAIT_OBJECT_0 + 2U) {
            runtime.RequestStop();
        } else if (waitResult == WAIT_FAILED) {
            std::wcerr << L"WaitForMultipleObjects failed with Win32 error "
                       << waitError << L".\n";
            sessionResult = 7;
            runtime.RequestStop();
        }
        break;
    }
    runtime.Wait();
    debugBinding.Detach(runtime);
    const inputweaver::WindowsProgramRuntimeSessionMetrics metrics = runtime.Metrics();
    PrintProgramMetrics(metrics, diagnosticLog);
    if (sessionResult != 0) {
        return sessionResult;
    }
    return metrics.fatalShutdown
            || metrics.circuitBreakerOpen
            || metrics.injectionFailures != 0U
        ? 8
        : 0;
}

int RunCompiledProgram(
    const WindowsExecutorOptions& options,
    inputweaver::WindowsSelfTag selfTag,
    inputweaver::DiagnosticLog& diagnosticLog,
    const std::shared_ptr<const inputweaver::CompiledProgram>& program,
    inputweaver::TargetSelectorKind targetKind,
    const std::wstring& targetSelector,
    inputweaver::win32::WindowsDebugServer* debugServer,
    RuntimeDebugBinding& debugBinding) {
    if (targetKind == inputweaver::TargetSelectorKind::Global) {
        return RunCompiledInstance(
            options,
            selfTag,
            diagnosticLog,
            program,
            targetKind,
            targetSelector,
            nullptr,
            debugServer,
            debugBinding);
    }

    inputweaver::TargetProcessContext targetContext;
    inputweaver::win32::LocatedProcess selected{};
    const TargetWaitResult targetResult = WaitForTarget(
        targetSelector,
        nullptr,
        targetContext,
        selected);
    if (targetResult == TargetWaitResult::Error) {
        return 3;
    }
    if (targetResult == TargetWaitResult::Stopped) {
        return 0;
    }

    std::wcout << L"Loaded " << options.programPath.wstring() << L"\n"
               << L"Attached to PID " << selected.processId << L": "
               << selected.imagePath << L"\n" << std::flush;
    return RunCompiledInstance(
        options,
        selfTag,
        diagnosticLog,
        program,
        targetKind,
        targetSelector,
        &targetContext,
        debugServer,
        debugBinding);
}

}  // namespace

int inputweaver::win32::RunWindowsExecutor(const WindowsExecutorOptions& options) {
    std::wstring errorMessage;
    inputweaver::RuntimeArtifactReadResult artifact =
        inputweaver::ReadWeavec(options.programPath);
    if (!artifact.Succeeded()) {
        PrintArtifactReadError(artifact);
        return 10;
    }
    std::shared_ptr<const inputweaver::CompiledProgram> compiledProgram =
        std::move(artifact.program);
    inputweaver::TargetSelectorKind compiledTargetKind =
        inputweaver::TargetSelectorKind::Unspecified;
    std::wstring compiledTargetSelector;
    if (!inputweaver::win32::ResolveCompiledTarget(
            *compiledProgram,
            options.targetGlobal,
            options.targetSelector,
            compiledTargetKind,
            compiledTargetSelector,
            errorMessage)) {
        std::wcerr << L"Error: " << errorMessage << L"\n";
        return 10;
    }
    inputweaver::DiagnosticLog diagnosticLog;
    if (!diagnosticLog.Start(options.jsonlPath, errorMessage)) {
        std::wcerr << L"Error: " << errorMessage << L"\n";
        return 4;
    }

    const bool ownsConsoleStopEvent = options.inheritedStopEvent == 0U;
    gConsoleStopEvent = ownsConsoleStopEvent
        ? CreateEventW(nullptr, TRUE, FALSE, nullptr)
        : reinterpret_cast<HANDLE>(options.inheritedStopEvent);
    DWORD stopEventFlags{};
    if (gConsoleStopEvent == nullptr
        || GetHandleInformation(gConsoleStopEvent, &stopEventFlags) == FALSE
        || WaitForSingleObject(gConsoleStopEvent, 0U) == WAIT_FAILED) {
        std::wcerr << L"Error: invalid executor stop event. Win32 error "
                   << GetLastError() << L".\n";
        if (ownsConsoleStopEvent && gConsoleStopEvent != nullptr) {
            CloseHandle(gConsoleStopEvent);
        }
        gConsoleStopEvent = nullptr;
        diagnosticLog.Stop();
        return 5;
    }
    if (ownsConsoleStopEvent
        && !SetConsoleCtrlHandler(&ConsoleControlHandler, TRUE)) {
        const DWORD consoleError = GetLastError();
        std::wcerr << L"Error: cannot install the console control handler. Win32 error "
                   << consoleError << L".\n";
        CloseHandle(gConsoleStopEvent);
        gConsoleStopEvent = nullptr;
        diagnosticLog.Stop();
        return 5;
    }

    const int result = [&]() {
        RuntimeDebugBinding debugBinding;
        std::unique_ptr<inputweaver::win32::WindowsDebugServer> debugServer;
        if (!options.debugSessionToken.empty()) {
            try {
                debugServer =
                    std::make_unique<inputweaver::win32::WindowsDebugServer>();
            } catch (...) {
                std::wcerr << L"Error: cannot allocate the input debug server.\n";
                return 6;
            }
            if (!debugServer->Start(
                    options.debugSessionToken,
                    compiledProgram,
                    {
                        {&debugBinding, &RuntimeDebugBinding::WakeRuntime},
                        {nullptr, &RequestExecutorStopFromDebug}},
                    errorMessage)) {
                std::wcerr << L"Error: " << errorMessage << L"\n";
                return 6;
            }
        }
        return RunCompiledProgram(
            options,
            GenerateSelfTag(),
            diagnosticLog,
            compiledProgram,
            compiledTargetKind,
            compiledTargetSelector,
            debugServer.get(),
            debugBinding);
    }();

    diagnosticLog.Stop();
    PrintFinalDiagnosticMetrics(diagnosticLog);
    if (ownsConsoleStopEvent) {
        SetConsoleCtrlHandler(&ConsoleControlHandler, FALSE);
    }
    CloseHandle(gConsoleStopEvent);
    gConsoleStopEvent = nullptr;
    return result;
}
