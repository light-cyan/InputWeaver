#pragma once

#include <string>
#include <string_view>

namespace inputweaver::win32 {

[[nodiscard]] bool Utf8ToWide(
    std::string_view source,
    std::wstring& destination) noexcept;

[[nodiscard]] bool WideToUtf8(
    std::wstring_view source,
    std::string& destination) noexcept;

} // namespace inputweaver::win32
