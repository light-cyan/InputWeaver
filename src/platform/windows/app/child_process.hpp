#pragma once

#include "platform/windows/support/unique_handle.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace inputweaver::win32 {

struct ChildProcess final {
    UniqueHandle process;
    UniqueHandle standardOutput;
    UniqueHandle standardError;
    std::uint32_t processId{};
};

struct CapturedProcessResult final {
    bool started{};
    std::uint32_t exitCode{};
    std::string standardOutput;
    std::string standardError;
    std::string error;
};

[[nodiscard]] bool StartChildProcess(
    const std::filesystem::path& executable,
    std::span<const std::wstring> arguments,
    std::uint32_t creationFlags,
    ChildProcess& process,
    std::string& error);

[[nodiscard]] CapturedProcessResult RunChildProcess(
    const std::filesystem::path& executable,
    std::span<const std::wstring> arguments,
    std::uint32_t creationFlags);

void ReadChildPipe(HANDLE handle, std::string& output) noexcept;

} // namespace inputweaver::win32
