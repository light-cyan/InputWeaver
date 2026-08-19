#include "process_locator.hpp"

#include <tlhelp32.h>

#include <algorithm>
#include <limits>
#include <new>

namespace ukr::win32 {
namespace {

constexpr DWORD kProcessAccess =
    PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE;
constexpr DWORD kInitialPathCapacity = 512;
constexpr DWORD kMaximumPathCapacity = 32768;

class ScopedHandle final {
public:
    explicit ScopedHandle(HANDLE handle = nullptr) noexcept : handle_(handle) {}

    ~ScopedHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
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

enum class SelectorKind : std::uint8_t {
    Basename,
    FullPath
};

struct ParsedSelector {
    SelectorKind kind{SelectorKind::Basename};
    std::wstring value;
    std::wstring basename;
};

struct ParseSelectorResult {
    ParsedSelector selector;
    DWORD win32Error{ERROR_SUCCESS};
    bool succeeded{false};
};

struct QueryPathResult {
    std::wstring path;
    DWORD win32Error{ERROR_SUCCESS};
    bool succeeded{false};
};

[[nodiscard]] bool EqualOrdinalIgnoreCase(
    std::wstring_view left,
    std::wstring_view right) noexcept {
    if (left.size() != right.size() ||
        left.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return false;
    }
    return CompareStringOrdinal(
               left.data(),
               static_cast<int>(left.size()),
               right.data(),
               static_cast<int>(right.size()),
               TRUE) == CSTR_EQUAL;
}

[[nodiscard]] bool IsSeparator(wchar_t character) noexcept {
    return character == L'\\' || character == L'/';
}

[[nodiscard]] bool IsAsciiDriveLetter(wchar_t character) noexcept {
    return (character >= L'A' && character <= L'Z') ||
           (character >= L'a' && character <= L'z');
}

[[nodiscard]] std::wstring_view BasenameOf(
    std::wstring_view path) noexcept {
    const std::size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring_view::npos
        ? path
        : path.substr(separator + 1U);
}

[[nodiscard]] bool IsAbsolutePath(std::wstring_view path) noexcept {
    if (path.size() >= 3U &&
        IsAsciiDriveLetter(path[0]) &&
        path[1] == L':' && IsSeparator(path[2])) {
        return true;
    }
    return path.size() >= 2U && IsSeparator(path[0]) &&
           IsSeparator(path[1]);
}

void RemoveExtendedPathPrefix(std::wstring& path) {
    constexpr std::wstring_view uncPrefix = L"\\\\?\\UNC\\";
    constexpr std::wstring_view localPrefix = L"\\\\?\\";
    if (path.size() >= uncPrefix.size() && EqualOrdinalIgnoreCase(
            std::wstring_view(path).substr(0, uncPrefix.size()), uncPrefix)) {
        path.replace(0, uncPrefix.size(), L"\\\\");
    } else if (path.size() >= localPrefix.size() && EqualOrdinalIgnoreCase(
                   std::wstring_view(path).substr(0, localPrefix.size()),
                   localPrefix)) {
        path.erase(0, localPrefix.size());
    }
}

[[nodiscard]] QueryPathResult NormalizeAbsolutePath(
    std::wstring_view path) {
    QueryPathResult result{};
    if (path.empty() || !IsAbsolutePath(path)) {
        result.win32Error = ERROR_BAD_PATHNAME;
        return result;
    }

    std::wstring source(path);
    std::replace(source.begin(), source.end(), L'/', L'\\');
    const DWORD required = GetFullPathNameW(source.c_str(), 0, nullptr, nullptr);
    if (required == 0) {
        result.win32Error = GetLastError();
        if (result.win32Error == ERROR_SUCCESS) {
            result.win32Error = ERROR_BAD_PATHNAME;
        }
        return result;
    }

    std::wstring normalized(static_cast<std::size_t>(required), L'\0');
    const DWORD length = GetFullPathNameW(
        source.c_str(), required, normalized.data(), nullptr);
    if (length == 0 || length >= required) {
        result.win32Error = length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        if (result.win32Error == ERROR_SUCCESS) {
            result.win32Error = ERROR_BAD_PATHNAME;
        }
        return result;
    }

    normalized.resize(static_cast<std::size_t>(length));
    RemoveExtendedPathPrefix(normalized);
    result.path = std::move(normalized);
    result.succeeded = true;
    return result;
}

[[nodiscard]] ParseSelectorResult ParseSelector(
    std::wstring_view selector) {
    ParseSelectorResult result{};
    if (selector.empty() ||
        selector.find(L'\0') != std::wstring_view::npos) {
        result.win32Error = ERROR_INVALID_PARAMETER;
        return result;
    }

    const bool containsSeparator =
        selector.find_first_of(L"\\/") != std::wstring_view::npos;
    const bool containsDriveMarker = selector.find(L':') != std::wstring_view::npos;
    if (!containsSeparator && !containsDriveMarker) {
        if (selector == L"." || selector == L"..") {
            result.win32Error = ERROR_INVALID_NAME;
            return result;
        }
        result.selector.kind = SelectorKind::Basename;
        result.selector.value.assign(selector);
        result.selector.basename.assign(selector);
        result.succeeded = true;
        return result;
    }

    const QueryPathResult normalized = NormalizeAbsolutePath(selector);
    if (!normalized.succeeded) {
        result.win32Error = normalized.win32Error;
        return result;
    }
    const std::wstring_view basename = BasenameOf(normalized.path);
    if (basename.empty()) {
        result.win32Error = ERROR_INVALID_NAME;
        return result;
    }

    result.selector.kind = SelectorKind::FullPath;
    result.selector.value = normalized.path;
    result.selector.basename.assign(basename);
    result.succeeded = true;
    return result;
}

[[nodiscard]] QueryPathResult QueryImagePath(HANDLE process) {
    QueryPathResult result{};
    for (DWORD capacity = kInitialPathCapacity;
         capacity <= kMaximumPathCapacity;
         capacity *= 2U) {
        std::wstring buffer(static_cast<std::size_t>(capacity), L'\0');
        DWORD length = capacity;
        if (QueryFullProcessImageNameW(
                process, 0, buffer.data(), &length) != FALSE) {
            buffer.resize(static_cast<std::size_t>(length));
            return NormalizeAbsolutePath(buffer);
        }
        const DWORD error = GetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER ||
            capacity == kMaximumPathCapacity) {
            result.win32Error = error == ERROR_SUCCESS
                ? ERROR_GEN_FAILURE
                : error;
            return result;
        }
    }
    result.win32Error = ERROR_INSUFFICIENT_BUFFER;
    return result;
}

[[nodiscard]] bool MatchesParsedSelector(
    std::wstring_view imagePath,
    const ParsedSelector& selector) noexcept {
    if (selector.kind == SelectorKind::FullPath) {
        return EqualOrdinalIgnoreCase(imagePath, selector.value);
    }
    return EqualOrdinalIgnoreCase(BasenameOf(imagePath), selector.value);
}

enum class Liveness : std::uint8_t {
    Alive,
    Exited,
    Error
};

[[nodiscard]] Liveness CheckLiveness(
    HANDLE process,
    DWORD* win32Error) noexcept {
    if (win32Error != nullptr) {
        *win32Error = ERROR_SUCCESS;
    }
    const DWORD waitResult = WaitForSingleObject(process, 0);
    if (waitResult == WAIT_TIMEOUT) {
        return Liveness::Alive;
    }
    if (waitResult == WAIT_OBJECT_0) {
        if (win32Error != nullptr) {
            *win32Error = ERROR_PROCESS_ABORTED;
        }
        return Liveness::Exited;
    }
    if (win32Error != nullptr) {
        *win32Error = waitResult == WAIT_FAILED
            ? GetLastError()
            : ERROR_GEN_FAILURE;
    }
    return Liveness::Error;
}

[[nodiscard]] LocateResult ErrorResult(DWORD win32Error) noexcept {
    LocateResult result{};
    result.status = LocateStatus::Error;
    result.win32Error = win32Error == ERROR_SUCCESS
        ? ERROR_GEN_FAILURE
        : win32Error;
    return result;
}

[[nodiscard]] LocateResult LocateExecutableImpl(
    std::wstring_view selectorText) {
    const ParseSelectorResult parsed = ParseSelector(selectorText);
    if (!parsed.succeeded) {
        return ErrorResult(parsed.win32Error);
    }

    const ScopedHandle snapshot(
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.Get() == INVALID_HANDLE_VALUE) {
        return ErrorResult(GetLastError());
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.Get(), &entry) == FALSE) {
        return ErrorResult(GetLastError());
    }

