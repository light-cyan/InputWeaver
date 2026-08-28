#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace inputweaver::ui::tui {

enum class SourceTokenKind {
    Keyword,
    Type,
    Variable,
    Constant,
    Control,
    Function,
    Operator,
    String,
    Comment,
};

struct SourceTokenSpan final {
    std::size_t beginByte{};
    std::size_t endByte{};
    SourceTokenKind kind{SourceTokenKind::Keyword};
};

struct SourceHighlightState final {
    bool blockComment{};
    std::vector<std::string> variables;
};

[[nodiscard]] std::vector<SourceTokenSpan> HighlightWeaveLine(
    std::string_view line,
    SourceHighlightState& state);

} // namespace inputweaver::ui::tui
