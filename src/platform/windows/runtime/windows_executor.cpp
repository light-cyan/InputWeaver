#include "windows_executor.hpp"

#include "compiled_target_resolver.hpp"
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
#include <string>

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

bool WaitForRetry() noexcept {
    return gConsoleStopEvent != nullptr &&
           WaitForSingleObject(gConsoleStopEvent, 1000) == WAIT_OBJECT_0;
}

DWORD WaitForProgramRuntime(
    inputweaver::WindowsProgramRuntimeSession& runtime,
    DWORD& waitError) {
    const HANDLE waitHandles[] = {runtime.StoppedEvent(), gConsoleStopEvent};
    const DWORD result = WaitForMultipleObjects(2U, waitHandles, FALSE, INFINITE);
    waitError = result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    return result;
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
    inputweaver::TargetProcessContext* targetContext) {
    inputweaver::WindowsProgramRuntimeSession runtime(
        {
            options.traceInput,
            options.dryRun,
            options.allowExec,
            selfTag,
            effectiveTargetKind,
            options.excludedProcessSelector,
            options.debugSessionToken,
            {nullptr, &RequestExecutorStopFromDebug}},
        targetContext,
        diagnosticLog);
    std::wstring errorMessage;
    if (!runtime.Start(program, errorMessage)) {
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
    DWORD waitError = ERROR_SUCCESS;
    const DWORD waitResult = WaitForProgramRuntime(runtime, waitError);
    if (waitResult == WAIT_OBJECT_0 + 1U || waitResult == WAIT_FAILED) {
        runtime.RequestStop();
    }
    runtime.Wait();
    const inputweaver::WindowsProgramRuntimeSessionMetrics metrics = runtime.Metrics();
    PrintProgramMetrics(metrics, diagnosticLog);
    if (waitResult == WAIT_FAILED) {
        std::wcerr << L"WaitForMultipleObjects failed with Win32 error "
                   << waitError << L".\n";
        return 7;
    }
    return metrics.circuitBreakerOpen || metrics.injectionFailures != 0U
        ? 8
        : 0;
}

int RunCompiledProgram(
    const WindowsExecutorOptions& options,
    inputweaver::WindowsSelfTag selfTag,
    inputweaver::DiagnosticLog& diagnosticLog,
    const std::shared_ptr<const inputweaver::CompiledProgram>& program,
    inputweaver::TargetSelectorKind targetKind,
    const std::wstring& targetSelector) {
    if (targetKind == inputweaver::TargetSelectorKind::Global) {
        return RunCompiledInstance(
            options,
            selfTag,
            diagnosticLog,
            program,
            targetKind,
            nullptr);
    }

    bool waitingMessagePrinted = false;
    bool ambiguousMessagePrinted = false;
    while (!StopWasRequested()) {
        const inputweaver::win32::LocateResult located =
            inputweaver::win32::LocateExecutable(targetSelector);
        if (located.status == inputweaver::win32::LocateStatus::Error) {
            std::wcerr << L"Error: target search failed with Win32 error "
                       << located.win32Error << L".\n";
            return 3;
        }
        if (located.status == inputweaver::win32::LocateStatus::None) {
            if (!waitingMessagePrinted) {
                std::wcout << L"Waiting for target " << targetSelector << L"...\n"
                           << std::flush;
                waitingMessagePrinted = true;
            }
            if (WaitForRetry()) {
                break;
            }
            continue;
        }

        inputweaver::win32::LocatedProcess selected{};
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
            if (WaitForRetry()) {
                break;
            }
            continue;
        }

        inputweaver::TargetProcessContext targetContext;
        const inputweaver::ProcessContextResult targetResult =
            targetContext.Initialize(selected.processId, selected.imagePath);
        if (targetResult.error == inputweaver::ProcessContextError::TargetExited
            || targetResult.error == inputweaver::ProcessContextError::TargetImageMismatch) {
            continue;
        }
        if (!targetResult.Succeeded()) {
            std::cerr << "Target validation failed: "
                      << inputweaver::ProcessContextErrorName(targetResult.error)
                      << " (Win32 error " << targetResult.win32Error << ").\n";
            return 3;
        }

        std::wcout << L"Loaded " << options.programPath.wstring() << L"\n"
                   << L"Attached to PID " << selected.processId << L": "
                   << selected.imagePath << L"\n" << std::flush;
        const int sessionResult = RunCompiledInstance(
            options,
            selfTag,
            diagnosticLog,
            program,
            targetKind,
            &targetContext);
        if (sessionResult != 0 || StopWasRequested()) {
            return sessionResult;
        }
        DWORD livenessError = ERROR_SUCCESS;
        if (targetContext.IsTargetAlive(&livenessError)) {
            return 0;
        }
        std::wcout << L"Target exited; waiting for it to restart.\n" << std::flush;
        waitingMessagePrinted = false;
        ambiguousMessagePrinted = false;
    }
    return 0;
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

    gConsoleStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (gConsoleStopEvent == nullptr) {
        std::wcerr << L"Error: cannot create the console stop event. Win32 error "
                   << GetLastError() << L".\n";
        diagnosticLog.Stop();
        return 5;
    }
    if (!SetConsoleCtrlHandler(&ConsoleControlHandler, TRUE)) {
        const DWORD consoleError = GetLastError();
        std::wcerr << L"Error: cannot install the console control handler. Win32 error "
                   << consoleError << L".\n";
        CloseHandle(gConsoleStopEvent);
        gConsoleStopEvent = nullptr;
        diagnosticLog.Stop();
        return 5;
    }

    const inputweaver::WindowsSelfTag selfTag = GenerateSelfTag();
    const int result = RunCompiledProgram(
        options,
        selfTag,
        diagnosticLog,
        compiledProgram,
        compiledTargetKind,
        compiledTargetSelector);

    diagnosticLog.Stop();
    PrintFinalDiagnosticMetrics(diagnosticLog);
    SetConsoleCtrlHandler(&ConsoleControlHandler, FALSE);
    CloseHandle(gConsoleStopEvent);
    gConsoleStopEvent = nullptr;
    return result;
}
