#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core/input_event.hpp"

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
    DWORD currentIntegrityRid{0};
    DWORD targetIntegrityRid{0};

    [[nodiscard]] bool Succeeded() const noexcept {
        return error == ProcessContextError::None;
    }
};

struct IntegrityLevelResult {
    DWORD integrityRid{0};
    DWORD win32Error{ERROR_SUCCESS};
    bool succeeded{false};
};

[[nodiscard]] const char* ProcessContextErrorName(
    ProcessContextError error) noexcept;

[[nodiscard]] IntegrityLevelResult QueryProcessIntegrityLevel(
    HANDLE process) noexcept;

[[nodiscard]] bool IsTargetIntegrityCompatible(
    DWORD currentIntegrityRid,
    DWORD targetIntegrityRid) noexcept;

[[nodiscard]] bool IsProcessForeground(ProcessId processId) noexcept;
[[nodiscard]] bool IsProcessPointerTarget(
    ProcessId processId,
    ScreenPoint screenPoint) noexcept;

class TargetProcessContext final {
public:
    TargetProcessContext() noexcept = default;
    ~TargetProcessContext();

    TargetProcessContext(const TargetProcessContext&) = delete;
    TargetProcessContext& operator=(const TargetProcessContext&) = delete;

    TargetProcessContext(TargetProcessContext&& other) noexcept;
    TargetProcessContext& operator=(TargetProcessContext&& other) noexcept;

    [[nodiscard]] ProcessContextResult Initialize(ProcessId targetPid) noexcept;
    [[nodiscard]] ProcessContextResult Initialize(
        ProcessId targetPid,
        std::wstring_view expectedImagePath) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool IsTargetAlive(DWORD* win32Error = nullptr) const noexcept;
    [[nodiscard]] bool IsTargetForeground() const noexcept;
    [[nodiscard]] bool IsTargetPointerTarget(ScreenPoint screenPoint) const noexcept;
    [[nodiscard]] bool IsTargetPointerTargetAtCursor() const noexcept;

    [[nodiscard]] ProcessId TargetPid() const noexcept;
    [[nodiscard]] HANDLE TargetHandle() const noexcept;
    [[nodiscard]] DWORD CurrentIntegrityRid() const noexcept;
    [[nodiscard]] DWORD TargetIntegrityRid() const noexcept;

private:
    HANDLE targetHandle_{nullptr};
    ProcessId targetPid_{0};
    DWORD currentIntegrityRid_{0};
    DWORD targetIntegrityRid_{0};
};

}  // namespace inputweaver
