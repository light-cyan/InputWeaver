#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "windows_input_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace inputweaver::win32 {

enum class LocateStatus : std::uint8_t {
    None,
    One,
    Ambiguous,
    Error
};

struct LocatedProcess {
    WindowsProcessId processId{0};
    std::wstring imagePath;
};

struct LocateResult {
    LocateStatus status{LocateStatus::None};
    DWORD win32Error{ERROR_SUCCESS};
    std::vector<LocatedProcess> matches;

    [[nodiscard]] const LocatedProcess* UniqueMatch() const noexcept {
        return status == LocateStatus::One && matches.size() == 1U
            ? &matches.front()
            : nullptr;
    }
};

// A selector is either a bare executable basename or an absolute executable path.
[[nodiscard]] LocateResult LocateExecutable(
    std::wstring_view selector) noexcept;

}  // namespace inputweaver::win32
