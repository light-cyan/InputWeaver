#include "process_context.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <utility>

namespace ukr {
namespace {

constexpr DWORD kTargetProcessAccess =
    PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE;
constexpr DWORD kMaximumImagePathCharacters = 32768;

class ScopedHandle final {
public:
    explicit ScopedHandle(HANDLE handle = nullptr) noexcept : handle_(handle) {}

    ~ScopedHandle() {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    [[nodiscard]] HANDLE Get() const noexcept {
        return handle_;
    }

private:
    HANDLE handle_;
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

[[nodiscard]] bool EqualOrdinalIgnoreCase(
    std::wstring_view left,
    std::wstring_view right) noexcept {
    if (left.size() != right.size() ||
        left.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    return CompareStringOrdinal(
               left.data(),
               static_cast<int>(left.size()),
               right.data(),
               static_cast<int>(right.size()),
               TRUE) == CSTR_EQUAL;
}

[[nodiscard]] bool EqualNormalizedImagePath(
    std::wstring_view actual,
    std::wstring_view expected) noexcept {
    constexpr std::wstring_view localPrefix = L"\\\\?\\";
    constexpr std::wstring_view uncPrefix = L"\\\\?\\UNC\\";
    if (actual.size() >= uncPrefix.size() &&
        EqualOrdinalIgnoreCase(actual.substr(0, uncPrefix.size()), uncPrefix)) {
        return expected.size() >= 2U && expected[0] == L'\\' && expected[1] == L'\\' &&
               EqualOrdinalIgnoreCase(actual.substr(uncPrefix.size()), expected.substr(2U));
    }
    if (actual.size() >= localPrefix.size() &&
        EqualOrdinalIgnoreCase(actual.substr(0, localPrefix.size()), localPrefix)) {
        actual.remove_prefix(localPrefix.size());
    }
    return EqualOrdinalIgnoreCase(actual, expected);
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

IntegrityLevelResult QueryProcessIntegrityLevel(HANDLE process) noexcept {
    // GetCurrentProcess returns the valid pseudo handle value -1, which is
    // numerically equal to INVALID_HANDLE_VALUE.
    if (process == nullptr) {
        return {0, ERROR_INVALID_HANDLE, false};
    }

    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        return {0, GetLastError(), false};
    }

    const ScopedHandle scopedToken(token);
    return QueryTokenIntegrityLevel(scopedToken.Get());
}

bool IsTargetIntegrityCompatible(
    DWORD currentIntegrityRid,
    DWORD targetIntegrityRid) noexcept {
    return targetIntegrityRid <= currentIntegrityRid;
}

bool IsProcessForeground(DWORD processId) noexcept {
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

bool IsProcessPointerTarget(
    DWORD processId,
    POINT screenPoint) noexcept {
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
        : WindowFromPoint(screenPoint);
    return WindowBelongsToProcess(routeWindow, processId);
}

TargetProcessContext::~TargetProcessContext() {
    Reset();
}

TargetProcessContext::TargetProcessContext(
    TargetProcessContext&& other) noexcept
    : targetHandle_(std::exchange(other.targetHandle_, nullptr)),
      targetPid_(std::exchange(other.targetPid_, 0)),
      currentIntegrityRid_(std::exchange(other.currentIntegrityRid_, 0)),
      targetIntegrityRid_(std::exchange(other.targetIntegrityRid_, 0)) {}

TargetProcessContext& TargetProcessContext::operator=(
    TargetProcessContext&& other) noexcept {
    if (this != &other) {
        Reset();
        targetHandle_ = std::exchange(other.targetHandle_, nullptr);
        targetPid_ = std::exchange(other.targetPid_, 0);
        currentIntegrityRid_ =
            std::exchange(other.currentIntegrityRid_, 0);
        targetIntegrityRid_ = std::exchange(other.targetIntegrityRid_, 0);
    }
    return *this;
}

ProcessContextResult TargetProcessContext::Initialize(DWORD targetPid) noexcept {
    return Initialize(targetPid, {});
}

ProcessContextResult TargetProcessContext::Initialize(
    DWORD targetPid,
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
    const ScopedHandle scopedCurrentToken(currentToken);
    const IntegrityLevelResult currentIntegrity =
        QueryTokenIntegrityLevel(scopedCurrentToken.Get());
    if (!currentIntegrity.succeeded) {
        result.error = ProcessContextError::CurrentIntegrityQueryFailed;
        result.win32Error = currentIntegrity.win32Error;
        Reset();
        return result;
    }
    result.currentIntegrityRid = currentIntegrity.integrityRid;

    HANDLE targetToken = nullptr;
    if (!OpenProcessToken(targetHandle_, TOKEN_QUERY, &targetToken)) {
        result.error = ProcessContextError::TargetTokenOpenFailed;
        result.win32Error = GetLastError();
        Reset();
        return result;
    }
    const ScopedHandle scopedTargetToken(targetToken);
    const IntegrityLevelResult targetIntegrity =
        QueryTokenIntegrityLevel(scopedTargetToken.Get());
    if (!targetIntegrity.succeeded) {
        result.error = ProcessContextError::TargetIntegrityQueryFailed;
        result.win32Error = targetIntegrity.win32Error;
        Reset();
        return result;
    }
    result.targetIntegrityRid = targetIntegrity.integrityRid;

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

    currentIntegrityRid_ = currentIntegrity.integrityRid;
    targetIntegrityRid_ = targetIntegrity.integrityRid;
    return result;
}

void TargetProcessContext::Reset() noexcept {
    if (targetHandle_ != nullptr) {
        CloseHandle(targetHandle_);
    }
    targetHandle_ = nullptr;
    targetPid_ = 0;
    currentIntegrityRid_ = 0;
    targetIntegrityRid_ = 0;
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
    POINT screenPoint) const noexcept {
    if (!IsTargetAlive() ||
        !IsProcessPointerTarget(targetPid_, screenPoint)) {
        return false;
    }
    return IsTargetAlive();
}

bool TargetProcessContext::IsTargetPointerTargetAtCursor() const noexcept {
    POINT cursor{};
    return GetCursorPos(&cursor) && IsTargetPointerTarget(cursor);
}

DWORD TargetProcessContext::TargetPid() const noexcept {
    return targetPid_;
}

HANDLE TargetProcessContext::TargetHandle() const noexcept {
    return targetHandle_;
}

DWORD TargetProcessContext::CurrentIntegrityRid() const noexcept {
    return currentIntegrityRid_;
}

DWORD TargetProcessContext::TargetIntegrityRid() const noexcept {
    return targetIntegrityRid_;
}

}  // namespace ukr
