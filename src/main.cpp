#include "diagnostics/diagnostic_log.hpp"
#include "app/runtime.hpp"
#include "platform/windows/process_locator.hpp"
#include "platform/windows/process_context.hpp"

#include <cstdint>
#include <iostream>
#include <string>

#include <windows.h>

namespace {

struct CommandLineOptions {
    bool showHelp{};
    bool testRules{};
    bool traceInput{};
    std::wstring targetSelector;
    std::wstring jsonlPath;
};

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

void PrintUsage() {
    std::wcout
        << L"UniversalKeyRemapper\n\n"
        << L"Observer mode:\n"
        << L"  UniversalKeyRemapper.exe [--log <jsonl-path>] [--trace-input]\n\n"
        << L"Phase 1 fixed-rule mode:\n"
        << L"  UniversalKeyRemapper.exe --test-rules --target <exe-name-or-absolute-path>"
           L" [--log <jsonl-path>] [--trace-input]\n\n"
        << L"Fixed rules: F6 -> F7, F7 -> F8, F9 -> middle click,"
           L" middle button -> F10.\n"
        << L"Rules are active only while the selected process owns the foreground window.\n"
        << L"Physical Ctrl+Shift+F12 stops the program.\n";
}

bool ParseCommandLine(
    int argumentCount,
    wchar_t** arguments,
    CommandLineOptions& options,
    std::wstring& errorMessage) {
    for (int index = 1; index < argumentCount; ++index) {
        const std::wstring argument = arguments[index];
        if (argument == L"--help" || argument == L"-h") {
            options.showHelp = true;
        } else if (argument == L"--test-rules") {
            options.testRules = true;
        } else if (argument == L"--trace-input") {
            options.traceInput = true;
        } else if (argument == L"--target") {
            if (++index >= argumentCount || arguments[index][0] == L'\0') {
                errorMessage = L"--target requires an executable name or absolute path.";
                return false;
            }
            options.targetSelector = arguments[index];
        } else if (argument == L"--log") {
            if (++index >= argumentCount || arguments[index][0] == L'\0') {
                errorMessage = L"--log requires a JSONL file path.";
                return false;
            }
            options.jsonlPath = arguments[index];
        } else {
            errorMessage = L"Unknown option: " + argument;
            return false;
        }
    }

    if (options.showHelp) {
        return true;
    }
    if (options.testRules && options.targetSelector.empty()) {
        errorMessage = L"--test-rules requires --target.";
        return false;
    }
    if (!options.testRules && !options.targetSelector.empty()) {
        errorMessage = L"--target is valid only with --test-rules.";
        return false;
    }
    if (options.traceInput && options.jsonlPath.empty()) {
        errorMessage = L"--trace-input requires --log.";
        return false;
    }
    return true;
}

std::uint64_t Mix64(std::uint64_t value) noexcept {
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return value;
}

ukr::SelfTag GenerateSelfTag() noexcept {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    std::uint64_t seed = static_cast<std::uint64_t>(counter.QuadPart);
    seed ^= static_cast<std::uint64_t>(GetTickCount64());
    seed ^= static_cast<std::uint64_t>(GetCurrentProcessId()) << 32U;
    seed ^= static_cast<std::uint64_t>(GetCurrentThreadId()) << 16U;
    const std::uint64_t mixed = Mix64(seed + 0x9E3779B97F4A7C15ULL);
    const std::uint32_t folded = static_cast<std::uint32_t>(mixed) ^
                                 static_cast<std::uint32_t>(mixed >> 32U);
    return folded == 0 ? static_cast<ukr::SelfTag>(0x554B5231U) : folded;
}

void PrintMetrics(
    const ukr::AppRuntimeMetrics& metrics,
    const ukr::DiagnosticLog& diagnosticLog) {
    std::wcout << L"Session stopped. hook_events=" << metrics.hookEvents
               << L" suppressed=" << metrics.suppressedEvents
               << L" queued=" << metrics.queuedBatches
               << L" cancelled=" << metrics.cancelledBatches
               << L" action_queue_rejections=" << metrics.rejectedActionPushes
               << L" injection_failures=" << metrics.injectionFailures
               << L" max_hook_us=" << metrics.maximumHookMicroseconds
               << L" unresolved_synthetic_releases=" << metrics.unresolvedSyntheticReleases
               << L" hook_log_drops=" << diagnosticLog.DroppedHookRecords()
               << L" injection_log_drops=" << diagnosticLog.DroppedInjectionRecords()
               << L" jsonl_bytes=" << diagnosticLog.JsonlBytesWritten()
               << L" jsonl_truncated=" << (diagnosticLog.JsonlTruncated() ? L"true" : L"false")
               << L"\n";
}

bool StopWasRequested() noexcept {
    return gConsoleStopEvent != nullptr &&
           WaitForSingleObject(gConsoleStopEvent, 0) == WAIT_OBJECT_0;
}

bool WaitForRetry() noexcept {
    return gConsoleStopEvent != nullptr &&
           WaitForSingleObject(gConsoleStopEvent, 1000) == WAIT_OBJECT_0;
}

DWORD WaitForRuntime(ukr::AppRuntime& runtime, DWORD& waitError) {
    const HANDLE waitHandles[] = {
        runtime.StoppedEvent(),
        gConsoleStopEvent,
        runtime.ActionQueueErrorEvent()};
    for (;;) {
        const DWORD waitResult = WaitForMultipleObjects(3, waitHandles, FALSE, INFINITE);
        if (waitResult != WAIT_OBJECT_0 + 2) {
            waitError = waitResult == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
            return waitResult;
        }

        const ukr::AppRuntimeMetrics metrics = runtime.Metrics();
        std::wcerr
            << L"Runtime error: the action queue reached capacity; the triggering input "
               L"was forwarded to keep the system responsive. "
            << L"total_action_queue_rejections=" << metrics.rejectedActionPushes << L"\n";
    }
}

bool SelectLocatedProcess(
    const ukr::win32::LocateResult& located,
    ukr::win32::LocatedProcess& selected) noexcept {
    if (const ukr::win32::LocatedProcess* unique = located.UniqueMatch()) {
        selected = *unique;
        return true;
    }

    const ukr::win32::LocatedProcess* foreground = nullptr;
    for (const auto& candidate : located.matches) {
        if (ukr::IsProcessForeground(candidate.processId)) {
            if (foreground != nullptr) {
                return false;
            }
            foreground = &candidate;
        }
    }
    if (foreground == nullptr) {
        return false;
    }
    selected = *foreground;
    return true;
}

int RunObserver(
    const CommandLineOptions& options,
    ukr::SelfTag selfTag,
    ukr::DiagnosticLog& diagnosticLog) {
    ukr::AppRuntime runtime({false, options.traceInput, selfTag}, nullptr, diagnosticLog);
    std::wstring errorMessage;
    if (!runtime.Start(errorMessage)) {
        std::wcerr << L"Error: " << errorMessage << L"\n";
        return 6;
    }

    std::wcout << L"Observer mode is active; no input will be suppressed or generated.\n"
               << L"Press physical Ctrl+Shift+F12 to stop.\n";
    DWORD waitError = ERROR_SUCCESS;
    const DWORD waitResult = WaitForRuntime(runtime, waitError);
    if (waitResult == WAIT_OBJECT_0 + 1 || waitResult == WAIT_FAILED) {
        runtime.RequestStop();
    }
    runtime.Wait();
    const ukr::AppRuntimeMetrics metrics = runtime.Metrics();
    PrintMetrics(metrics, diagnosticLog);
    if (waitResult == WAIT_FAILED) {
        std::wcerr << L"WaitForMultipleObjects failed with Win32 error " << waitError << L".\n";
        return 7;
    }
    if (metrics.unresolvedSyntheticReleases != 0) {
        return 9;
    }
    return metrics.circuitBreakerOpen ? 8 : 0;
}

int RunTargeted(
    const CommandLineOptions& options,
    ukr::SelfTag selfTag,
    ukr::DiagnosticLog& diagnosticLog) {
    bool waitingMessagePrinted = false;
    bool ambiguousMessagePrinted = false;

    while (!StopWasRequested()) {
        const ukr::win32::LocateResult located =
            ukr::win32::LocateExecutable(options.targetSelector);
        if (located.status == ukr::win32::LocateStatus::Error) {
            std::wcerr << L"Error: target search failed with Win32 error "
                       << located.win32Error << L".\n";
            return 3;
        }
        if (located.status == ukr::win32::LocateStatus::None) {
            if (!waitingMessagePrinted) {
                std::wcout << L"Waiting for target " << options.targetSelector << L"...\n";
                waitingMessagePrinted = true;
            }
            if (WaitForRetry()) {
                break;
            }
            continue;
        }

        ukr::win32::LocatedProcess selected{};
        if (!SelectLocatedProcess(located, selected)) {
            if (!ambiguousMessagePrinted) {
                std::wcout << L"Multiple target processes match; focus the intended instance:\n";
                for (const auto& candidate : located.matches) {
                    std::wcout << L"  PID " << candidate.processId << L"  "
                               << candidate.imagePath << L"\n";
                }
                ambiguousMessagePrinted = true;
            }
            if (WaitForRetry()) {
                break;
            }
            continue;
        }

        waitingMessagePrinted = false;
        ambiguousMessagePrinted = false;
        ukr::TargetProcessContext targetContext;
        const ukr::ProcessContextResult targetResult =
            targetContext.Initialize(selected.processId, selected.imagePath);
        if (targetResult.error == ukr::ProcessContextError::TargetExited ||
            targetResult.error == ukr::ProcessContextError::TargetImageMismatch) {
            continue;
        }
        if (!targetResult.Succeeded()) {
            std::cerr << "Target validation failed: "
                      << ukr::ProcessContextErrorName(targetResult.error)
                      << " (Win32 error " << targetResult.win32Error << ").\n";
            return 3;
        }

        std::wcout << L"Attached to PID " << selected.processId << L": "
                   << selected.imagePath << L"\n"
                   << L"Fixed rules are active only while this process is foreground.\n"
                   << L"Press physical Ctrl+Shift+F12 to stop.\n";

        ukr::AppRuntime runtime({true, options.traceInput, selfTag}, &targetContext, diagnosticLog);
        std::wstring errorMessage;
        if (!runtime.Start(errorMessage)) {
            std::wcerr << L"Error: " << errorMessage << L"\n";
            return 6;
        }

        DWORD waitError = ERROR_SUCCESS;
        const DWORD waitResult = WaitForRuntime(runtime, waitError);
        if (waitResult == WAIT_OBJECT_0 + 1 || waitResult == WAIT_FAILED) {
            runtime.RequestStop();
        }
        runtime.Wait();
        const ukr::AppRuntimeMetrics metrics = runtime.Metrics();
        PrintMetrics(metrics, diagnosticLog);

        if (waitResult == WAIT_FAILED) {
            std::wcerr << L"WaitForMultipleObjects failed with Win32 error " << waitError << L".\n";
            return 7;
        }
        if (metrics.unresolvedSyntheticReleases != 0) {
            return 9;
        }
        if (metrics.circuitBreakerOpen) {
            return 8;
        }
        if (StopWasRequested()) {
            break;
        }

        DWORD livenessError = ERROR_SUCCESS;
        if (targetContext.IsTargetAlive(&livenessError)) {
            return 0;
        }
        std::wcout << L"Target exited; waiting for it to restart.\n";
    }
    return 0;
}

}  // namespace

int wmain(int argumentCount, wchar_t** arguments) {
    CommandLineOptions options{};
    std::wstring errorMessage;
    if (!ParseCommandLine(argumentCount, arguments, options, errorMessage)) {
        std::wcerr << L"Error: " << errorMessage << L"\n\n";
        PrintUsage();
        return 2;
    }
    if (options.showHelp) {
        PrintUsage();
        return 0;
    }

    ukr::DiagnosticLog diagnosticLog;
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

    const ukr::SelfTag selfTag = GenerateSelfTag();
    const int result = options.testRules
        ? RunTargeted(options, selfTag, diagnosticLog)
        : RunObserver(options, selfTag, diagnosticLog);

    diagnosticLog.Stop();
    SetConsoleCtrlHandler(&ConsoleControlHandler, FALSE);
    CloseHandle(gConsoleStopEvent);
    gConsoleStopEvent = nullptr;
    return result;
}
