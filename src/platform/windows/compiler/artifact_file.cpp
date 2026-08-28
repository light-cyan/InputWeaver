#include "compiler/artifact_file.hpp"

#include "platform/windows/support/atomic_file.hpp"

namespace inputweaver::compiler::artifact_file {

std::filesystem::path MakeSiblingTemporaryPath(
    const std::filesystem::path& destination) {
    return inputweaver::win32::MakeSiblingTemporaryPath(destination);
}

bool ReplaceDestination(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::string& error) {
    return inputweaver::win32::ReplaceFileAtomically(
        temporary,
        destination,
        error);
}

}  // namespace inputweaver::compiler::artifact_file
