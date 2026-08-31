#include "child_process.hpp"

#include "platform/windows/support/command_line.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace inputweaver::win32 {
namespace {

inline constexpr std::size_t kMaximumCapturedOutputBytes = 4U * 1024U * 1024U;
inline constexpr std::string_view kOutputTruncated =
    "\n[child output truncated]\n";

struct PipePair final {
    UniqueHandle read;
    UniqueHandle write;
};

[[nodiscard]] bool CreateChildPipe(PipePair& pipe, std::string& error)
{
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read{};
    HANDLE write{};
    if (CreatePipe(&read, &write, &security, 0U) == FALSE) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    pipe.read.Reset(read);
    pipe.write.Reset(write);
    if (SetHandleInformation(pipe.read.Get(), HANDLE_FLAG_INHERIT, 0U) == FALSE) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    return true;
}

} // namespace

bool StartChildProcess(
    const std::filesystem::path& executable,
    std::span<const std::wstring> arguments,
    std::uint32_t creationFlags,
    ChildProcess& process,
    std::string& error)
{
    PipePair standardOutput;
    PipePair standardError;
    if (!CreateChildPipe(standardOutput, error)
        || !CreateChildPipe(standardError, error)) {
        return false;
    }
    std::vector<std::wstring> commandArguments;
    commandArguments.reserve(arguments.size() + 1U);
    commandArguments.push_back(executable.wstring());
    commandArguments.insert(
        commandArguments.end(),
        arguments.begin(),
        arguments.end());
    std::wstring commandLine = BuildCommandLine(commandArguments);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = standardOutput.write.Get();
    startup.hStdError = standardError.write.Get();
    PROCESS_INFORMATION information{};
    if (CreateProcessW(
            executable.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            creationFlags | CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            executable.parent_path().c_str(),
            &startup,
            &information) == FALSE) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    UniqueHandle thread(information.hThread);
    process.process.Reset(information.hProcess);
    process.processId = information.dwProcessId;
    standardOutput.write.Reset();
    standardError.write.Reset();
    process.standardOutput = std::move(standardOutput.read);
    process.standardError = std::move(standardError.read);
    return true;
}

CapturedProcessResult RunChildProcess(
    const std::filesystem::path& executable,
    std::span<const std::wstring> arguments,
    std::uint32_t creationFlags)
{
    CapturedProcessResult result{};
    ChildProcess process;
    if (!StartChildProcess(
            executable,
            arguments,
            creationFlags,
            process,
            result.error)) {
        return result;
    }
    result.started = true;
    std::thread outputReader;
    std::thread errorReader;
    try {
        outputReader = std::thread(
            &ReadChildPipe,
            process.standardOutput.Get(),
            std::ref(result.standardOutput));
        errorReader = std::thread(
            &ReadChildPipe,
            process.standardError.Get(),
            std::ref(result.standardError));
    } catch (...) {
        result.error = "Cannot create child-process output readers.";
        result.exitCode = 1U;
        (void)TerminateProcess(process.process.Get(), 1U);
        (void)WaitForSingleObject(process.process.Get(), INFINITE);
        if (outputReader.joinable()) {
            outputReader.join();
        }
        if (errorReader.joinable()) {
            errorReader.join();
        }
        return result;
    }
    const DWORD waited = WaitForSingleObject(process.process.Get(), INFINITE);
    DWORD exitCode{};
    if (waited != WAIT_OBJECT_0
        || GetExitCodeProcess(process.process.Get(), &exitCode) == FALSE) {
        result.error = std::system_category().message(
            static_cast<int>(GetLastError()));
        result.exitCode = 1U;
        (void)TerminateProcess(process.process.Get(), 1U);
        (void)WaitForSingleObject(process.process.Get(), INFINITE);
    } else {
        result.exitCode = exitCode;
    }
    outputReader.join();
    errorReader.join();
    return result;
}

void ReadChildPipe(HANDLE handle, std::string& output) noexcept
{
    try {
        std::array<char, 4'096U> buffer{};
        bool truncated = false;
        for (;;) {
            DWORD read{};
            if (ReadFile(
                    handle,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    &read,
                    nullptr) == FALSE
                || read == 0U) {
                break;
            }
            const std::size_t available = output.size()
                    < kMaximumCapturedOutputBytes
                ? kMaximumCapturedOutputBytes - output.size()
                : 0U;
            const std::size_t accepted = (std::min)(
                available,
                static_cast<std::size_t>(read));
            output.append(buffer.data(), accepted);
            truncated = truncated || accepted != static_cast<std::size_t>(read);
        }
        if (truncated) {
            output.append(kOutputTruncated);
        }
    } catch (...) {
    }
}

} // namespace inputweaver::win32
