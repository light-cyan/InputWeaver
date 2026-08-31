#include "platform/windows/runtime/windows_executor.hpp"
#include "ui/cli/runtime_cli.hpp"

#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] bool ParseHandleValue(
    std::wstring_view text,
    std::uintptr_t& value) noexcept
{
    if (text.empty()) {
        return false;
    }
    std::uintptr_t parsed{};
    for (const wchar_t character : text) {
        if (character < L'0' || character > L'9') {
            return false;
        }
        const std::uintptr_t digit = static_cast<std::uintptr_t>(
            character - L'0');
        if (parsed > ((std::numeric_limits<std::uintptr_t>::max)() - digit)
                / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
    }
    value = parsed;
    return parsed != 0U;
}

} // namespace

int wmain(int argumentCount, wchar_t** arguments) {
    std::vector<std::filesystem::path> commandLine;
    std::uintptr_t inheritedStopEvent{};
    if (argumentCount > 0) {
        commandLine.reserve(static_cast<std::size_t>(argumentCount));
    }
    for (int index = 0; index < argumentCount; ++index) {
        if (std::wstring_view{arguments[index]} == L"--host-stop-event") {
            if (inheritedStopEvent != 0U
                || index + 1 >= argumentCount
                || !ParseHandleValue(arguments[++index], inheritedStopEvent)) {
                std::cerr << "Error: invalid host stop event.\n";
                return 2;
            }
            continue;
        }
        commandLine.emplace_back(arguments[index]);
    }

    inputweaver::ui::cli::RuntimeCliOptions options;
    std::string errorMessage;
    if (!inputweaver::ui::cli::ParseRuntimeCommandLine(
            std::span<const std::filesystem::path>{commandLine},
            options,
            errorMessage)) {
        std::cerr << "Error: " << errorMessage << "\n\n";
        inputweaver::ui::cli::PrintRuntimeUsage(std::cerr);
        return 2;
    }
    if (options.showHelp) {
        inputweaver::ui::cli::PrintRuntimeUsage(std::cout);
        return 0;
    }

    inputweaver::win32::WindowsExecutorOptions executor;
    executor.traceInput = options.traceInput;
    executor.dryRun = options.dryRun;
    executor.allowExec = options.allowExec;
    executor.targetGlobal = options.targetGlobal;
    executor.excludedProcessSelector = options.excludedProcessSelector.wstring();
    executor.targetSelector = options.targetSelector.wstring();
    executor.jsonlPath = options.jsonlPath.wstring();
    executor.debugSessionToken = options.debugSessionToken.wstring();
    executor.inheritedStopEvent = inheritedStopEvent;
    executor.programPath = options.programPath;
    return inputweaver::win32::RunWindowsExecutor(executor);
}
