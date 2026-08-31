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
    std::wstring excludedProcessSelector;
    std::wstring targetSelector;
    std::wstring jsonlPath;
    std::wstring debugSessionToken;
    std::uintptr_t inheritedStopEvent{};
    std::filesystem::path programPath;
};

[[nodiscard]] int RunWindowsExecutor(const WindowsExecutorOptions& options);

}  // namespace inputweaver::win32
