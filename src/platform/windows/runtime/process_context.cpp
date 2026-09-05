#include "process_context.hpp"

#include "platform/windows/support/ordinal_string.hpp"
#include "platform/windows/support/unique_handle.hpp"
#include "process_locator.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <utility>

namespace inputweaver {
namespace {

constexpr DWORD kTargetProcessAccess =
    PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE;
constexpr DWORD kMaximumImagePathCharacters = 32768;

struct IntegrityLevelResult {
    DWORD integrityRid{0};
    DWORD win32Error{ERROR_SUCCESS};
    bool succeeded{false};
};

[[nodiscard]] bool WindowBelongsToProcess(
    HWND window,
    DWORD processId) noexcept {
    if (window == nullptr || processId == 0) {
        return false;
    }
    HWND root = GetAncestor(window, GA_ROOT);
    if (root == nullptr) {
        root = window;
    }
    DWORD windowProcessId = 0;
    GetWindowThreadProcessId(root, &windowProcessId);
    return windowProcessId == processId;
}

[[nodiscard]] IntegrityLevelResult QueryTokenIntegrityLevel(
    HANDLE token) noexcept {
    constexpr std::size_t bufferSize =
        sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE;
    alignas(TOKEN_MANDATORY_LABEL) std::array<std::byte, bufferSize> buffer{};
    DWORD returnedSize = 0;
    if (!GetTokenInformation(
            token,
            TokenIntegrityLevel,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &returnedSize)) {
        return {0, GetLastError(), false};
    }

    auto* label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer.data());
    PSID integritySid = label->Label.Sid;
    if (integritySid == nullptr || !IsValidSid(integritySid)) {
        return {0, ERROR_INVALID_SID, false};
    }

    const PUCHAR subAuthorityCount = GetSidSubAuthorityCount(integritySid);
    if (subAuthorityCount == nullptr || *subAuthorityCount == 0) {
        return {0, ERROR_INVALID_SID, false};
    }

    const PDWORD integrityRid =
        GetSidSubAuthority(integritySid, *subAuthorityCount - 1U);
    if (integrityRid == nullptr) {
        return {0, ERROR_INVALID_SID, false};
    }

    return {*integrityRid, ERROR_SUCCESS, true};
}

struct ImagePathMatchResult {
    DWORD win32Error{ERROR_SUCCESS};
    bool queried{false};
    bool matches{false};
};

[[nodiscard]] bool EqualNormalizedImagePath(
    std::wstring_view actual,
    std::wstring_view expected) noexcept {
    constexpr std::wstring_view localPrefix = L"\\\\?\\";
    constexpr std::wstring_view uncPrefix = L"\\\\?\\UNC\\";
    if (actual.size() >= uncPrefix.size() &&
        win32::EqualOrdinalIgnoreCase(actual.substr(0, uncPrefix.size()), uncPrefix)) {
        return expected.size() >= 2U && expected[0] == L'\\' && expected[1] == L'\\' &&
               win32::EqualOrdinalIgnoreCase(actual.substr(uncPrefix.size()), expected.substr(2U));
    }
    if (actual.size() >= localPrefix.size() &&
        win32::EqualOrdinalIgnoreCase(actual.substr(0, localPrefix.size()), localPrefix)) {
        actual.remove_prefix(localPrefix.size());
    }
    return win32::EqualOrdinalIgnoreCase(actual, expected);
}

[[nodiscard]] ImagePathMatchResult ProcessImagePathMatches(
    HANDLE process,
    std::wstring_view expectedImagePath) noexcept {
    std::array<wchar_t, kMaximumImagePathCharacters> path{};
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
        return {GetLastError(), false, false};
    }
    if (expectedImagePath.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        length > static_cast<DWORD>((std::numeric_limits<int>::max)())) {
        return {ERROR_FILENAME_EXCED_RANGE, true, false};
    }
    const bool matches = EqualNormalizedImagePath(
        std::wstring_view(path.data(), static_cast<std::size_t>(length)),
        expectedImagePath);
    return {ERROR_SUCCESS, true, matches};
}

}  // namespace

const char* ProcessContextErrorName(ProcessContextError error) noexcept {
    switch (error) {
        case ProcessContextError::None:
            return "none";
        case ProcessContextError::InvalidPid:
            return "invalid-pid";
        case ProcessContextError::OpenProcessFailed:
            return "open-process-failed";
        case ProcessContextError::TargetExited:
            return "target-exited";
        case ProcessContextError::TargetLivenessCheckFailed:
            return "target-liveness-check-failed";
        case ProcessContextError::TargetImageQueryFailed:
            return "target-image-query-failed";
        case ProcessContextError::TargetImageMismatch:
            return "target-image-mismatch";
        case ProcessContextError::CurrentTokenOpenFailed:
            return "current-token-open-failed";
        case ProcessContextError::CurrentIntegrityQueryFailed:
            return "current-integrity-query-failed";
        case ProcessContextError::TargetTokenOpenFailed:
            return "target-token-open-failed";
        case ProcessContextError::TargetIntegrityQueryFailed:
            return "target-integrity-query-failed";
        case ProcessContextError::TargetIntegrityHigher:
            return "target-integrity-higher";
    }
    return "unknown";
}

