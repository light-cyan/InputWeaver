#include "ui/cli/runtime_cli.hpp"

#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

int gFailureCount = 0;

void Check(bool condition, std::string_view name)
{
    if (!condition) {
        ++gFailureCount;
        std::cerr << "FAIL: " << name << '\n';
    }
}

void TestDebugSessionOption()
{
    std::vector<std::filesystem::path> arguments = {
        "InputWeaver",
        "--program",
        "test.weavec",
        "--debug-session",
        "session-123"};
    inputweaver::ui::cli::RuntimeCliOptions options{};
    std::string error;
    Check(
        inputweaver::ui::cli::ParseRuntimeCommandLine(
            std::span<const std::filesystem::path>{arguments},
            options,
            error)
            && options.debugSessionToken
                == std::filesystem::path{"session-123"},
        "debug session token is parsed with the compiled program");

    arguments.pop_back();
    options = {};
    error.clear();
    Check(
        !inputweaver::ui::cli::ParseRuntimeCommandLine(
            std::span<const std::filesystem::path>{arguments},
            options,
            error)
            && error.find("--debug-session") != std::string::npos,
        "missing debug session token is rejected");
}

void TestDryRunOption()
{
    const std::vector<std::filesystem::path> arguments = {
        "InputWeaver",
        "--program",
        "test.weavec",
        "--dry-run",
        "--allow-exec"};
    inputweaver::ui::cli::RuntimeCliOptions options{};
    std::string error;
    Check(
        inputweaver::ui::cli::ParseRuntimeCommandLine(
            std::span<const std::filesystem::path>{arguments},
            options,
            error)
            && options.dryRun
            && options.allowExec,
        "dry-run composes with explicit process-launch permission");
}

void TestExcludedProcessOption()
{
    std::vector<std::filesystem::path> arguments = {
        "InputWeaver",
        "--program",
        "test.weavec",
        "--target-global",
        "--exclude-process",
        "4242"};
    inputweaver::ui::cli::RuntimeCliOptions options{};
    std::string error;
    Check(
        inputweaver::ui::cli::ParseRuntimeCommandLine(
            std::span<const std::filesystem::path>{arguments},
            options,
            error)
            && options.targetGlobal
            && options.excludedProcessId == 4242U,
        "global target accepts one excluded process PID");

    for (const std::string_view invalid : {"", "0", "12x"}) {
        arguments.back() = invalid;
        options = {};
        error.clear();
        Check(
            !inputweaver::ui::cli::ParseRuntimeCommandLine(
                std::span<const std::filesystem::path>{arguments},
                options,
                error)
                && error.find("--exclude-process") != std::string::npos,
            "invalid excluded process PID is rejected");
    }
}

} // namespace

int main()
{
    TestDebugSessionOption();
    TestDryRunOption();
    TestExcludedProcessOption();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " runtime CLI test(s) failed.\n";
        return 1;
    }
    std::cout << "Runtime CLI tests passed.\n";
    return 0;
}
