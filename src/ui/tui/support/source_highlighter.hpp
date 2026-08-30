#pragma once

#include "language/lexer.hpp"

#include <cstddef>
#include <functional>
#include <set>
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
    Action,
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
    language::LexerState lexer{};
    std::set<std::string, std::less<>> scalarNames;
    std::set<std::string, std::less<>> arrayNames;
    bool expectsDeclarationName{};
    bool declarationIsArray{};
};

[[nodiscard]] std::vector<SourceTokenSpan> HighlightWeaveLine(
    std::string_view line,
    SourceHighlightState& state);

} // namespace inputweaver::ui::tui