bool IsTargetIntegrityCompatible(
    DWORD currentIntegrityRid,
    DWORD targetIntegrityRid) noexcept {
    return targetIntegrityRid <= currentIntegrityRid;
}

bool IsProcessForeground(WindowsProcessId processId) noexcept {
    if (processId == 0) {
        return false;
    }

    const HWND foregroundWindow = GetForegroundWindow();
    if (foregroundWindow == nullptr) {
        return false;
    }

    DWORD foregroundProcessId = 0;
    GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
    return foregroundProcessId == processId;
}

ForegroundProcessExclusion::ForegroundProcessExclusion(
    std::wstring executableSelector)
    : executableSelector_(std::move(executableSelector))
{
}

bool ForegroundProcessExclusion::Enabled() const noexcept
{
    return !executableSelector_.empty();
}

bool ForegroundProcessExclusion::IsForegroundExcluded() const noexcept
{
    if (!Enabled()) {
        return false;
    }
    const HWND foregroundWindow = GetForegroundWindow();
    if (foregroundWindow == nullptr) {
        return false;
    }
    DWORD foregroundProcessId = 0U;
    if (GetWindowThreadProcessId(
            foregroundWindow,
            &foregroundProcessId) == 0U
        || foregroundProcessId == 0U) {
        return false;
    }

    const std::uint64_t cached = cachedResult_.load(std::memory_order_acquire);
    const std::uintptr_t windowIdentity = reinterpret_cast<std::uintptr_t>(
        foregroundWindow);
    if (cachedWindow_.load(std::memory_order_acquire) == windowIdentity
        && static_cast<DWORD>(cached >> 1U) == foregroundProcessId) {
        return (cached & 1U) != 0U;
    }

    const win32::LocateResult located = win32::LocateExecutable(
        executableSelector_);
    win32::LocatedProcess selected{};
    const bool matches = located.status == win32::LocateStatus::Error
        || (win32::SelectLocatedProcess(located, selected)
            && selected.processId == foregroundProcessId);
    const std::uint64_t updated =
        (static_cast<std::uint64_t>(foregroundProcessId) << 1U)
        | static_cast<std::uint64_t>(matches);
    cachedResult_.store(updated, std::memory_order_release);
    cachedWindow_.store(windowIdentity, std::memory_order_release);
    return matches;
}

[[nodiscard]] static bool IsProcessPointerTarget(
    WindowsProcessId processId,
    ScreenPoint screenPoint) noexcept {
    if (processId == 0) {
        return false;
    }

    const HWND foregroundWindow = GetForegroundWindow();
    if (foregroundWindow == nullptr) {
        return false;
    }
    DWORD foregroundProcessId = 0;
    const DWORD foregroundThreadId =
        GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
    if (foregroundThreadId == 0 || foregroundProcessId != processId) {
        return false;
    }

    GUITHREADINFO threadInformation{};
    threadInformation.cbSize = sizeof(threadInformation);
    if (!GetGUIThreadInfo(foregroundThreadId, &threadInformation)) {
        return false;
    }
    const HWND routeWindow = threadInformation.hwndCapture != nullptr
        ? threadInformation.hwndCapture
        : WindowFromPhysicalPoint({
            static_cast<LONG>(screenPoint.x),
            static_cast<LONG>(screenPoint.y)});
    return WindowBelongsToProcess(routeWindow, processId);
}

TargetProcessContext::~TargetProcessContext() {
    Reset();
}

TargetProcessContext::TargetProcessContext(
    TargetProcessContext&& other) noexcept
    : targetHandle_(std::exchange(other.targetHandle_, nullptr)),
      targetPid_(std::exchange(other.targetPid_, 0)) {}

TargetProcessContext& TargetProcessContext::operator=(
    TargetProcessContext&& other) noexcept {
    if (this != &other) {
        Reset();
        targetHandle_ = std::exchange(other.targetHandle_, nullptr);
        targetPid_ = std::exchange(other.targetPid_, 0);
    }
    return *this;
}

ProcessContextResult TargetProcessContext::Initialize(
    WindowsProcessId targetPid) noexcept {
    return Initialize(targetPid, {});
}

