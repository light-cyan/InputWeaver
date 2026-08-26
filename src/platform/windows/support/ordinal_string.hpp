#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <limits>
#include <string_view>

namespace inputweaver::win32 {

[[nodiscard]] inline bool EqualOrdinalIgnoreCase(
    std::wstring_view left,
    std::wstring_view right) noexcept
{
    if (left.size() != right.size()
        || left.size() > static_cast<std::size_t>(
            (std::numeric_limits<int>::max)())) {
        return false;
    }
    return CompareStringOrdinal(
               left.data(),
               static_cast<int>(left.size()),
               right.data(),
               static_cast<int>(right.size()),
               TRUE) == CSTR_EQUAL;
}

} // namespace inputweaver::win32
