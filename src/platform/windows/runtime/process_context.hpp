#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "windows_input_types.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace inputweaver {

enum class ProcessContextError : unsigned char {
    None,
    InvalidPid,
    OpenProcessFailed,
    TargetExited,
    TargetLivenessCheckFailed,
    TargetImageQueryFailed,
    TargetImageMismatch,
    CurrentTokenOpenFailed,
    CurrentIntegrityQueryFailed,
    TargetTokenOpenFailed,
    TargetIntegrityQueryFailed,
    TargetIntegrityHigher
};

struct ProcessContextResult {
    ProcessContextError error{ProcessContextError::None};
    DWORD win32Error{ERROR_SUCCESS};

    [[nodiscard]] bool Succeeded() const noexcept {
        return error == ProcessContextError::None;
    }
};

[[nodiscard]] const char* ProcessContextErrorName(
    ProcessContextError error) noexcept;

[[nodiscard]] bool IsTargetIntegrityCompatible(
    DWORD currentIntegrityRid,
    DWORD targetIntegrityRid) noexcept;

[[nodiscard]] bool IsProcessForeground(WindowsProcessId processId) noexcept;

class ForegroundProcessExclusion final {
public:
    explicit ForegroundProcessExclusion(std::wstring executableSelector = {});

    [[nodiscard]] bool Enabled() const noexcept;
    [[nodiscard]] bool IsForegroundExcluded() const noexcept;

private:
    std::wstring executableSelector_;
    mutable std::atomic<std::uint64_t> cachedResult_{0U};
    mutable std::atomic<std::uintptr_t> cachedWindow_{0U};
};

class TargetProcessContext final {
public:
    TargetProcessContext() noexcept = default;
    ~TargetProcessContext();

    TargetProcessContext(const TargetProcessContext&) = delete;
    TargetProcessContext& operator=(const TargetProcessContext&) = delete;

    TargetProcessContext(TargetProcessContext&& other) noexcept;
    TargetProcessContext& operator=(TargetProcessContext&& other) noexcept;

    [[nodiscard]] ProcessContextResult Initialize(WindowsProcessId targetPid) noexcept;
    [[nodiscard]] ProcessContextResult Initialize(
        WindowsProcessId targetPid,
        std::wstring_view expectedImagePath) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool IsTargetAlive(DWORD* win32Error = nullptr) const noexcept;
    [[nodiscard]] bool IsTargetForeground() const noexcept;
    [[nodiscard]] bool IsTargetPointerTarget(ScreenPoint screenPoint) const noexcept;
    [[nodiscard]] bool IsTargetPointerTargetAtCursor() const noexcept;

    [[nodiscard]] WindowsProcessId TargetPid() const noexcept;
    [[nodiscard]] HANDLE TargetHandle() const noexcept;

private:
    HANDLE targetHandle_{nullptr};
    WindowsProcessId targetPid_{0};
};

}  // namespace inputweaver
