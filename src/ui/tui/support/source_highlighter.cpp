#include "source_highlighter.hpp"

#include <algorithm>
#include <array>

namespace inputweaver::ui::tui {
namespace {

inline constexpr std::array kKeywords{
    std::string_view{"exit"}, std::string_view{"pause"},
    std::string_view{"when"}, std::string_view{"repeat"},
    std::string_view{"and"}, std::string_view{"or"},
    std::string_view{"not"}, std::string_view{"if"},
    std::string_view{"then"}, std::string_view{"else"},
    std::string_view{"while"}, std::string_view{"do"},
    std::string_view{"end"}};
inline constexpr std::array kTypes{
    std::string_view{"state"}, std::string_view{"number"},
    std::string_view{"duration"}};
inline constexpr std::array kBuiltinVariables{
    std::string_view{"TARGET"}, std::string_view{"TAP_DURATION"},
    std::string_view{"ACTION_GAP"}, std::string_view{"PAUSE"}};
inline constexpr std::array kConstants{
    std::string_view{"GLOBAL"}, std::string_view{"on"},
    std::string_view{"off"}, std::string_view{"held"},
    std::string_view{"idle"}, std::string_view{"down"},
    std::string_view{"up"}, std::string_view{"E0"},
    std::string_view{"E1"}};
inline constexpr std::array kArrows{
    std::string_view{"=>>"}, std::string_view{"~>>"},
    std::string_view{"->"}, std::string_view{"=>"},
    std::string_view{"~>"}};

[[nodiscard]] bool IsWordStart(char character) noexcept
{
    return (character >= 'A' && character <= 'Z')
        || (character >= 'a' && character <= 'z')
        || character == '_';
}

[[nodiscard]] bool IsWordCharacter(char character) noexcept
{
    return IsWordStart(character)
        || (character >= '0' && character <= '9');
}

template <typename Range>
[[nodiscard]] bool Contains(
    const Range& values,
    std::string_view word)
{
    return std::find(values.begin(), values.end(), word) != values.end();
}

[[nodiscard]] std::size_t SkipSpaces(
    std::string_view line,
    std::size_t offset) noexcept
{
    while (offset < line.size()
        && (line[offset] == ' ' || line[offset] == '\t')) {
        ++offset;
    }
    return offset;
}

[[nodiscard]] std::size_t ArrowLength(
    std::string_view line,
    std::size_t offset) noexcept
{
    for (const std::string_view arrow : kArrows) {
        if (line.compare(offset, arrow.size(), arrow) == 0) {
            return arrow.size();
        }
    }
    return 0U;
}

[[nodiscard]] std::size_t DottedNameEnd(
    std::string_view line,
    std::size_t offset) noexcept
{
    while (offset + 1U < line.size() && line[offset] == '.'
        && IsWordStart(line[offset + 1U])) {
        offset += 2U;
        while (offset < line.size() && IsWordCharacter(line[offset])) {
            ++offset;
        }
    }
    return offset;
}

} // namespace

std::vector<SourceTokenSpan> HighlightWeaveLine(
    std::string_view line,
    SourceHighlightState& state)
{
    std::vector<SourceTokenSpan> spans;
    std::size_t offset{};
    bool declarationName{};
    while (offset < line.size()) {
        if (state.blockComment) {
            const std::size_t end = line.find("*/", offset);
            if (end == std::string_view::npos) {
                spans.push_back({offset, line.size(), SourceTokenKind::Comment});
                break;
            }
            spans.push_back({offset, end + 2U, SourceTokenKind::Comment});
            offset = end + 2U;
            state.blockComment = false;
            continue;
        }
        if (line.compare(offset, 2U, "//") == 0) {
            spans.push_back({offset, line.size(), SourceTokenKind::Comment});
            break;
        }
        if (line.compare(offset, 2U, "/*") == 0) {
            const std::size_t end = line.find("*/", offset + 2U);
            if (end == std::string_view::npos) {
                spans.push_back({offset, line.size(), SourceTokenKind::Comment});
                state.blockComment = true;
                break;
            }
            spans.push_back({offset, end + 2U, SourceTokenKind::Comment});
            offset = end + 2U;
            continue;
        }
        if (line[offset] == '"') {
            const std::size_t begin = offset++;
            bool escaped = false;
            while (offset < line.size()) {
                const char character = line[offset++];
                if (character == '"' && !escaped) {
                    break;
                }
                escaped = character == '\\' && !escaped;
                if (character != '\\') {
                    escaped = false;
                }
            }
            spans.push_back({begin, offset, SourceTokenKind::String});
            continue;
        }
        if (line[offset] >= '0' && line[offset] <= '9') {
            const std::size_t begin = offset++;
            while (offset < line.size()
                && (IsWordCharacter(line[offset]) || line[offset] == '.')) {
                ++offset;
            }
            spans.push_back({begin, offset, SourceTokenKind::Constant});
            continue;
        }
        const std::size_t arrowLength = ArrowLength(line, offset);
        if (arrowLength != 0U) {
            spans.push_back({
                offset,
                offset + arrowLength,
                SourceTokenKind::Operator});
            offset += arrowLength;
            continue;
        }
        if (line[offset] == '|' || line[offset] == '('
            || line[offset] == ')') {
            spans.push_back({offset, offset + 1U, SourceTokenKind::Function});
            ++offset;
            continue;
        }
        if (!IsWordStart(line[offset])) {
            ++offset;
            continue;
        }

        const std::size_t begin = offset++;
        while (offset < line.size() && IsWordCharacter(line[offset])) {
            ++offset;
        }
        const std::string_view word = line.substr(begin, offset - begin);
        const std::size_t next = SkipSpaces(line, offset);
        if (Contains(kTypes, word)) {
            spans.push_back({begin, offset, SourceTokenKind::Type});
            declarationName = true;
        } else if (declarationName) {
            spans.push_back({begin, offset, SourceTokenKind::Variable});
            state.variables.emplace_back(word);
            declarationName = false;
        } else if (Contains(kBuiltinVariables, word)
            || Contains(state.variables, word)) {
            spans.push_back({begin, offset, SourceTokenKind::Variable});
        } else if (Contains(kConstants, word)) {
            spans.push_back({begin, offset, SourceTokenKind::Constant});
        } else if (word == "toggle"
            || (next < line.size() && line[next] == '('
                && (begin == 0U || line[begin - 1U] != '.'))) {
            spans.push_back({begin, offset, SourceTokenKind::Function});
        } else if (Contains(kKeywords, word)) {
            spans.push_back({begin, offset, SourceTokenKind::Keyword});
        } else if (word.front() >= 'A' && word.front() <= 'Z') {
            offset = DottedNameEnd(line, offset);
            spans.push_back({begin, offset, SourceTokenKind::Control});
        }
    }
    return spans;
}

} // namespace inputweaver::ui::tui