    LocateResult result{};
    DWORD firstRelevantError = ERROR_SUCCESS;
    for (;;) {
        if (entry.th32ProcessID != 0 && EqualOrdinalIgnoreCase(
                entry.szExeFile, parsed.selector.basename)) {
            const ScopedHandle process(OpenProcess(
                kProcessAccess, FALSE, entry.th32ProcessID));
            if (process.Get() == nullptr) {
                const DWORD error = GetLastError();
                if (error != ERROR_INVALID_PARAMETER &&
                    firstRelevantError == ERROR_SUCCESS) {
                    firstRelevantError = error;
                }
            } else {
                DWORD livenessError = ERROR_SUCCESS;
                const Liveness before =
                    CheckLiveness(process.Get(), &livenessError);
                if (before == Liveness::Error &&
                    firstRelevantError == ERROR_SUCCESS) {
                    firstRelevantError = livenessError;
                } else if (before == Liveness::Alive) {
                    const QueryPathResult queried = QueryImagePath(process.Get());
                    if (!queried.succeeded) {
                        const Liveness afterFailure =
                            CheckLiveness(process.Get(), &livenessError);
                        if (afterFailure == Liveness::Error &&
                            firstRelevantError == ERROR_SUCCESS) {
                            firstRelevantError = livenessError;
                        } else if (afterFailure == Liveness::Alive &&
                                   firstRelevantError == ERROR_SUCCESS) {
                            firstRelevantError = queried.win32Error;
                        }
                    } else {
                        const Liveness after =
                            CheckLiveness(process.Get(), &livenessError);
                        if (after == Liveness::Error &&
                            firstRelevantError == ERROR_SUCCESS) {
                            firstRelevantError = livenessError;
                        } else if (after == Liveness::Alive &&
                                   MatchesParsedSelector(
                                       queried.path, parsed.selector)) {
                            result.matches.push_back(
                                {entry.th32ProcessID, queried.path});
                        }
                    }
                }
            }
        }

        if (Process32NextW(snapshot.Get(), &entry) == FALSE) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                return ErrorResult(error);
            }
            break;
        }
    }

    if (firstRelevantError != ERROR_SUCCESS) {
        return ErrorResult(firstRelevantError);
    }

    std::sort(
        result.matches.begin(),
        result.matches.end(),
        [](const LocatedProcess& left, const LocatedProcess& right) noexcept {
            return left.processId < right.processId;
        });
    result.status = result.matches.empty()
        ? LocateStatus::None
        : result.matches.size() == 1U
            ? LocateStatus::One
            : LocateStatus::Ambiguous;
    return result;
}

}  // namespace

const char* LocateStatusName(LocateStatus status) noexcept {
    switch (status) {
        case LocateStatus::None:
            return "none";
        case LocateStatus::One:
            return "one";
        case LocateStatus::Ambiguous:
            return "ambiguous";
        case LocateStatus::Error:
            return "error";
    }
    return "unknown";
}

LocateResult LocateExecutable(std::wstring_view selector) noexcept {
    try {
        return LocateExecutableImpl(selector);
    } catch (const std::bad_alloc&) {
        return ErrorResult(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        return ErrorResult(ERROR_GEN_FAILURE);
    }
}

}  // namespace ukr::win32
