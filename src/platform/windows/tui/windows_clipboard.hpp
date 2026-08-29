#pragma once

#include <string>
#include <string_view>

namespace inputweaver::win32 {

[[nodiscard]] bool CopyTextToClipboard(
    std::string_view text,
    std::string& error) noexcept;

} // namespace inputweaver::win32
