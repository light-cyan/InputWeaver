#include "windows_executor.hpp"

#include "compiled_target_resolver.hpp"
#include "platform/windows/diagnostics/diagnostic_log.hpp"
#include "platform/windows/runtime/process_context.hpp"
#include "platform/windows/runtime/process_locator.hpp"
#include "platform/windows/support/file_identity.hpp"
#include "program_runtime_session.hpp"
#include "runtime/artifact_loader.hpp"
#include "support/bit_mix.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <windows.h>

namespace {

using inputweaver::win32::WindowsExecutorOptions;

HANDLE gConsoleStopEvent = nullptr;

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

std::string RequestedTargetMode(const WindowsExecutorOptions& options) {
    if (options.targetGlobal) return "Global";
    return options.targetSelector.empty() ? "Compiled" : "ExecutableOverride";
}

std::string EffectiveTargetMode(inputweaver::TargetSelectorKind kind) {
    switch (kind) {
    case inputweaver::TargetSelectorKind::Global: return "Global";
    case inputweaver::TargetSelectorKind::Executable: return "Executable";
    case inputweaver::TargetSelectorKind::Unspecified: return "Unspecified";
    }
    return "Unspecified";
}

void PublishExecutorDiagnostic(
    inputweaver::DiagnosticLog& log,
    const inputweaver::ExecutorDiagnosticRecord& record) noexcept {
    (void)log.WriteExecutor(record);
}

void PublishTargetDiagnostic(
    inputweaver::DiagnosticLog& log,
    inputweaver::ExecutorDiagnosticKind kind,
    std::wstring_view selector = {},
    std::wstring_view imagePath = {},
    inputweaver::WindowsProcessId pid = 0U,
    std::uint32_t code = 0U,
    std::uint32_t win32Error = 0U,
    std::uint32_t matchCount = 0U,
    std::wstring_view detail = {}) noexcept {
    inputweaver::ExecutorDiagnosticRecord record{};
    record.kind = kind;
    record.targetSelector = selector;
    record.imagePath = imagePath;
    record.pid = pid;
    record.code = code;
    record.win32Error = win32Error;
    record.matchCount = matchCount;
    record.detail = detail;
    PublishExecutorDiagnostic(log, record);
}

void PublishStartupFailure(
    inputweaver::DiagnosticLog& log,
    std::string stage,
    std::wstring detail,
    std::uint32_t code = 0U,
    std::uint32_t win32Error = 0U) noexcept {
    inputweaver::ExecutorDiagnosticRecord record{};
    record.kind = inputweaver::ExecutorDiagnosticKind::StartupFailure;
    record.stage = std::move(stage);
    record.detail = std::move(detail);
    record.code = code;
    record.win32Error = win32Error;
    PublishExecutorDiagnostic(log, record);
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

int FinishDiagnosticSession(
    inputweaver::DiagnosticLog& diagnosticLog,
    int exitCode,
    std::string reason) {
    inputweaver::ExecutorDiagnosticRecord record{};
    record.kind = inputweaver::ExecutorDiagnosticKind::SessionStop;
    record.reason = std::move(reason);
    record.exitCode = static_cast<std::uint32_t>(exitCode);
    diagnosticLog.Stop(record);
    PrintFinalDiagnosticMetrics(diagnosticLog);
    return exitCode;
}

bool StopWasRequested() noexcept {
    return gConsoleStopEvent != nullptr &&
           WaitForSingleObject(gConsoleStopEvent, 0) == WAIT_OBJECT_0;
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
    inputweaver::win32::LocatedProcess& selected,
    inputweaver::DiagnosticLog& diagnosticLog) {
    PublishTargetDiagnostic(
        diagnosticLog,
        inputweaver::ExecutorDiagnosticKind::TargetSearchStarted,
        targetSelector);
    bool waitingMessagePrinted = false;
    bool ambiguousMessagePrinted = false;
    while (!StopWasRequested()
        && (runtimeStoppedEvent == nullptr
            || WaitForSingleObject(runtimeStoppedEvent, 0U) != WAIT_OBJECT_0)) {
        const inputweaver::win32::LocateResult located =
            inputweaver::win32::LocateExecutable(targetSelector);
        if (located.status == inputweaver::win32::LocateStatus::Error) {
            PublishTargetDiagnostic(
                diagnosticLog,
                inputweaver::ExecutorDiagnosticKind::TargetSearchFailure,
                targetSelector,
                {},
                0U,
                0U,
                located.win32Error);
            std::wcerr << L"Error: target search failed with Win32 error "
                       << located.win32Error << L".\n";
            return TargetWaitResult::Error;
        }
        if (located.status == inputweaver::win32::LocateStatus::None) {
            if (!waitingMessagePrinted) {
                PublishTargetDiagnostic(
                    diagnosticLog,
                    inputweaver::ExecutorDiagnosticKind::TargetSearchWaiting,
                    targetSelector);
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
                PublishTargetDiagnostic(
                    diagnosticLog,
                    inputweaver::ExecutorDiagnosticKind::TargetSearchAmbiguous,
                    targetSelector,
                    {},
                    0U,
                    0U,
                    0U,
                    static_cast<std::uint32_t>(located.matches.size()));
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
            PublishTargetDiagnostic(
                diagnosticLog,
                inputweaver::ExecutorDiagnosticKind::TargetSearchFailure,
                targetSelector,
                selected.imagePath,
                selected.processId,
                static_cast<std::uint32_t>(targetResult.error),
                targetResult.win32Error);
            std::cerr << "Target validation failed: "
                      << inputweaver::ProcessContextErrorName(targetResult.error)
                      << " (Win32 error " << targetResult.win32Error << ").\n";
            return TargetWaitResult::Error;
        }
        PublishTargetDiagnostic(
            diagnosticLog,
            inputweaver::ExecutorDiagnosticKind::TargetFound,
            targetSelector,
            selected.imagePath,
            selected.processId);
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

int RunCompiledProgram(
    const WindowsExecutorOptions& options,
    inputweaver::WindowsSelfTag selfTag,
    inputweaver::DiagnosticLog& diagnosticLog,
    const std::shared_ptr<const inputweaver::CompiledProgram>& program,
    inputweaver::TargetSelectorKind targetKind,
    const std::wstring& targetSelector,
    std::string& stopReason) {
    inputweaver::WindowsProgramRuntimeSession runtime(
        {
            options.traceInput,
            options.dryRun,
            options.allowExec,
            selfTag,
            targetKind,
            options.excludedProcessSelector,
            options.debugSessionToken},
        diagnosticLog);
    std::wstring errorMessage;
    if (!runtime.Start(program, errorMessage)) {
        PublishStartupFailure(diagnosticLog, "runtime_start", errorMessage);
        stopReason = "startup_failure";
        std::wcerr << L"Error: " << errorMessage << L"\n";
        return 6;
    }

    const bool executableTarget =
        targetKind == inputweaver::TargetSelectorKind::Executable;
    std::wcout << L"Loaded " << options.programPath.wstring() << L"\n"
               << L"Compiled program is active"
               << (executableTarget
                    ? L" only while the selected process is foreground.\n"
                    : L" globally.\n")
               << (options.dryRun
                    ? L"Dry-run is active; physical input is forwarded and output effects are simulated.\n"
                    : L"")
               << L"Use the configured physical exit event to stop.\n" << std::flush;

    bool targetBound = !executableTarget;
    bool targetWasLost = false;
    inputweaver::WindowsProcessId attachedPid{};
    std::wstring attachedPath;
    int sessionResult = 0;
    for (;;) {
        if (!targetBound) {
            if (targetWasLost) {
                PublishTargetDiagnostic(
                    diagnosticLog,
                    inputweaver::ExecutorDiagnosticKind::TargetLost,
                    {},
                    attachedPath,
                    attachedPid);
                std::wcout << L"Target exited; waiting for it to restart.\n"
                           << std::flush;
                targetWasLost = false;
            }
            inputweaver::TargetProcessContext candidate;
            inputweaver::win32::LocatedProcess selected{};
            const TargetWaitResult targetResult = WaitForTarget(
                targetSelector,
                runtime.StoppedEvent(),
                candidate,
                selected,
                diagnosticLog);
            if (targetResult == TargetWaitResult::Error) {
                sessionResult = 3;
                stopReason = "target_search_failure";
                runtime.RequestStop();
                break;
            }
            if (targetResult == TargetWaitResult::Stopped) {
                if (StopWasRequested()) {
                    runtime.RequestStop();
                }
                break;
            }
            if (!runtime.AttachTarget(std::move(candidate))) {
                if (WaitForSingleObject(runtime.StoppedEvent(), 0U)
                    != WAIT_OBJECT_0) {
                    PublishTargetDiagnostic(
                        diagnosticLog,
                        inputweaver::ExecutorDiagnosticKind::TargetAttachFailure,
                        targetSelector,
                        selected.imagePath,
                        selected.processId,
                        0U,
                        0U,
                        0U,
                        L"The runtime rejected the selected target.");
                    std::wcerr << L"Error: cannot attach the target.\n";
                    sessionResult = 7;
                    stopReason = "target_attach_failure";
                    runtime.RequestStop();
                }
                break;
            }
            targetBound = true;
            attachedPid = selected.processId;
            attachedPath = selected.imagePath;
            PublishTargetDiagnostic(
                diagnosticLog,
                inputweaver::ExecutorDiagnosticKind::TargetAttached,
                targetSelector,
                attachedPath,
                attachedPid);
            std::wcout << L"Attached to PID " << selected.processId << L": "
                       << selected.imagePath << L"\n" << std::flush;
        }

        DWORD waitError = ERROR_SUCCESS;
        const DWORD waitResult = WaitForProgramRuntime(runtime, waitError);
        if (waitResult == WAIT_OBJECT_0 + 1U) {
            targetBound = false;
            targetWasLost = true;
            continue;
        }
        if (waitResult == WAIT_OBJECT_0 + 2U) {
            runtime.RequestStop();
        } else if (waitResult == WAIT_FAILED) {
            std::wcerr << L"WaitForMultipleObjects failed with Win32 error "
                       << waitError << L".\n";
            sessionResult = 7;
            stopReason = "wait_failure";
            runtime.RequestStop();
        }
        break;
    }
    runtime.Wait();
    const inputweaver::WindowsProgramRuntimeSessionMetrics metrics = runtime.Metrics();
    PrintProgramMetrics(metrics, diagnosticLog);
    if (sessionResult != 0) {
        return sessionResult;
    }
    if (metrics.fatalShutdown) stopReason = "runtime_failure";
    else if (metrics.circuitBreakerOpen) stopReason = "injection_circuit_breaker";
    else if (metrics.exitRequested) stopReason = "exit_rule";
    else stopReason = "stop_requested";
    return metrics.fatalShutdown
            || metrics.circuitBreakerOpen
            || metrics.injectionFailures != 0U
        ? 8
        : 0;
}

}  // namespace

int inputweaver::win32::RunWindowsExecutor(const WindowsExecutorOptions& options) {
    std::wstring errorMessage;
    if (!options.jsonlPath.empty()
        && inputweaver::win32::PathsReferToSameFile(
            options.programPath,
            std::filesystem::path{options.jsonlPath})) {
        std::wcerr << L"Error: the diagnostic log path must differ from the compiled program path.\n";
        return 4;
    }
    inputweaver::DiagnosticLog diagnosticLog;
    if (!diagnosticLog.Start(options.jsonlPath, errorMessage)) {
        std::wcerr << L"Error: " << errorMessage << L"\n";
        return 4;
    }
    inputweaver::ExecutorDiagnosticRecord sessionStart{};
    sessionStart.kind = inputweaver::ExecutorDiagnosticKind::SessionStart;
    sessionStart.targetMode = RequestedTargetMode(options);
    sessionStart.programPath = options.programPath.wstring();
    sessionStart.targetSelector = options.targetSelector;
    sessionStart.excludedProcessSelector = options.excludedProcessSelector;
    sessionStart.traceInput = options.traceInput;
    sessionStart.dryRun = options.dryRun;
    sessionStart.allowExec = options.allowExec;
    sessionStart.debug = !options.debugSessionToken.empty();
    PublishExecutorDiagnostic(diagnosticLog, sessionStart);
    inputweaver::RuntimeArtifactReadResult artifact =
        inputweaver::ReadWeavec(options.programPath);
    if (!artifact.Succeeded()) {
        PublishStartupFailure(
            diagnosticLog,
            "artifact_load",
            L"Cannot read or validate the compiled program.",
            static_cast<std::uint32_t>(artifact.error));
        PrintArtifactReadError(artifact);
        return FinishDiagnosticSession(diagnosticLog, 10, "startup_failure");
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
        PublishStartupFailure(diagnosticLog, "target_resolution", errorMessage);
        std::wcerr << L"Error: " << errorMessage << L"\n";
        return FinishDiagnosticSession(diagnosticLog, 10, "startup_failure");
    }
    inputweaver::ExecutorDiagnosticRecord configuration{};
    configuration.kind = inputweaver::ExecutorDiagnosticKind::ConfigurationResolved;
    configuration.targetMode = EffectiveTargetMode(compiledTargetKind);
    configuration.targetSelector = compiledTargetSelector;
    PublishExecutorDiagnostic(diagnosticLog, configuration);

    const bool ownsConsoleStopEvent = options.inheritedStopEvent == 0U;
    gConsoleStopEvent = ownsConsoleStopEvent
        ? CreateEventW(nullptr, TRUE, FALSE, nullptr)
        : reinterpret_cast<HANDLE>(options.inheritedStopEvent);
    DWORD stopEventFlags{};
    if (gConsoleStopEvent == nullptr
        || GetHandleInformation(gConsoleStopEvent, &stopEventFlags) == FALSE
        || WaitForSingleObject(gConsoleStopEvent, 0U) == WAIT_FAILED) {
        const DWORD stopEventError = GetLastError();
        PublishStartupFailure(diagnosticLog, "stop_event", L"The executor stop event is invalid.", 0U, stopEventError);
        std::wcerr << L"Error: invalid executor stop event. Win32 error "
                   << stopEventError << L".\n";
        if (ownsConsoleStopEvent && gConsoleStopEvent != nullptr) {
            CloseHandle(gConsoleStopEvent);
        }
        gConsoleStopEvent = nullptr;
        return FinishDiagnosticSession(diagnosticLog, 5, "startup_failure");
    }
    if (ownsConsoleStopEvent
        && !SetConsoleCtrlHandler(&ConsoleControlHandler, TRUE)) {
        const DWORD consoleError = GetLastError();
        PublishStartupFailure(
            diagnosticLog, "console_handler", L"Cannot install the console control handler.", 0U, consoleError);
        std::wcerr << L"Error: cannot install the console control handler. Win32 error "
                   << consoleError << L".\n";
        CloseHandle(gConsoleStopEvent);
        gConsoleStopEvent = nullptr;
        return FinishDiagnosticSession(diagnosticLog, 5, "startup_failure");
    }

    std::string stopReason;
    const int result = RunCompiledProgram(
        options,
        GenerateSelfTag(),
        diagnosticLog,
        compiledProgram,
        compiledTargetKind,
        compiledTargetSelector,
        stopReason);

    if (ownsConsoleStopEvent) {
        SetConsoleCtrlHandler(&ConsoleControlHandler, FALSE);
    }
    CloseHandle(gConsoleStopEvent);
    gConsoleStopEvent = nullptr;
    return FinishDiagnosticSession(diagnosticLog, result, std::move(stopReason));
}
