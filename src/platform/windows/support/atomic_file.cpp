#include "atomic_file.hpp"

#include "unique_handle.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <system_error>

namespace inputweaver::win32 {

std::filesystem::path MakeSiblingTemporaryPath(
    const std::filesystem::path& destination)
{
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

bool ReplaceFileAtomically(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::string& error)
{
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

bool WriteFileAtomically(
    const std::filesystem::path& destination,
    std::string_view bytes,
    std::string& error)
{
    const std::filesystem::path temporary = MakeSiblingTemporaryPath(
        destination);
    UniqueHandle file(CreateFileW(
        temporary.c_str(),
        GENERIC_WRITE,
        0U,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (!file) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const DWORD requested = static_cast<DWORD>((std::min)(
            remaining,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written{};
        if (WriteFile(
                file.Get(),
                bytes.data() + offset,
                requested,
                &written,
                nullptr) == FALSE
            || written == 0U) {
            error = std::system_category().message(
                static_cast<int>(GetLastError()));
            file.Reset();
            (void)DeleteFileW(temporary.c_str());
            return false;
        }
        offset += written;
    }
    if (FlushFileBuffers(file.Get()) == FALSE) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        file.Reset();
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    file.Reset();
    if (!ReplaceFileAtomically(temporary, destination, error)) {
        (void)DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace inputweaver::win32
