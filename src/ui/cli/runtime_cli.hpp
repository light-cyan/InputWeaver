#pragma once

#include <filesystem>
#include <iosfwd>
#include <cstdint>
#include <span>
#include <string>

namespace inputweaver::ui::cli {

struct RuntimeCliOptions final {
    bool showHelp{};
    bool traceInput{};
    bool dryRun{};
    bool allowExec{};
    bool targetGlobal{};
    std::uint32_t excludedProcessId{};
    std::filesystem::path targetSelector;
    std::filesystem::path jsonlPath;
    std::filesystem::path debugSessionToken;
    std::filesystem::path programPath;
};

[[nodiscard]] bool ParseRuntimeCommandLine(
    std::span<const std::filesystem::path> arguments,
    RuntimeCliOptions& options,
    std::string& errorMessage);

void PrintRuntimeUsage(std::ostream& output);

}  // namespace inputweaver::ui::cli
