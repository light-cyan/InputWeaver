#include "platform/windows/runtime/windows_executor.hpp"
#include "ui/cli/runtime_cli.hpp"

#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>

int wmain(int argumentCount, wchar_t** arguments) {
    std::vector<std::filesystem::path> commandLine;
    if (argumentCount > 0) {
        commandLine.reserve(static_cast<std::size_t>(argumentCount));
    }
    for (int index = 0; index < argumentCount; ++index) {
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
    executor.targetSelector = options.targetSelector.wstring();
    executor.jsonlPath = options.jsonlPath.wstring();
    executor.debugSessionToken = options.debugSessionToken.wstring();
    executor.programPath = options.programPath;
    return inputweaver::win32::RunWindowsExecutor(executor);
}
