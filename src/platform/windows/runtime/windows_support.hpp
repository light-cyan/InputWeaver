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

class UniqueHandle final {
public:
    explicit UniqueHandle(HANDLE handle) noexcept
        : handle_(handle)
    {
    }

    ~UniqueHandle()
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] HANDLE Get() const noexcept
    {
        return handle_;
    }

private:
    HANDLE handle_;
};

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
