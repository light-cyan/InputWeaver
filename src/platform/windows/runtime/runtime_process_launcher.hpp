#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "runtime/runtime_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace inputweaver::win32 {

enum class ExecutableResolutionError : std::uint8_t {
    None,
    InvalidUtf8,
    EmptyCommand,
    UnterminatedQuote,
    InvalidTokenBoundary,
    InvalidToken,
    EnvironmentQueryFailed,
    ExecutableNotFound,
    WorkingDirectoryUnavailable,
    CommandTooLong,
};

struct ExecutableResolutionResult final {
    ExecutableResolutionError error{ExecutableResolutionError::None};
    DWORD win32Error{ERROR_SUCCESS};
    std::wstring commandLine;
    std::wstring executableToken;
    std::wstring executablePath;
    std::wstring workingDirectory;

    [[nodiscard]] bool Succeeded() const noexcept {
        return error == ExecutableResolutionError::None;
    }
};

[[nodiscard]] ExecutableResolutionResult ResolveExecutableCommand(
    std::string_view authoredCommand) noexcept;

using CreateProcessWFunction = decltype(&::CreateProcessW);

class WindowsProcessLauncher final : public RuntimeProcessLauncher {
public:
    explicit WindowsProcessLauncher(
        bool permitted = true,
        CreateProcessWFunction createProcess = &::CreateProcessW) noexcept;

    [[nodiscard]] bool Permitted() const noexcept override;
    [[nodiscard]] RuntimeLaunchResult Launch(
        std::string_view command,
        RuntimeCancellationProbe cancellation) noexcept override;

    [[nodiscard]] ExecutableResolutionError LastResolutionError() const noexcept;
    [[nodiscard]] DWORD LastWin32Error() const noexcept;

private:
    bool permitted_;
    CreateProcessWFunction createProcess_;
    ExecutableResolutionError lastResolutionError_{
        ExecutableResolutionError::None};
    DWORD lastWin32Error_{ERROR_SUCCESS};
};

} // namespace inputweaver::win32
