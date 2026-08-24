#pragma once

#include <filesystem>
#include <string>

namespace inputweaver::win32 {

struct WindowsExecutorOptions final {
    bool traceInput{};
    bool allowExec{};
    bool targetGlobal{};
    std::wstring targetSelector;
    std::wstring jsonlPath;
    std::filesystem::path programPath;
};

[[nodiscard]] int RunWindowsExecutor(const WindowsExecutorOptions& options);

}  // namespace inputweaver::win32
