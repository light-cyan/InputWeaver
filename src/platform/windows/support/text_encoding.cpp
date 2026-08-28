#include "text_encoding.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <limits>

namespace inputweaver::win32 {
namespace {

template <typename Character>
[[nodiscard]] bool FitsWindowsLength(
    std::basic_string_view<Character> text) noexcept
{
    return text.size()
        <= static_cast<std::size_t>((std::numeric_limits<int>::max)());
}

} // namespace

bool Utf8ToWide(
    std::string_view source,
    std::wstring& destination) noexcept
{
    try {
        destination.clear();
        if (source.empty()) {
            return true;
        }
        if (!FitsWindowsLength(source)) {
            return false;
        }
        const int required = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            source.data(),
            static_cast<int>(source.size()),
            nullptr,
            0);
        if (required <= 0) {
            return false;
        }
        destination.resize(static_cast<std::size_t>(required));
        return MultiByteToWideChar(
                   CP_UTF8,
                   MB_ERR_INVALID_CHARS,
                   source.data(),
                   static_cast<int>(source.size()),
                   destination.data(),
                   required) == required;
    } catch (...) {
        destination.clear();
        return false;
    }
}

bool WideToUtf8(
    std::wstring_view source,
    std::string& destination) noexcept
{
    try {
        destination.clear();
        if (source.empty()) {
            return true;
        }
        if (!FitsWindowsLength(source)) {
            return false;
        }
        const int required = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            source.data(),
            static_cast<int>(source.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (required <= 0) {
            return false;
        }
        destination.resize(static_cast<std::size_t>(required));
        return WideCharToMultiByte(
                   CP_UTF8,
                   WC_ERR_INVALID_CHARS,
                   source.data(),
                   static_cast<int>(source.size()),
                   destination.data(),
                   required,
                   nullptr,
                   nullptr) == required;
    } catch (...) {
        destination.clear();
        return false;
    }
}

} // namespace inputweaver::win32
