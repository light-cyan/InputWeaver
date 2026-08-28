#pragma once

#include <span>
#include <string>
#include <string_view>

namespace inputweaver::win32 {

[[nodiscard]] std::wstring QuoteCommandLineArgument(std::wstring_view argument);
[[nodiscard]] std::wstring BuildCommandLine(
    std::span<const std::wstring> arguments);

} // namespace inputweaver::win32
