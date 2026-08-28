#include "command_line.hpp"

namespace inputweaver::win32 {

std::wstring QuoteCommandLineArgument(std::wstring_view argument)
{
    if (!argument.empty()
        && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring{argument};
    }
    std::wstring result;
    result.push_back(L'\"');
    std::size_t backslashes = 0U;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'\"');
        } else {
            result.append(backslashes, L'\\');
            result.push_back(character);
        }
        backslashes = 0U;
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}

std::wstring BuildCommandLine(std::span<const std::wstring> arguments)
{
    std::wstring result;
    for (const std::wstring& argument : arguments) {
        if (!result.empty()) {
            result.push_back(L' ');
        }
        result += QuoteCommandLineArgument(argument);
    }
    return result;
}

} // namespace inputweaver::win32
