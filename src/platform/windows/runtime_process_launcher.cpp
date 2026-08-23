#include "runtime_process_launcher.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace inputweaver::win32 {
namespace {

constexpr std::size_t kMaximumCreateProcessCharacters = 32767U;

[[nodiscard]] bool IsCommandWhitespace(wchar_t character) noexcept
{
    return character == L' ' || character == L'\t';
}

[[nodiscard]] bool ConvertUtf8(
    std::string_view source,
    std::wstring& destination,
    DWORD& error) noexcept
{
    if (source.empty()
        || source.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())
        || source.find('\0') != std::string_view::npos) {
        error = ERROR_INVALID_DATA;
        return false;
    }
    const int sourceLength = static_cast<int>(source.size());
    SetLastError(ERROR_SUCCESS);
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        source.data(),
        sourceLength,
        nullptr,
        0);
    if (required <= 0) {
        error = GetLastError();
        return false;
    }
    try {
        destination.resize(static_cast<std::size_t>(required));
    } catch (...) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        source.data(),
        sourceLength,
        destination.data(),
        required);
    if (converted != required) {
        error = GetLastError();
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

[[nodiscard]] ExecutableResolutionError ExtractToken(
    std::wstring_view command,
    std::wstring& token) noexcept
{
    std::size_t begin = 0U;
    while (begin < command.size() && IsCommandWhitespace(command[begin])) {
        ++begin;
    }
    if (begin == command.size()) {
        return ExecutableResolutionError::EmptyCommand;
    }
    try {
        if (command[begin] == L'"') {
            const std::size_t end = command.find(L'"', begin + 1U);
            if (end == std::wstring_view::npos) {
                return ExecutableResolutionError::UnterminatedQuote;
            }
            if (end == begin + 1U) {
                return ExecutableResolutionError::InvalidToken;
            }
            if (end + 1U < command.size()
                && !IsCommandWhitespace(command[end + 1U])) {
                return ExecutableResolutionError::InvalidTokenBoundary;
            }
            token.assign(command.substr(begin + 1U, end - begin - 1U));
        } else {
            std::size_t end = begin;
            while (end < command.size() && !IsCommandWhitespace(command[end])) {
                if (command[end] == L'"') {
                    return ExecutableResolutionError::InvalidTokenBoundary;
                }
                ++end;
            }
            token.assign(command.substr(begin, end - begin));
        }
    } catch (...) {
        return ExecutableResolutionError::EnvironmentQueryFailed;
    }
    return token.empty()
        ? ExecutableResolutionError::InvalidToken
        : ExecutableResolutionError::None;
}

[[nodiscard]] bool HasDirectoryComponent(std::wstring_view token) noexcept
{
    return token.find_first_of(L"\\/") != std::wstring_view::npos
        || token.find(L':') != std::wstring_view::npos;
}

[[nodiscard]] bool HasExtension(std::wstring_view token) noexcept
{
    const std::size_t separator = token.find_last_of(L"\\/");
    const std::size_t dot = token.find_last_of(L'.');
    return dot != std::wstring_view::npos
        && (separator == std::wstring_view::npos || dot > separator)
        && dot + 1U < token.size();
}

[[nodiscard]] bool QueryModulePath(std::wstring& path, DWORD& error) noexcept
{
    std::vector<wchar_t> buffer(512U);
    while (buffer.size() <= kMaximumCreateProcessCharacters) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0U) {
            error = GetLastError();
            return false;
        }
        if (length < buffer.size() - 1U
            || (length < buffer.size() && GetLastError() != ERROR_INSUFFICIENT_BUFFER)) {
            path.assign(buffer.data(), length);
            error = ERROR_SUCCESS;
            return true;
        }
        buffer.resize(buffer.size() * 2U);
    }
    error = ERROR_FILENAME_EXCED_RANGE;
    return false;
}

template <typename QueryFunction>
[[nodiscard]] bool QueryDirectory(
    QueryFunction query,
    std::wstring& path,
    DWORD& error) noexcept
{
    std::vector<wchar_t> buffer(512U);
    while (buffer.size() <= kMaximumCreateProcessCharacters) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = query(
            static_cast<DWORD>(buffer.size()),
            buffer.data());
        if (length == 0U) {
            error = GetLastError();
            return false;
        }
        if (length < buffer.size()) {
            path.assign(buffer.data(), length);
            error = ERROR_SUCCESS;
            return true;
        }
        buffer.resize(static_cast<std::size_t>(length) + 1U);
    }
    error = ERROR_FILENAME_EXCED_RANGE;
    return false;
}

[[nodiscard]] bool QueryEnvironmentPath(
    std::wstring& path,
    DWORD& error) noexcept
{
    SetLastError(ERROR_SUCCESS);
    const DWORD required = GetEnvironmentVariableW(L"PATH", nullptr, 0U);
    if (required == 0U) {
        const DWORD queryError = GetLastError();
        if (queryError == ERROR_ENVVAR_NOT_FOUND || queryError == ERROR_SUCCESS) {
            path.clear();
            error = ERROR_SUCCESS;
            return true;
        }
        error = queryError;
        return false;
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(required));
    const DWORD length = GetEnvironmentVariableW(
        L"PATH",
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size()) {
        error = GetLastError();
        return false;
    }
    path.assign(buffer.data(), length);
    error = ERROR_SUCCESS;
    return true;
}

