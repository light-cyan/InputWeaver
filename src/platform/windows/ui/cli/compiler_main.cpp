#include "ui/cli/compiler_cli.hpp"

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

    inputweaver::ui::cli::CompilerCliOptions options;
    std::string errorMessage;
    if (!inputweaver::ui::cli::ParseCompilerCommandLine(
            std::span<const std::filesystem::path>{commandLine},
            options,
            errorMessage)) {
        std::cerr << "Error: " << errorMessage << "\n\n";
        inputweaver::ui::cli::PrintCompilerUsage(std::cerr);
        return 2;
    }
    return inputweaver::ui::cli::RunCompilerCommand(
        options, std::cout, std::cerr);
}
