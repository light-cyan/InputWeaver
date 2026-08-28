#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace inputweaver::win32 {

struct WindowsExecutorOptions final {
    bool traceInput{};
    bool dryRun{};
    bool allowExec{};
    bool targetGlobal{};
    std::uint32_t excludedProcessId{};
    std::wstring excludedProcessSelector;
    std::wstring targetSelector;
    std::wstring jsonlPath;
    std::wstring debugSessionToken;
    std::filesystem::path programPath;
};

[[nodiscard]] int RunWindowsExecutor(const WindowsExecutorOptions& options);

}  // namespace inputweaver::win32
