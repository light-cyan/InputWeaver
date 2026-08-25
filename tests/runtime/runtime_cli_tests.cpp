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

} // namespace

int main()
{
    TestDebugSessionOption();
    if (gFailureCount != 0) {
        std::cerr << gFailureCount << " runtime CLI test(s) failed.\n";
        return 1;
    }
    std::cout << "Runtime CLI tests passed.\n";
    return 0;
}