ProcessContextResult TargetProcessContext::Initialize(
    WindowsProcessId targetPid,
    std::wstring_view expectedImagePath) noexcept {
    Reset();

    ProcessContextResult result{};
    if (targetPid == 0) {
        result.error = ProcessContextError::InvalidPid;
        result.win32Error = ERROR_INVALID_PARAMETER;
        return result;
    }

    targetHandle_ = OpenProcess(kTargetProcessAccess, FALSE, targetPid);
    if (targetHandle_ == nullptr) {
        result.error = ProcessContextError::OpenProcessFailed;
        result.win32Error = GetLastError();
        return result;
    }
    targetPid_ = targetPid;

    DWORD livenessError = ERROR_SUCCESS;
    if (!IsTargetAlive(&livenessError)) {
        result.error = livenessError == ERROR_PROCESS_ABORTED
            ? ProcessContextError::TargetExited
            : ProcessContextError::TargetLivenessCheckFailed;
        result.win32Error = livenessError;
        Reset();
        return result;
    }

    if (!expectedImagePath.empty()) {
        const ImagePathMatchResult imageMatch =
            ProcessImagePathMatches(targetHandle_, expectedImagePath);
        if (!imageMatch.queried) {
            result.error = ProcessContextError::TargetImageQueryFailed;
            result.win32Error = imageMatch.win32Error;
            Reset();
            return result;
        }
        if (!imageMatch.matches) {
            result.error = ProcessContextError::TargetImageMismatch;
            result.win32Error = ERROR_NOT_FOUND;
            Reset();
            return result;
        }
        if (!IsTargetAlive(&livenessError)) {
            result.error = livenessError == ERROR_PROCESS_ABORTED
                ? ProcessContextError::TargetExited
                : ProcessContextError::TargetLivenessCheckFailed;
            result.win32Error = livenessError;
            Reset();
            return result;
        }
    }

    HANDLE currentToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &currentToken)) {
        result.error = ProcessContextError::CurrentTokenOpenFailed;
        result.win32Error = GetLastError();
        Reset();
        return result;
    }
    const win32::UniqueHandle scopedCurrentToken(currentToken);
    const IntegrityLevelResult currentIntegrity =
        QueryTokenIntegrityLevel(scopedCurrentToken.Get());
    if (!currentIntegrity.succeeded) {
        result.error = ProcessContextError::CurrentIntegrityQueryFailed;
        result.win32Error = currentIntegrity.win32Error;
        Reset();
        return result;
    }
    HANDLE targetToken = nullptr;
    if (!OpenProcessToken(targetHandle_, TOKEN_QUERY, &targetToken)) {
        result.error = ProcessContextError::TargetTokenOpenFailed;
        result.win32Error = GetLastError();
        Reset();
        return result;
    }
    const win32::UniqueHandle scopedTargetToken(targetToken);
    const IntegrityLevelResult targetIntegrity =
        QueryTokenIntegrityLevel(scopedTargetToken.Get());
    if (!targetIntegrity.succeeded) {
        result.error = ProcessContextError::TargetIntegrityQueryFailed;
        result.win32Error = targetIntegrity.win32Error;
        Reset();
        return result;
    }
    if (!IsTargetIntegrityCompatible(
            currentIntegrity.integrityRid,
            targetIntegrity.integrityRid)) {
        result.error = ProcessContextError::TargetIntegrityHigher;
        result.win32Error = ERROR_ACCESS_DENIED;
        Reset();
        return result;
    }

    if (!IsTargetAlive(&livenessError)) {
        result.error = livenessError == ERROR_PROCESS_ABORTED
            ? ProcessContextError::TargetExited
            : ProcessContextError::TargetLivenessCheckFailed;
        result.win32Error = livenessError;
        Reset();
        return result;
    }

    return result;
}

void TargetProcessContext::Reset() noexcept {
    if (targetHandle_ != nullptr) {
        CloseHandle(targetHandle_);
    }
    targetHandle_ = nullptr;
    targetPid_ = 0;
}

bool TargetProcessContext::IsValid() const noexcept {
    return targetHandle_ != nullptr && targetPid_ != 0;
}

bool TargetProcessContext::IsTargetAlive(DWORD* win32Error) const noexcept {
    if (win32Error != nullptr) {
        *win32Error = ERROR_SUCCESS;
    }
    if (!IsValid()) {
        if (win32Error != nullptr) {
            *win32Error = ERROR_INVALID_HANDLE;
        }
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(targetHandle_, 0);
    if (waitResult == WAIT_TIMEOUT) {
        return true;
    }
    if (win32Error != nullptr) {
        if (waitResult == WAIT_OBJECT_0) {
            *win32Error = ERROR_PROCESS_ABORTED;
        } else if (waitResult == WAIT_FAILED) {
            *win32Error = GetLastError();
        } else {
            *win32Error = ERROR_GEN_FAILURE;
        }
    }
    return false;
}

bool TargetProcessContext::IsTargetForeground() const noexcept {
    if (!IsTargetAlive() || !IsProcessForeground(targetPid_)) {
        return false;
    }
    return IsTargetAlive();
}

bool TargetProcessContext::IsTargetPointerTarget(
    ScreenPoint screenPoint) const noexcept {
    if (!IsTargetAlive() ||
        !IsProcessPointerTarget(targetPid_, screenPoint)) {
        return false;
    }
    return IsTargetAlive();
}

bool TargetProcessContext::IsTargetPointerTargetAtCursor() const noexcept {
    POINT cursor{};
    return GetPhysicalCursorPos(&cursor) && IsTargetPointerTarget({
        static_cast<InputCoordinate>(cursor.x),
        static_cast<InputCoordinate>(cursor.y)});
}

WindowsProcessId TargetProcessContext::TargetPid() const noexcept {
    return targetPid_;
}

HANDLE TargetProcessContext::TargetHandle() const noexcept {
    return targetHandle_;
}

}  // namespace inputweaver
