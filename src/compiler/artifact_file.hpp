#pragma once

#include <filesystem>
#include <string>

namespace inputweaver::compiler::artifact_file {

[[nodiscard]] std::filesystem::path MakeSiblingTemporaryPath(
    const std::filesystem::path& destination);

[[nodiscard]] bool ReplaceDestination(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::string& error);

}  // namespace inputweaver::compiler::artifact_file
