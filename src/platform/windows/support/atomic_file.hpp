#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace inputweaver::win32 {

[[nodiscard]] std::filesystem::path MakeSiblingTemporaryPath(
    const std::filesystem::path& destination);

[[nodiscard]] bool ReplaceFileAtomically(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::string& error);

[[nodiscard]] bool WriteFileAtomically(
    const std::filesystem::path& destination,
    std::string_view bytes,
    std::string& error);

} // namespace inputweaver::win32
