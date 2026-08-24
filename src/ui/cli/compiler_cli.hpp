#pragma once

#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>

namespace inputweaver::ui::cli {

enum class CompilerCliCommand {
    Compile,
    Validate,
    Dump,
};

struct CompilerCliOptions final {
    CompilerCliCommand command{CompilerCliCommand::Compile};
    std::filesystem::path source;
    std::filesystem::path destination;
};

[[nodiscard]] bool ParseCompilerCommandLine(
    std::span<const std::filesystem::path> arguments,
    CompilerCliOptions& options,
    std::string& errorMessage);

[[nodiscard]] int RunCompilerCommand(
    const CompilerCliOptions& options,
    std::ostream& output,
    std::ostream& errors);

void PrintCompilerUsage(std::ostream& output);

}  // namespace inputweaver::ui::cli