[[nodiscard]] std::wstring ParentDirectory(std::wstring_view path)
{
    const std::size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring_view::npos) {
        return {};
    }
    if (separator == 2U && path.size() >= 3U && path[1] == L':') {
        return std::wstring(path.substr(0U, 3U));
    }
    return std::wstring(path.substr(0U, separator));
}

[[nodiscard]] bool FullPath(
    std::wstring_view path,
    std::wstring& resolved,
    DWORD& error) noexcept
{
    std::vector<wchar_t> buffer(512U);
    const std::wstring terminated(path);
    while (buffer.size() <= kMaximumCreateProcessCharacters) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetFullPathNameW(
            terminated.c_str(),
            static_cast<DWORD>(buffer.size()),
            buffer.data(),
            nullptr);
        if (length == 0U) {
            error = GetLastError();
            return false;
        }
        if (length < buffer.size()) {
            resolved.assign(buffer.data(), length);
            error = ERROR_SUCCESS;
            return true;
        }
        buffer.resize(static_cast<std::size_t>(length) + 1U);
    }
    error = ERROR_FILENAME_EXCED_RANGE;
    return false;
}

[[nodiscard]] bool IsRegularExecutableCandidate(
    const std::wstring& path) noexcept
{
    SetLastError(ERROR_SUCCESS);
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
}

[[nodiscard]] bool TryCandidate(
    std::wstring_view directory,
    std::wstring_view fileName,
    std::wstring& resolved,
    DWORD& error) noexcept
{
    if (directory.empty()) {
        return false;
    }
    std::wstring candidate(directory);
    if (candidate.back() != L'\\' && candidate.back() != L'/') {
        candidate.push_back(L'\\');
    }
    candidate.append(fileName);
    std::wstring full;
    if (!FullPath(candidate, full, error) || !IsRegularExecutableCandidate(full)) {
        return false;
    }
    resolved = std::move(full);
    return true;
}

[[nodiscard]] bool ResolveBareExecutable(
    std::wstring_view token,
    std::wstring& resolved,
    DWORD& error) noexcept
{
    std::wstring fileName(token);
    if (!HasExtension(token) && token.back() != L'.') {
        fileName += L".exe";
    }

    std::wstring modulePath;
    std::wstring currentDirectory;
    std::wstring systemDirectory;
    std::wstring windowsDirectory;
    std::wstring environmentPath;
    if (!QueryModulePath(modulePath, error)
        || !QueryDirectory(
            [](DWORD size, wchar_t* buffer) noexcept {
                return GetCurrentDirectoryW(size, buffer);
            },
            currentDirectory,
            error)
        || !QueryDirectory(
            [](DWORD size, wchar_t* buffer) noexcept {
                return GetSystemDirectoryW(buffer, size);
            },
            systemDirectory,
            error)
        || !QueryDirectory(
            [](DWORD size, wchar_t* buffer) noexcept {
                return GetWindowsDirectoryW(buffer, size);
            },
            windowsDirectory,
            error)
        || !QueryEnvironmentPath(environmentPath, error)) {
        return false;
    }

    const std::wstring applicationDirectory = ParentDirectory(modulePath);
    std::wstring legacySystemDirectory = windowsDirectory;
    if (!legacySystemDirectory.empty()
        && legacySystemDirectory.back() != L'\\') {
        legacySystemDirectory.push_back(L'\\');
    }
    legacySystemDirectory += L"System";
    const std::wstring_view fixedDirectories[] = {
        applicationDirectory,
        currentDirectory,
        systemDirectory,
        legacySystemDirectory,
        windowsDirectory,
    };
    for (const std::wstring_view directory : fixedDirectories) {
        if (TryCandidate(directory, fileName, resolved, error)) {
            return true;
        }
    }

    std::size_t begin = 0U;
    while (begin <= environmentPath.size()) {
        const std::size_t separator = environmentPath.find(L';', begin);
        const std::size_t end = separator == std::wstring::npos
            ? environmentPath.size()
            : separator;
        std::wstring_view directory(environmentPath.data() + begin, end - begin);
        while (!directory.empty() && IsCommandWhitespace(directory.front())) {
            directory.remove_prefix(1U);
        }
        while (!directory.empty() && IsCommandWhitespace(directory.back())) {
            directory.remove_suffix(1U);
        }
        if (directory.size() >= 2U
            && directory.front() == L'"'
            && directory.back() == L'"') {
            directory.remove_prefix(1U);
            directory.remove_suffix(1U);
        }
        if (TryCandidate(directory, fileName, resolved, error)) {
            return true;
        }
        if (separator == std::wstring::npos) {
            break;
        }
        begin = separator + 1U;
    }
    error = ERROR_FILE_NOT_FOUND;
    return false;
}

} // namespace

