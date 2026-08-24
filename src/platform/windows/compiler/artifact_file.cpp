#include "compiler/artifact_file.hpp"

#include <atomic>
#include <cstdint>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace inputweaver::compiler::artifact_file {

std::filesystem::path MakeSiblingTemporaryPath(
    const std::filesystem::path& destination) {
    static std::atomic<std::uint64_t> sequence{0U};
    const std::uint64_t process = GetCurrentProcessId();
    for (;;) {
        const std::uint64_t ordinal = sequence.fetch_add(
            1U,
            std::memory_order_relaxed);
        std::filesystem::path temporary = destination;
        temporary += ".tmp." + std::to_string(process) + "."
            + std::to_string(ordinal);
        std::error_code existsError;
        const bool exists = std::filesystem::exists(temporary, existsError);
        if (!exists || existsError) {
            return temporary;
        }
    }
}

bool ReplaceDestination(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::string& error) {
    if (MoveFileExW(
            temporary.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
        != FALSE) {
        return true;
    }
    error = std::system_category().message(static_cast<int>(GetLastError()));
    return false;
}

}  // namespace inputweaver::compiler::artifact_file