ExecutableResolutionResult ResolveExecutableCommandUnchecked(
    std::string_view authoredCommand)
{
    ExecutableResolutionResult result{};
    if (!ConvertUtf8(authoredCommand, result.commandLine, result.win32Error)) {
        result.error = ExecutableResolutionError::InvalidUtf8;
        return result;
    }
    if (result.commandLine.size() + 1U > kMaximumCreateProcessCharacters) {
        result.error = ExecutableResolutionError::CommandTooLong;
        result.win32Error = ERROR_FILENAME_EXCED_RANGE;
        return result;
    }
    result.error = ExtractToken(result.commandLine, result.executableToken);
    if (result.error != ExecutableResolutionError::None) {
        result.win32Error = ERROR_INVALID_DATA;
        return result;
    }

    if (HasDirectoryComponent(result.executableToken)) {
        if (!FullPath(
                result.executableToken,
                result.executablePath,
                result.win32Error)
            || !IsRegularExecutableCandidate(result.executablePath)) {
            result.error = ExecutableResolutionError::ExecutableNotFound;
            if (result.win32Error == ERROR_SUCCESS) {
                result.win32Error = ERROR_FILE_NOT_FOUND;
            }
            return result;
        }
    } else if (!ResolveBareExecutable(
                   result.executableToken,
                   result.executablePath,
                   result.win32Error)) {
        result.error = result.win32Error == ERROR_FILE_NOT_FOUND
            ? ExecutableResolutionError::ExecutableNotFound
            : ExecutableResolutionError::EnvironmentQueryFailed;
        return result;
    }

    result.workingDirectory = ParentDirectory(result.executablePath);
    if (result.workingDirectory.empty()) {
        result.error = ExecutableResolutionError::WorkingDirectoryUnavailable;
        result.win32Error = ERROR_PATH_NOT_FOUND;
        return result;
    }
    result.error = ExecutableResolutionError::None;
    result.win32Error = ERROR_SUCCESS;
    return result;
}

ExecutableResolutionResult ResolveExecutableCommand(
    std::string_view authoredCommand) noexcept
{
    try {
        return ResolveExecutableCommandUnchecked(authoredCommand);
    } catch (...) {
        ExecutableResolutionResult result{};
        result.error = ExecutableResolutionError::EnvironmentQueryFailed;
        result.win32Error = ERROR_NOT_ENOUGH_MEMORY;
        return result;
    }
}

WindowsProcessLauncher::WindowsProcessLauncher(
    bool permitted,
    CreateProcessWFunction createProcess) noexcept
    : permitted_(permitted), createProcess_(createProcess)
{
}

bool WindowsProcessLauncher::Permitted() const noexcept
{
    return permitted_;
}

RuntimeLaunchResult WindowsProcessLauncher::Launch(
    std::string_view command,
    RuntimeCancellationProbe cancellation) noexcept
{
    lastResolutionError_ = ExecutableResolutionError::None;
    lastWin32Error_ = ERROR_SUCCESS;
    if (!permitted_ || createProcess_ == nullptr) {
        lastWin32Error_ = ERROR_ACCESS_DISABLED_BY_POLICY;
        return RuntimeLaunchResult::CreationFailed;
    }
    if (cancellation.Cancelled()) {
        return RuntimeLaunchResult::Cancelled;
    }
    ExecutableResolutionResult resolved = ResolveExecutableCommand(command);
    lastResolutionError_ = resolved.error;
    lastWin32Error_ = resolved.win32Error;
    if (!resolved.Succeeded()) {
        return resolved.error == ExecutableResolutionError::InvalidUtf8
                || resolved.error == ExecutableResolutionError::EmptyCommand
                || resolved.error == ExecutableResolutionError::UnterminatedQuote
                || resolved.error == ExecutableResolutionError::InvalidTokenBoundary
                || resolved.error == ExecutableResolutionError::InvalidToken
                || resolved.error == ExecutableResolutionError::CommandTooLong
            ? RuntimeLaunchResult::InvalidCommand
            : RuntimeLaunchResult::ResolutionFailed;
    }

    std::vector<wchar_t> mutableCommand;
    try {
        mutableCommand.assign(
            resolved.commandLine.begin(),
            resolved.commandLine.end());
        mutableCommand.push_back(L'\0');
    } catch (...) {
        lastWin32Error_ = ERROR_NOT_ENOUGH_MEMORY;
        return RuntimeLaunchResult::CreationFailed;
    }
    if (cancellation.Cancelled()) {
        return RuntimeLaunchResult::Cancelled;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    SetLastError(ERROR_SUCCESS);
    const BOOL created = createProcess_(
        resolved.executablePath.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        0U,
        nullptr,
        resolved.workingDirectory.c_str(),
        &startup,
        &process);
    if (created == FALSE) {
        lastWin32Error_ = GetLastError();
        return RuntimeLaunchResult::CreationFailed;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    lastWin32Error_ = ERROR_SUCCESS;
    return RuntimeLaunchResult::Launched;
}

ExecutableResolutionError WindowsProcessLauncher::LastResolutionError() const noexcept
{
    return lastResolutionError_;
}

DWORD WindowsProcessLauncher::LastWin32Error() const noexcept
{
    return lastWin32Error_;
}

} // namespace inputweaver::win32
